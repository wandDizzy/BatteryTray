// Razer mice and keyboards (wireless receivers and wired), 90-byte feature report protocol.
//
// Report layout (+1 leading report ID byte on Windows):
//   status, transaction id, remaining packets (2), protocol type, data size,
//   command class, command id, arguments[80], crc (xor of bytes 2..87), reserved
// Battery level: class 0x07 id 0x80 -> arguments[1] (0..255)
// Charging:      class 0x07 id 0x84 -> arguments[1] (1 = charging)
// Layout as documented by OpenRazer; independent implementation.
#include "devices.h"
#include "hid_util.h"
extern "C" {
#include <hidsdi.h>
}
#include <cwctype>
#include <map>

namespace {

const unsigned short kRazer = 0x1532;
const int kLen = 91;   // report ID + 90 bytes

enum { ST_BUSY = 1, ST_OK = 2, ST_FAIL = 3, ST_TIMEOUT = 4, ST_NOT_SUPPORTED = 5 };

// returns the status byte (0 = no answer); args[1] in *value
int Command(HANDLE h, unsigned char tid, unsigned char cls, unsigned char cmd, int* value) {
    unsigned char b[kLen] = {0};
    b[2] = tid;      // report[1]
    b[6] = 0x02;     // data size
    b[7] = cls;
    b[8] = cmd;
    unsigned char crc = 0;
    for (int i = 3; i <= 88; i++) crc ^= b[i];   // report bytes 2..87
    b[89] = crc;
    if (!HidD_SetFeature(h, b, kLen)) return 0;
    for (int attempt = 0; attempt < 6; attempt++) {
        Sleep(attempt == 0 ? 50 : 40);
        unsigned char r[kLen] = {0};
        if (!HidD_GetFeature(h, r, kLen)) return 0;
        int st = r[1];
        if (st == ST_BUSY || st == 0) continue;
        if (r[7] != cls || r[8] != cmd) continue;   // answer to something else
        if (value) *value = r[10];
        return st;
    }
    return ST_BUSY;
}

DevType Guess(const HidIface& f, const std::vector<HidIface>& all) {
    std::wstring n = f.product;
    for (auto& c : n) c = (wchar_t)towlower(c);
    if (n.find(L"keyboard") != std::wstring::npos || n.find(L"blackwidow") != std::wstring::npos ||
        n.find(L"huntsman") != std::wstring::npos || n.find(L"ornata") != std::wstring::npos ||
        n.find(L"deathstalker") != std::wstring::npos || n.find(L"pro type") != std::wstring::npos)
        return DevType::Keyboard;
    if (n.find(L"headset") != std::wstring::npos || n.find(L"barracuda") != std::wstring::npos ||
        n.find(L"kraken") != std::wstring::npos || n.find(L"blackshark") != std::wstring::npos)
        return DevType::Headset;
    for (auto& o : all) {   // main interface of the receiver
        if (o.vid != f.vid || o.pid != f.pid || InterfaceNumber(o.path) != 0) continue;
        if (o.usagePage == 0x01 && o.usage == 0x06) return DevType::Keyboard;
        if (o.usagePage == 0x01 && o.usage == 0x02) return DevType::Mouse;
    }
    return DevType::Mouse;
}

std::wstring CleanName(std::wstring n) {
    const wchar_t* tails[] = {L" Dongle", L" Receiver", L" Wireless Receiver", L" (Wired)", L" Wired"};
    for (auto t : tails) {
        size_t p = n.rfind(t);
        if (p != std::wstring::npos && p + wcslen(t) == n.size()) n.erase(p);
    }
    return n.empty() ? L"Razer device" : n;
}

struct Known { unsigned char tid = 0; std::wstring path; Device last; bool hasLast = false; };
std::map<std::string, Known> g_known;
std::map<std::wstring, int> g_unsupported;   // collections that never answered (wired keyboards etc.)

} // namespace

void PollRazer(std::vector<Device>& out) {
    auto ifaces = EnumHid();
    std::map<std::string, bool> done;
    for (auto& f : ifaces) {
        if (f.vid != kRazer || f.featLen != kLen) continue;
        if (g_unsupported[f.path] >= 3) continue;
        std::string id = "razer:" + HexId(f.vid) + ":" + HexId(f.pid);
        if (done[id]) continue;
        Known& k = g_known[id];
        if (!k.path.empty() && k.path != f.path) {
            // another collection of the same device answered before: wait for that one
            bool exists = false;
            for (auto& o : ifaces) if (o.path == k.path) exists = true;
            if (exists) continue;
        }
        HANDLE h = OpenHidRW(f.path, false);
        if (h == INVALID_HANDLE_VALUE) continue;

        // newer wireless models use 0x1F, most others 0x3F, older ones 0xFF
        const unsigned char tids[] = {0x1F, 0x3F, 0xFF, 0x9F};
        int value = 0, st = 0;
        if (k.tid) st = Command(h, k.tid, 0x07, 0x80, &value);
        if (st != ST_OK && st != ST_TIMEOUT) {
            for (unsigned char t : tids) {
                st = Command(h, t, 0x07, 0x80, &value);
                if (st == ST_OK || st == ST_TIMEOUT) { k.tid = t; break; }
            }
        }
        DbgLog("razer %04X:%04X tid=%02X status=%d value=%d", f.vid, f.pid, k.tid, st, value);

        if (st == ST_OK) {
            int chg = 0;
            int cst = Command(h, k.tid, 0x07, 0x84, &chg);
            Device d;
            d.id = id;
            d.name = CleanName(f.product);
            d.type = Guess(f, ifaces);
            d.source = "razer";
            d.battery = (value * 100 + 127) / 255;
            d.online = true;
            d.charging = cst == ST_OK && chg == 1;
            k.path = f.path;
            k.last = d;
            k.hasLast = true;
            out.push_back(d);
            done[id] = true;
        } else if (k.hasLast) {
            // receiver is there but the mouse sleeps / is off
            Device d = k.last;
            d.idle = true;
            d.charging = false;
            out.push_back(d);
            done[id] = true;
        }
        else if (st == ST_NOT_SUPPORTED || st == 0) {
            g_unsupported[f.path]++;   // a mouse that is just switched off answers "timeout", not this
        }
        CloseHandle(h);
    }
}
