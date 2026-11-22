// Keyboards on the Evision 2.4 GHz receiver 320F:5088 (and the cable 320F:5055, Telink 320F:50C3):
// Epomaker, VGN / VXE, ATK, IQUNIX, Rainy, Feker and many receivers named "Wireless Gaming Keyboard".
//
//   request  (output report 04 on FF1C:0092): 04 <seq lo> <seq hi> 1A <06 for VGN / VXE, else 00> 00 ...
//   reply    (input report 04):               04 <seq lo> <seq hi> 1A .. 00 00 .. <percent> <0 battery, 1/2 charging>
// The receiver answers about a second later. Asking wakes the keyboard (its backlight comes on), so the
// level is asked rarely over the receiver and remembered in between.
// Protocol as published by vgn-battery, HaloBattery and Wireless-Device-Battery-Monitor; own implementation.
#include "devices.h"
#include "hid_util.h"
#include <cwctype>
#include <map>

namespace {

const unsigned short kVid = 0x320F;

struct State {
    DWORD lastAsk = 0;
    bool  hasValue = false;
    Device last;
    int   variant = -1;    // 0x00 or 0x06 once known
};
std::map<std::string, State> g_state;
unsigned short g_seq = 0;

bool Upper(const std::wstring& s, const wchar_t* part) {
    std::wstring u = s;
    for (auto& c : u) c = (wchar_t)towupper(c);
    return u.find(part) != std::wstring::npos;
}

bool Ask(const HidIface& f, unsigned char variant, int& pct, bool& charging) {
    HidPort p;
    if (!p.Open(f)) return false;
    g_seq = (unsigned short)(g_seq % 0xFFFF + 1);
    std::vector<unsigned char> req(64, 0);
    req[0] = 0x04;
    req[1] = (unsigned char)(g_seq & 0xFF);
    req[2] = (unsigned char)(g_seq >> 8);
    req[3] = 0x1A;
    req[4] = variant;
    if (!p.Write(req)) return false;
    DWORD start = GetTickCount();
    std::vector<unsigned char> r;
    while (GetTickCount() - start < 2500) {
        int n = p.Read(r, 2500 - (GetTickCount() - start));
        if (n <= 0) return false;
        if (n < 10 || r[0] != 0x04 || r[1] != req[1] || r[2] != req[2] || r[3] != 0x1A || r[5] || r[6]) continue;
        DbgLog("evision %04X:%04X %s", f.vid, f.pid, HexDump(r.data(), 12).c_str());
        if (r[8] > 100 || r[9] > 2) return false;
        pct = r[8];
        charging = r[9] == 1 || r[9] == 2;
        return true;
    }
    DbgLog("evision %04X:%04X no answer (variant %02X)", f.vid, f.pid, variant);
    return false;
}

std::wstring KeyboardName(const std::wstring& product) {
    std::wstring n = product;
    const wchar_t* tails[] = {L" 2.4G Dongle", L" Dongle", L" 2.4G"};
    for (auto t : tails) {
        size_t p = n.rfind(t);
        if (p != std::wstring::npos && p + wcslen(t) == n.size()) n.erase(p);
    }
    return n.empty() ? L"Wireless Keyboard" : n;
}

} // namespace

void PollEvision(std::vector<Device>& out) {
    auto ifaces = EnumHid();
    std::map<std::string, bool> done;
    for (auto& f : ifaces) {
        if (f.vid != kVid || f.usagePage != 0xFF1C || f.usage != 0x0092 || f.outLen < 32 || f.inLen < 10) continue;
        std::string id = "evision:" + HexId(f.vid) + ":" + HexId(f.pid);
        if (done[id]) continue;
        done[id] = true;
        State& s = g_state[id];
        bool cable = f.pid == 0x5055;
        // over the receiver: every 10 minutes once known (asking wakes the keyboard), every 30 s until then
        DWORD every = cable ? 30000 : s.hasValue ? 600000 : 30000;
        if (s.lastAsk && GetTickCount() - s.lastAsk < every) {
            if (s.hasValue) out.push_back(s.last);
            continue;
        }
        s.lastAsk = GetTickCount();

        int pct = -1;
        bool chg = false;
        bool vgn = Upper(f.product, L"VGN") || Upper(f.product, L"VXE");
        unsigned char first = s.variant >= 0 ? (unsigned char)s.variant : (vgn ? 0x06 : 0x00);
        bool ok = Ask(f, first, pct, chg);
        if (!ok && s.variant < 0) {
            unsigned char other = first == 0x06 ? 0x00 : 0x06;
            if (Ask(f, other, pct, chg)) { ok = true; first = other; }
        }
        if (ok) {
            s.variant = first;
            Device d;
            d.id = id;
            d.name = KeyboardName(f.product);
            d.type = DevType::Keyboard;
            d.source = "dongle";
            d.battery = pct;
            d.online = true;
            d.charging = chg;
            s.last = d;
            s.hasValue = true;
            out.push_back(d);
        } else if (s.hasValue) {
            Device d = s.last;
            d.idle = true;   // asleep: keep the last known level
            d.charging = false;
            out.push_back(d);
        }
    }
}
