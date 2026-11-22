// ASUS ROG / TUF wireless mice and keyboards (2.4 GHz receiver, Omni receiver or cable).
//
// Request (output report, ID 0, 64 bytes) on the receiver's vendor collection:
//   mouse:    12 07  -> reply 12 07 .. [4] = %, [7..8] = mV (little endian), [9] = charging
//   keyboard: 12 01  -> reply 12 01 .. [5] = %, [8] = charging
//   error:    ff aa  -> unknown request, or the device is asleep / switched off
// Byte positions as published by several open-source tools (G-Helper, HaloBattery,
// AsusMouseBatteryIndicator); this is an independent implementation.
#include "devices.h"
#include "hid_util.h"
#include <cwctype>
#include <map>

namespace {

const unsigned short kAsus = 0x0B05;

enum class Kind { Unknown, Mouse, Keyboard, None };

struct Known {
    Kind kind = Kind::Unknown;
    std::wstring path;
    Device last;
    bool hasLast = false;
    DWORD lastTry = 0;
};
std::map<std::string, Known> g_known;

// Sends "a b" with the collection's report ID (0 on single receivers, 2/3 on the Omni receiver)
// and returns the reply without the report ID, so byte positions are the same for both.
std::vector<unsigned char> Ask(HidPort& p, unsigned char rid, unsigned char a, unsigned char b) {
    std::vector<unsigned char> req(65, 0);
    req[0] = rid;
    req[1] = a;
    req[2] = b;
    if (!p.Write(req)) return {};
    DWORD start = GetTickCount();
    std::vector<unsigned char> r;
    while (GetTickCount() - start < 400) {
        int n = p.Read(r, 400 - (GetTickCount() - start));
        if (n <= 0) return {};
        if (rid != 0) {
            if (r[0] != rid) continue;
            r.erase(r.begin());
            n--;
        }
        if (n >= 10 && ((r[0] == a && r[1] == b) || (r[0] == 0xFF && r[1] == 0xAA))) return r;
    }
    return {};
}

bool IsPeripheralChannel(const HidIface& f) {
    if (f.vid != kAsus || f.outLen < 64 || f.inLen < 10) return false;
    if (f.usagePage == 0xFF01) return true;
    // the Omni receiver also uses 0xFF00 / 0xFF02 (one channel per paired device);
    // laptop keyboards ("N-KEY Device", page 0xFF31) are never touched
    std::wstring n = f.product;
    for (auto& c : n) c = (wchar_t)towupper(c);
    bool rog = n.find(L"ROG") != std::wstring::npos || n.find(L"TUF") != std::wstring::npos;
    return rog && n.find(L"N-KEY") == std::wstring::npos && (f.usagePage == 0xFF00 || f.usagePage == 0xFF02);
}

std::wstring CleanName(std::wstring n, Kind k) {
    std::wstring u = n;
    for (auto& c : u) c = (wchar_t)towupper(c);
    // receivers are named after themselves ("ROG OMNI RECEIVER"), not after the paired device
    if (u.empty() || u == L"ROG" || u.find(L"RECEIVER") != std::wstring::npos || u.find(L"DONGLE") != std::wstring::npos ||
        u.find(L"OMNI") != std::wstring::npos)
        return k == Kind::Keyboard ? L"ASUS ROG Keyboard" : L"ASUS ROG Mouse";
    return n;
}

} // namespace

void PollAsus(std::vector<Device>& out) {
    auto ifaces = EnumHid();
    std::map<std::string, bool> done;
    for (auto& f : ifaces) {
        if (!IsPeripheralChannel(f)) continue;
        std::string id = "asus:" + HexId(f.vid) + ":" + HexId(f.pid) + ":" + HexId(f.usagePage);
        if (done[id]) continue;
        Known& k = g_known[id];
        // collections that did not answer (laptop keyboards, Aura controllers, a sleeping mouse
        // that was never seen awake) are retried once a minute instead of every poll
        if (k.kind == Kind::None) {
            if (GetTickCount() - k.lastTry < 60000) continue;
            k.kind = Kind::Unknown;
        }
        HidPort p;
        if (!p.Open(f)) continue;

        std::vector<unsigned char> r;
        Kind kind = k.kind;
        bool asleep = false;
        if (kind == Kind::Unknown || kind == Kind::Mouse) {
            r = Ask(p, f.inReportId, 0x12, 0x07);
            // the Omni receiver answers 12 07 with zeros on channels without a mouse (keyboard / receiver)
            bool empty = !r.empty() && r[0] == 0x12 && r[4] == 0 && r[7] == 0 && r[8] == 0;
            if (empty) { r.clear(); if (kind == Kind::Mouse) kind = Kind::Unknown; }
            if (!r.empty() && r[0] == 0x12) kind = Kind::Mouse;
            else if (!r.empty() && kind == Kind::Mouse) asleep = true;
            else if (!r.empty()) r.clear();   // ff aa: not a mouse, try the keyboard request
        }
        if (!asleep && (kind == Kind::Unknown || kind == Kind::Keyboard) && (r.empty() || r[0] != 0x12)) {
            r = Ask(p, f.inReportId, 0x12, 0x01);
            if (!r.empty() && r[0] == 0x12 && r[5] == 0 && r[8] == 0 && r[4] == 0) r.clear();   // empty channel
            if (!r.empty() && r[0] == 0x12) kind = Kind::Keyboard;
            else if (!r.empty() && kind == Kind::Keyboard) asleep = true;
        }
        if (!r.empty()) DbgLog("asus %04X:%04X %s", f.vid, f.pid, HexDump(r.data(), 12).c_str());

        if ((kind == Kind::Mouse || kind == Kind::Keyboard) && !r.empty() && r[0] == 0x12) {
            int pct = kind == Kind::Mouse ? r[4] : r[5];
            bool chg = kind == Kind::Mouse ? r[9] != 0 : r[8] != 0;
            if (pct > 100) { asleep = true; }
            else {
                Device d;
                d.id = id;
                d.name = CleanName(f.product, kind);
                d.type = kind == Kind::Mouse ? DevType::Mouse : DevType::Keyboard;
                d.source = "asus";
                d.battery = pct;
                d.online = true;
                d.charging = chg;
                k.kind = kind;
                k.path = f.path;
                k.last = d;
                k.hasLast = true;
                out.push_back(d);
                done[id] = true;
                continue;
            }
        }
        if (k.hasLast) {   // receiver present, device asleep or off: keep the last level
            Device d = k.last;
            d.idle = true;
            d.charging = false;
            out.push_back(d);
            done[id] = true;
        } else if (r.empty() || (r[0] == 0xFF && r[1] == 0xAA)) {
            k.kind = kind == Kind::Unknown ? Kind::None : kind;
            k.lastTry = GetTickCount();
        }
    }
}
