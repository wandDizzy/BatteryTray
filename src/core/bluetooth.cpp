// Bluetooth (classic + LE) battery levels as reported by Windows itself.
#include "devices.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <objbase.h>
#include <devpropdef.h>
#include <map>
#include <cwctype>

namespace {
const DEVPROPKEY kBattery = {{0x104EA319, 0x6EE2, 0x4701, {0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5}}, 2};
const DEVPROPKEY kFriendly = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};
const DEVPROPKEY kName = {{0xB725F130, 0x47EF, 0x101A, {0xA5, 0xF1, 0x02, 0x60, 0x8C, 0x9E, 0xBA, 0xC0}}, 10};
const DEVPROPKEY kContainer = {{0x8C7ED206, 0x3F8A, 0x4827, {0xB3, 0xAB, 0xAE, 0x9E, 0x1F, 0xAE, 0xFC, 0x6C}}, 2};

std::wstring GetStr(HDEVINFO set, SP_DEVINFO_DATA& di, const DEVPROPKEY& key) {
    DEVPROPTYPE type = 0;
    wchar_t buf[256] = L"";
    if (SetupDiGetDevicePropertyW(set, &di, &key, &type, (PBYTE)buf, sizeof(buf) - 2, nullptr, 0) &&
        type == DEVPROP_TYPE_STRING)
        return buf;
    return L"";
}

std::wstring Clean(std::wstring n) {
    const wchar_t* suffixes[] = {L" Hands-Free AG", L" Hands-Free", L" Avrcp Transport", L" Stereo", L" Hands-free"};
    for (auto s : suffixes) {
        size_t p = n.rfind(s);
        if (p != std::wstring::npos && p + wcslen(s) == n.size()) n.erase(p);
    }
    return n;
}

DevType Guess(const std::wstring& name) {
    std::wstring n = name;
    for (auto& c : n) c = towlower(c);
    auto has = [&](const wchar_t* s) { return n.find(s) != std::wstring::npos; };
    if (has(L"mouse") || has(L"mx ") || has(L"мыш") || has(L"миш")) return DevType::Mouse;
    if (has(L"keyboard") || has(L"keys") || has(L"клав")) return DevType::Keyboard;
    if (has(L"bud") || has(L"pods") || has(L"head") || has(L"ear") || has(L"wh-") || has(L"wf-") ||
        has(L"наушн") || has(L"навушн"))
        return DevType::Headset;
    if (has(L"controller") || has(L"gamepad") || has(L"joystick") || has(L"dualsense") || has(L"dualshock") ||
        has(L"джойст") || has(L"геймпад"))
        return DevType::Gamepad;
    return DevType::Other;
}
} // namespace

void PollBluetooth(std::vector<Device>& out) {
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return;
    std::map<std::wstring, size_t> byContainer;

    SP_DEVINFO_DATA di{sizeof(di)};
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &di); i++) {
        DEVPROPTYPE type = 0;
        BYTE level = 0;
        if (!SetupDiGetDevicePropertyW(set, &di, &kBattery, &type, &level, sizeof(level), nullptr, 0) ||
            type != DEVPROP_TYPE_BYTE)
            continue;

        GUID cont{};
        std::wstring contKey;
        if (SetupDiGetDevicePropertyW(set, &di, &kContainer, &type, (PBYTE)&cont, sizeof(cont), nullptr, 0)) {
            wchar_t g[64];
            StringFromGUID2(cont, g, 64);
            contKey = g;
        } else {
            wchar_t inst[256] = L"";
            SetupDiGetDeviceInstanceIdW(set, &di, inst, 256, nullptr);
            contKey = inst;
        }

        std::wstring name = GetStr(set, di, kFriendly);
        if (name.empty()) name = GetStr(set, di, kName);
        name = Clean(name);

        auto it = byContainer.find(contKey);
        if (it != byContainer.end()) {            // same physical device seen via another node
            Device& d = out[it->second];
            if (!name.empty() && name.size() < d.name.size()) d.name = name;
            continue;
        }
        Device d;
        d.id = "bt:" + std::string(contKey.begin(), contKey.end());
        d.name = name.empty() ? L"Bluetooth device" : name;
        d.type = Guess(d.name);
        d.source = "bluetooth";
        d.battery = level <= 100 ? level : -1;
        d.online = true;
        byContainer[contKey] = out.size();
        out.push_back(d);
    }
    SetupDiDestroyDeviceInfoList(set);
}
