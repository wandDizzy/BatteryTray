// Logitech HID++ 2.0 battery reading (Lightspeed / Unifying receivers, G-series headsets).
// Supported battery features: 0x1004 Unified Battery, 0x1000 Battery Status, 0x1F20 ADC (voltage).
#include "devices.h"
#include "hid_util.h"
#include <map>
#include <deque>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cwctype>

namespace {

const unsigned char kLong = 0x11;
const unsigned char kSwId = 0x0A;
const DWORD kTimeoutMs = 400;

struct Port {
    HANDLE h = INVALID_HANDLE_VALUE;
    HANDLE ev = nullptr;
    int inLen = 20, outLen = 20;
};

// 1 = ok, 0 = device answered with an error, -1 = no answer
int Xfer(Port& p, unsigned char dev, unsigned char feat, unsigned char fn,
         const unsigned char* params, int np, unsigned char* resp) {
    unsigned char req[64] = {0};
    req[0] = kLong; req[1] = dev; req[2] = feat; req[3] = (unsigned char)((fn << 4) | kSwId);
    if (params && np) memcpy(req + 4, params, np > 16 ? 16 : np);

    OVERLAPPED ov{}; ov.hEvent = p.ev;
    ResetEvent(p.ev);
    DWORD wr = 0;
    if (!WriteFile(p.h, req, p.outLen, &wr, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return -1;
        if (WaitForSingleObject(p.ev, kTimeoutMs) != WAIT_OBJECT_0) { CancelIo(p.h); return -1; }
        if (!GetOverlappedResult(p.h, &ov, &wr, FALSE)) return -1;
    }

    DWORD start = GetTickCount();
    unsigned char buf[64];
    for (;;) {
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= kTimeoutMs) return -1;
        ResetEvent(p.ev);
        DWORD rd = 0;
        if (!ReadFile(p.h, buf, p.inLen, &rd, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) return -1;
            if (WaitForSingleObject(p.ev, kTimeoutMs - elapsed) != WAIT_OBJECT_0) {
                CancelIo(p.h);
                GetOverlappedResult(p.h, &ov, &rd, TRUE);
                return -1;
            }
            if (!GetOverlappedResult(p.h, &ov, &rd, FALSE)) return -1;
        }
        if (rd < 7 || buf[1] != dev) continue;
        // HID++ 2.0 error: [2]=0xFF [3]=feat [4]=fn|sw ; HID++ 1.0 error: [2]=0x8F [3]=feat
        if ((buf[2] == 0xFF && buf[3] == feat && buf[4] == req[3]) || (buf[2] == 0x8F && buf[3] == feat))
            return 0;
        if (buf[2] == feat && buf[3] == req[3]) {
            memcpy(resp, buf, rd < 20 ? rd : 20);
            return 1;
        }
    }
}

int GetFeatureIndex(Port& p, unsigned char dev, unsigned short featureId) {
    unsigned char prm[2] = {(unsigned char)(featureId >> 8), (unsigned char)featureId};
    unsigned char r[20] = {0};
    int rc = Xfer(p, dev, 0x00, 0, prm, 2, r);
    if (rc < 0) return -2;          // no answer at all
    if (rc == 0) return -1;         // error (no such device / not HID++ 2.0)
    return r[4];                    // 0 = feature not supported
}

int VoltageToPercent(int mv) {
    static const int t[][2] = {{4186, 100}, {4067, 90}, {3989, 80}, {3922, 70}, {3859, 60}, {3811, 50},
                               {3778, 40}, {3751, 30}, {3717, 20}, {3671, 10}, {3646, 5}, {3579, 2}, {3500, 0}};
    if (mv >= t[0][0]) return 100;
    for (size_t i = 1; i < sizeof(t) / sizeof(t[0]); i++)
        if (mv >= t[i][0])
            return t[i][1] + (mv - t[i][0]) * (t[i - 1][1] - t[i][1]) / (t[i - 1][0] - t[i][0]);
    return 0;
}

struct CacheEntry {            // remembered per device so offline devices keep last value
    std::wstring name;
    DevType type = DevType::Other;
    int battery = -1;
    unsigned short featId = 0;
    unsigned char featIdx = 0;
    std::deque<int> mvHist;     // recent voltage readings for smoothing
    int smoothPct = -1;
};
std::map<std::string, CacheEntry> g_cache;

bool ReadBattery(Port& p, unsigned char dev, CacheEntry& c, int& pct, bool& charging) {
    unsigned char r[20] = {0};
    if (!c.featIdx) {
        const unsigned short ids[] = {0x1004, 0x1000, 0x1F20};
        for (auto id : ids) {
            int idx = GetFeatureIndex(p, dev, id);
            if (idx <= 0) { if (idx < 0) return false; continue; }
            c.featId = id; c.featIdx = (unsigned char)idx; break;
        }
        if (!c.featIdx) return false;
    }
    switch (c.featId) {
    case 0x1004:   // get_status: [4]=%, [5]=level flags, [6]=charging state
        if (Xfer(p, dev, c.featIdx, 1, nullptr, 0, r) != 1) return false;
        DbgLog("hidpp 1004 raw=%s", HexDump(r, 8).c_str());
        pct = r[4];
        if (!pct) pct = (r[5] & 8) ? 100 : (r[5] & 4) ? 50 : (r[5] & 2) ? 20 : (r[5] & 1) ? 5 : 0;
        charging = r[6] == 1 || r[6] == 2;
        return true;
    case 0x1000:   // get_level_status: [4]=%, [6]=status (1,2 = charging)
        if (Xfer(p, dev, c.featIdx, 0, nullptr, 0, r) != 1) return false;
        DbgLog("hidpp 1000 raw=%s", HexDump(r, 8).c_str());
        pct = r[4];
        charging = r[6] == 1 || r[6] == 2;
        return true;
    case 0x1F20: { // ADC measurement: [4..5] = mV big-endian, [6] = flags (bit0 connected, bit1 charging)
        if (Xfer(p, dev, c.featIdx, 0, nullptr, 0, r) != 1) return false;
        int mv = (r[4] << 8) | r[5];
        DbgLog("hidpp adc  mv=%d flags=%02X  raw=%s", mv, r[6], HexDump(r, 8).c_str());
        if (!(r[6] & 0x01) || mv < 3000) return false;      // headset switched off
        charging = (r[6] & 0x02) != 0;
        // Voltage dips for a moment under load (sound, mic), so a single low sample must not
        // drag the level down: use the median of the last minute of samples and only move
        // the shown value when the estimate differs by 2 % or more.
        c.mvHist.push_back(mv);
        while (c.mvHist.size() > 12) c.mvHist.pop_front();
        std::vector<int> sorted(c.mvHist.begin(), c.mvHist.end());
        std::sort(sorted.begin(), sorted.end());
        int med = sorted[sorted.size() / 2];
        pct = VoltageToPercent(med);   // same curve as the level the headset itself announces
        if (charging) {
            c.mvHist.clear();
            c.smoothPct = pct;
        } else if (c.smoothPct < 0 || pct >= c.smoothPct + 2 || pct <= c.smoothPct - 2) {
            c.smoothPct = pct;
        } else {
            pct = c.smoothPct;
        }
        return true;
    }
    }
    return false;
}

std::wstring ReadName(Port& p, unsigned char dev) {
    int idx = GetFeatureIndex(p, dev, 0x0005);
    if (idx <= 0) return L"";
    unsigned char r[20] = {0};
    if (Xfer(p, dev, (unsigned char)idx, 0, nullptr, 0, r) != 1) return L"";
    int len = r[4];
    std::string name;
    while ((int)name.size() < len) {
        unsigned char off = (unsigned char)name.size();
        if (Xfer(p, dev, (unsigned char)idx, 1, &off, 1, r) != 1) break;
        for (int i = 4; i < 20 && (int)name.size() < len; i++) name += (char)r[i];
    }
    return std::wstring(name.begin(), name.end());
}

DevType ReadType(Port& p, unsigned char dev, const std::wstring& name) {
    std::wstring n = name;
    for (auto& ch : n) ch = towlower(ch);
    if (n.find(L"headset") != std::wstring::npos || n.find(L"g733") != std::wstring::npos ||
        n.find(L"g935") != std::wstring::npos || n.find(L"g535") != std::wstring::npos ||
        n.find(L"pro x") != std::wstring::npos)
        return DevType::Headset;
    int idx = GetFeatureIndex(p, dev, 0x0005);
    if (idx > 0) {
        unsigned char r[20] = {0};
        if (Xfer(p, dev, (unsigned char)idx, 2, nullptr, 0, r) == 1) {
            if (r[4] == 0 || r[4] == 2) return DevType::Keyboard;
            if (r[4] == 3 || r[4] == 4 || r[4] == 5) return DevType::Mouse;
            if (r[4] == 8) return DevType::Headset;
        }
    }
    return DevType::Other;
}

void PollPort(Port& p, const HidIface& f, std::vector<Device>& out) {
    std::string base = "hidpp:" + HexId(f.vid) + ":" + HexId(f.pid) + ":";
    const unsigned char direct = 0xFF;

    auto emit = [&](unsigned char dev, bool online, int pct, bool chg) {
        char ix[4]; snprintf(ix, sizeof(ix), "%02X", dev);
        auto& c = g_cache[base + ix];
        Device d;
        d.id = base + ix;
        d.name = c.name.empty() ? f.product : c.name;
        d.type = c.type;
        d.source = "hidpp";
        d.online = online;
        if (online) c.battery = pct;
        d.battery = c.battery;
        d.charging = online && chg;
        out.push_back(d);
    };

    // Device directly behind the dongle (headsets, some Lightspeed mice) answers at index 0xFF
    {
        char ix[4]; snprintf(ix, sizeof(ix), "%02X", direct);
        auto& c = g_cache[base + ix];
        int probe = c.featIdx ? 1 : GetFeatureIndex(p, direct, 0x0000);
        if (probe >= 0) {   // root answered -> HID++ 2.0 device at 0xFF
            if (c.name.empty()) {
                c.name = ReadName(p, direct);
                if (c.name.empty()) c.name = f.product;
                c.type = ReadType(p, direct, c.name);
            }
            int pct = -1; bool chg = false;
            bool ok = ReadBattery(p, direct, c, pct, chg);
            if (ok || c.featIdx) { emit(direct, ok, pct, chg); return; }
        } else if (c.featIdx) {          // known device stopped answering -> offline
            emit(direct, false, -1, false);
            return;
        }
    }

    // Receiver with paired devices at indexes 1..6
    for (unsigned char dev = 1; dev <= 6; dev++) {
        char ix[4]; snprintf(ix, sizeof(ix), "%02X", dev);
        std::string key = base + ix;
        bool known = g_cache.count(key) && g_cache[key].featIdx;
        int root = known ? 1 : GetFeatureIndex(p, dev, 0x0000);
        if (root < 0 && !known) continue;
        auto& c = g_cache[key];
        if (c.name.empty()) {
            c.name = ReadName(p, dev);
            if (c.name.empty()) continue;
            c.type = ReadType(p, dev, c.name);
        }
        int pct = -1; bool chg = false;
        bool ok = ReadBattery(p, dev, c, pct, chg);
        if (ok || c.featIdx) emit(dev, ok, pct, chg);
    }
}

} // namespace

void PollHidpp(std::vector<Device>& out) {
    auto ifaces = EnumHid();
    std::map<std::string, bool> done;
    for (auto& f : ifaces) {
        if (f.vid != 0x046D) continue;
        if (f.usagePage < 0xFF00 || f.outLen != 20 || f.inLen != 20) continue;   // long-report collection
        std::string key = HexId(f.vid) + HexId(f.pid);
        if (done[key]) continue;
        Port p;
        p.h = OpenHidRW(f.path, true);
        if (p.h == INVALID_HANDLE_VALUE) continue;
        p.ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        p.inLen = f.inLen; p.outLen = f.outLen;
        PollPort(p, f, out);
        CloseHandle(p.ev);
        CloseHandle(p.h);
        done[key] = true;
    }
}
