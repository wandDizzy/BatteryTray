// Gamepads.
//  * XInput (Xbox and Xbox-compatible pads, including most 2.4 GHz pads in "X" mode):
//    Windows reports the battery in 4 steps (empty / low / medium / full).
//  * Sony DualShock 4 and DualSense: battery is part of every input report.
#include "devices.h"
#include "hid_util.h"
#include <xinput.h>
#include <map>

namespace {

typedef DWORD(WINAPI* GetBatteryFn)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
typedef DWORD(WINAPI* GetStateFn)(DWORD, XINPUT_STATE*);

struct XInputApi {
    GetBatteryFn battery = nullptr;
    GetStateFn state = nullptr;
    XInputApi() {
        HMODULE m = LoadLibraryExW(L"xinput1_4.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!m) return;
        battery = (GetBatteryFn)GetProcAddress(m, "XInputGetBatteryInformation");
        state = (GetStateFn)GetProcAddress(m, "XInputGetState");
    }
};

std::wstring XInputName(const std::vector<HidIface>& ifaces) {
    // XInput devices also expose a HID collection whose path contains "ig_"
    std::wstring name;
    int count = 0;
    for (auto& f : ifaces) {
        std::wstring p = f.path;
        for (auto& c : p) c = (wchar_t)towlower(c);
        if (p.find(L"ig_") == std::wstring::npos || f.product.empty()) continue;
        if (name != f.product) { name = f.product; count++; }
    }
    // some pads put a line break into their name
    for (auto& c : name) if (c == L'\n' || c == L'\r' || c == L'\t') c = L' ';
    while (!name.empty() && name.back() == L' ') name.pop_back();
    while (name.find(L" )") != std::wstring::npos) name.replace(name.find(L" )"), 2, L")");
    if (count == 1) {
        // "Controller (Xbox One For Windows)" -> keep as is; generic names get a nicer label
        if (name.find(L"XINPUT") != std::wstring::npos || name == L"Controller") return L"";
        return name;
    }
    return L"";
}

void PollXInput(std::vector<Device>& out, const std::vector<HidIface>& ifaces) {
    static XInputApi api;
    if (!api.battery || !api.state) return;
    std::wstring hidName;
    bool nameDone = false;
    for (DWORD i = 0; i < 4; i++) {
        XINPUT_STATE st;
        if (api.state(i, &st) != ERROR_SUCCESS) continue;   // nothing in this slot
        XINPUT_BATTERY_INFORMATION bi{};
        if (api.battery(i, BATTERY_DEVTYPE_GAMEPAD, &bi) != ERROR_SUCCESS) continue;
        DbgLog("xinput %lu type=%u level=%u", i, bi.BatteryType, bi.BatteryLevel);
        if (bi.BatteryType == BATTERY_TYPE_DISCONNECTED) continue;
        // Many 2.4 GHz pads (GamePro and other Xbox-compatible ones) tell Windows they are wired, so nobody
        // can know their level. They are still listed, with an unknown level, so the user sees them.
        if (!nameDone) { hidName = XInputName(ifaces); nameDone = true; }
        Device d;
        d.id = "xinput:" + std::to_string(i);
        d.name = hidName.empty() ? L"Gamepad " + std::to_wstring(i + 1) : hidName;
        d.type = DevType::Gamepad;
        d.source = "xinput";
        d.online = true;
        if (bi.BatteryType == BATTERY_TYPE_UNKNOWN || bi.BatteryType == BATTERY_TYPE_WIRED) d.battery = -1;
        else {
            static const int lv[] = {5, 30, 65, 100};
            d.battery = lv[bi.BatteryLevel & 3];
        }
        out.push_back(d);
    }
}

// ------------------------------------------------------------------ Sony

bool IsDs4(unsigned short pid) { return pid == 0x05C4 || pid == 0x09CC || pid == 0x0BA0; }
bool IsDualSense(unsigned short pid) { return pid == 0x0CE6 || pid == 0x0DF2; }

void PollSony(std::vector<Device>& out, const std::vector<HidIface>& ifaces) {
    std::map<std::wstring, bool> seen;
    for (auto& f : ifaces) {
        if (f.vid != 0x054C || !(IsDs4(f.pid) || IsDualSense(f.pid))) continue;
        if (!(f.usagePage == 0x01 && f.usage == 0x05)) continue;   // gamepad collection
        if (seen[f.path]) continue;
        seen[f.path] = true;
        HidPort p;
        if (!p.Open(f)) continue;
        std::vector<unsigned char> r;
        int n = 0;
        for (int tries = 0; tries < 3; tries++) {   // pads stream reports continuously
            n = p.Read(r, 150);
            if (n > 0) break;
        }
        if (n <= 0) continue;
        // HidPort removed a leading report ID 0; DS4/DualSense use non-zero IDs so r[0] is the ID
        int rid = r[0];
        int level = -1;
        bool charging = false, full = false;
        if (IsDs4(f.pid)) {
            int off = rid == 0x01 ? 30 : rid == 0x11 ? 32 : -1;          // USB / Bluetooth
            if (off < 0 || off >= n) continue;
            int v = r[off] & 0x0F;
            bool cable = (r[off] & 0x10) != 0;
            if (cable) { charging = v < 11; full = v >= 11; level = v >= 11 ? 100 : v * 10; }
            else level = v * 10 + 5 > 100 ? 100 : v * 10 + 5;
        } else {
            int off = rid == 0x01 ? 53 : rid == 0x31 ? 54 : -1;
            if (off < 0 || off >= n) continue;
            int v = r[off] & 0x0F, s = (r[off] >> 4) & 0x0F;
            charging = s == 1;
            full = s == 2;
            level = full ? 100 : (v * 10 + 5 > 100 ? 100 : v * 10 + 5);
        }
        Device d;
        d.id = "sony:" + HexId(f.vid) + ":" + HexId(f.pid) + (rid == 0x01 ? ":usb" : ":bt");
        d.name = IsDs4(f.pid) ? L"DualShock 4" : (f.pid == 0x0DF2 ? L"DualSense Edge" : L"DualSense");
        d.type = DevType::Gamepad;
        d.source = rid == 0x01 ? "usb" : "bluetooth";
        d.battery = level < 0 ? -1 : level > 100 ? 100 : level;
        d.online = true;
        d.charging = charging;
        out.push_back(d);
    }
}

} // namespace

void PollGamepads(std::vector<Device>& out) {
    auto ifaces = EnumHid();
    PollXInput(out, ifaces);
    PollSony(out, ifaces);
}
