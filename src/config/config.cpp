#include "config.h"
#include "json.h"
#include <shlobj.h>
#include <cstdio>
#include <cstdlib>

static const wchar_t* kApp = L"BatteryTray";

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string ColorToHex(COLORREF c) {
    char b[8];
    snprintf(b, sizeof(b), "#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return b;
}

bool HexToColor(const std::string& s, COLORREF* out) {
    std::string h = s;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() != 6) return false;
    char* end = nullptr;
    unsigned long v = strtoul(h.c_str(), &end, 16);
    if (!end || *end) return false;
    *out = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

static std::wstring ExeDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    return s.substr(0, s.find_last_of(L'\\'));
}

std::wstring ConfigPath() {
    std::wstring portable = ExeDir() + L"\\config.json";
    if (GetFileAttributesW(portable.c_str()) != INVALID_FILE_ATTRIBUTES) return portable;
    wchar_t base[MAX_PATH] = L"";
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base);
    std::wstring dir = std::wstring(base) + L"\\BatteryTray";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\config.json";
}

std::wstring ConfigDir() {
    std::wstring p = ConfigPath();
    return p.substr(0, p.find_last_of(L'\\'));
}

static const char* TypeName(DevType t) {
    switch (t) {
    case DevType::Mouse: return "mouse";
    case DevType::Keyboard: return "keyboard";
    case DevType::Headset: return "headset";
    case DevType::Gamepad: return "gamepad";
    default: return "other";
    }
}
static DevType TypeFromName(const std::string& s) {
    if (s == "mouse") return DevType::Mouse;
    if (s == "keyboard") return DevType::Keyboard;
    if (s == "headset") return DevType::Headset;
    if (s == "gamepad") return DevType::Gamepad;
    return DevType::Other;
}

static void ReadColor(const Json& j, const char* key, COLORREF& c) {
    COLORREF v;
    if (HexToColor(j[key].asStr(), &v)) c = v;
}

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void LoadConfig(Config& c) {
    FILE* f = _wfopen(ConfigPath().c_str(), L"rb");
    if (!f) return;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    Json j;
    if (!Json::parse(text, j) || !j.isObject()) return;

    c.intervalSec = Clamp(j["interval"].asInt(c.intervalSec), 2, 3600);
    c.lowThreshold = Clamp(j["lowThreshold"].asInt(c.lowThreshold), 1, 99);
    c.midThreshold = Clamp(j["midThreshold"].asInt(c.midThreshold), 1, 99);
    c.notifyLow = j["notifyLow"].asBool(c.notifyLow);
    c.showOffline = j["showOffline"].asBool(c.showOffline);
    c.debugLog = j["debugLog"].asBool(c.debugLog);
    c.language = FromUtf8(j["language"].asStr("auto"));

    std::string disp = j["display"].asStr("widget");
    c.displayMode = disp == "tray" ? MODE_TRAY : disp == "both" ? MODE_BOTH : MODE_WIDGET;
    std::string ts = j["trayStyle"].asStr("number");
    c.iconStyle = ts == "badge" ? STYLE_BADGE : ts == "battery" ? STYLE_BATTERY : STYLE_TEXT;

    const Json& w = j["widget"];
    c.widgetPercent = w["percentSign"].asBool(c.widgetPercent);
    c.widgetHover = w["hoverHighlight"].asBool(c.widgetHover);

    const Json& th = j["theme"];
    c.theme.mode = th["mode"].asStr(c.theme.mode);
    c.theme.accent = th["accent"].asStr(c.theme.accent);
    c.theme.background = th["background"].asStr(c.theme.background);
    c.theme.image = FromUtf8(th["image"].asStr());
    c.theme.imageBlur = Clamp(th["imageBlur"].asInt(c.theme.imageBlur), 0, 60);
    c.theme.imageDim = Clamp(th["imageDim"].asInt(c.theme.imageDim), 0, 90);
    c.theme.animations = th["animations"].asBool(c.theme.animations);

    const Json& ov = j["overlay"];
    c.overlay.enabled = ov["enabled"].asBool(c.overlay.enabled);
    c.overlay.mode = ov["mode"].asStr(c.overlay.mode);
    c.overlay.corner = ov["corner"].asStr(c.overlay.corner);
    c.overlay.opacity = Clamp(ov["opacity"].asInt(c.overlay.opacity), 0, 100);
    c.overlay.scale = Clamp(ov["scale"].asInt(c.overlay.scale), 60, 200);
    c.overlay.showNames = ov["showNames"].asBool(c.overlay.showNames);
    c.overlay.hotkey = ov["hotkey"].asStr(c.overlay.hotkey);

    const Json& col = j["colors"];
    ReadColor(col, "high", c.colHigh);
    ReadColor(col, "medium", c.colMid);
    ReadColor(col, "low", c.colLow);
    ReadColor(col, "charging", c.colCharging);
    ReadColor(col, "unknown", c.colOffline);
    ReadColor(col, "badgeText", c.colText);

    c.devices.clear();
    for (auto& kv : j["devices"].items()) {
        const Json& d = kv.second;
        DeviceSettings s;
        s.hidden = d["hidden"].asBool(false);
        s.customName = FromUtf8(d["name"].asStr());
        s.order = d["order"].asInt(-1);
        s.glyph = d["icon"].asStr("auto");
        s.color = d["color"].asStr();
        s.notify = d["notify"].asBool(true);
        s.lowThreshold = d["lowThreshold"].asInt(-1);
        c.devices[kv.first] = s;
    }

    c.customDongles.clear();
    const Json& dl = j["customDongles"];
    for (size_t i = 0; i < dl.size(); i++) {
        const Json& d = dl.at(i);
        DongleSpec s;
        s.vid = (unsigned short)strtoul(d["vid"].asStr("0").c_str(), nullptr, 16);
        s.pid = (unsigned short)strtoul(d["pid"].asStr("0").c_str(), nullptr, 16);
        s.name = FromUtf8(d["name"].asStr());
        s.type = TypeFromName(d["type"].asStr("other"));
        s.battByte = d["batteryByte"].asInt(-1);
        s.idleByte = d["idleByte"].asInt(-1);
        s.idleValue = d["idleValue"].asInt(1);
        s.chargeByte = d["chargeByte"].asInt(-1);
        s.chargeValue = d["chargeValue"].asInt(1);
        if (s.vid && s.pid && s.battByte >= 0) c.customDongles.push_back(s);
    }
}

void SaveConfig(const Config& c) {
    Json j = Json::Obj();
    j["interval"] = c.intervalSec;
    j["lowThreshold"] = c.lowThreshold;
    j["midThreshold"] = c.midThreshold;
    j["notifyLow"] = c.notifyLow;
    j["showOffline"] = c.showOffline;
    j["language"] = ToUtf8(c.language);
    j["display"] = c.displayMode == MODE_TRAY ? "tray" : c.displayMode == MODE_BOTH ? "both" : "widget";
    j["trayStyle"] = c.iconStyle == STYLE_BADGE ? "badge" : c.iconStyle == STYLE_BATTERY ? "battery" : "number";
    j["debugLog"] = c.debugLog;

    Json& w = j["widget"];
    w["percentSign"] = c.widgetPercent;
    w["hoverHighlight"] = c.widgetHover;

    Json& th = j["theme"];
    th["mode"] = c.theme.mode;
    th["accent"] = c.theme.accent;
    th["background"] = c.theme.background;
    th["image"] = ToUtf8(c.theme.image);
    th["imageBlur"] = c.theme.imageBlur;
    th["imageDim"] = c.theme.imageDim;
    th["animations"] = c.theme.animations;

    Json& ov = j["overlay"];
    ov["enabled"] = c.overlay.enabled;
    ov["mode"] = c.overlay.mode;
    ov["corner"] = c.overlay.corner;
    ov["opacity"] = c.overlay.opacity;
    ov["scale"] = c.overlay.scale;
    ov["showNames"] = c.overlay.showNames;
    ov["hotkey"] = c.overlay.hotkey;

    Json& col = j["colors"];
    col["high"] = ColorToHex(c.colHigh);
    col["medium"] = ColorToHex(c.colMid);
    col["low"] = ColorToHex(c.colLow);
    col["charging"] = ColorToHex(c.colCharging);
    col["unknown"] = ColorToHex(c.colOffline);
    col["badgeText"] = ColorToHex(c.colText);

    Json& devs = j["devices"];
    devs = Json::Obj();
    for (auto& kv : c.devices) {
        Json& d = devs[kv.first];
        d["name"] = ToUtf8(kv.second.customName);
        d["hidden"] = kv.second.hidden;
        d["order"] = kv.second.order;
        d["icon"] = kv.second.glyph;
        d["color"] = kv.second.color;
        d["notify"] = kv.second.notify;
        d["lowThreshold"] = kv.second.lowThreshold;
    }

    Json& dl = j["customDongles"];
    dl = Json::Arr();
    for (auto& s : c.customDongles) {
        Json d = Json::Obj();
        char b[8];
        snprintf(b, sizeof(b), "%04X", s.vid); d["vid"] = std::string(b);
        snprintf(b, sizeof(b), "%04X", s.pid); d["pid"] = std::string(b);
        d["name"] = ToUtf8(s.name);
        d["type"] = TypeName(s.type);
        d["batteryByte"] = s.battByte;
        d["idleByte"] = s.idleByte;
        d["idleValue"] = s.idleValue;
        d["chargeByte"] = s.chargeByte;
        d["chargeValue"] = s.chargeValue;
        dl.push(d);
    }

    std::string text = j.dump(2) + "\n";
    std::wstring path = ConfigPath();
    std::wstring tmp = path + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool GetAutostart() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    bool on = RegQueryValueExW(k, kApp, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(k);
    return on;
}

void SetAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(k, kApp, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kApp);
    }
    RegCloseKey(k);
}
