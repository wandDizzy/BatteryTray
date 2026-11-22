#pragma once
// User settings, stored as JSON.
// Location: config.json next to the exe (portable mode) if that file exists,
// otherwise %APPDATA%\BatteryTray\config.json.
#include "../core/devices.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <map>
#include <string>
#include <vector>

enum IconStyle { STYLE_TEXT = 0, STYLE_BADGE = 1, STYLE_BATTERY = 2 };
enum DisplayMode { MODE_WIDGET = 0, MODE_TRAY = 1, MODE_BOTH = 2 };

struct DeviceSettings {
    bool         hidden = false;
    std::wstring customName;          // empty = name reported by the device
    int          order = -1;          // position in the widget, -1 = not assigned yet
    std::string  glyph = "auto";      // auto | mouse | keyboard | headset | gamepad | speaker | phone | pen | device | bluetooth
    std::string  color;               // "" = automatic, otherwise "#RRGGBB" for the pictogram
    bool         notify = true;       // low-battery notification for this device
    int          lowThreshold = -1;   // -1 = use the global threshold
};

struct ThemeSettings {
    std::string  mode = "system";     // system | dark | light
    std::string  accent = "system";   // system | blue | violet | pink | red | orange | green | teal | "#RRGGBB"
    std::string  background = "mica"; // mica | acrylic | solid | image
    std::wstring image;               // background picture for the settings window
    int          imageBlur = 24;      // 0..60
    int          imageDim = 45;       // 0..90 %
    bool         animations = true;
};

struct OverlaySettings {
    bool        enabled = false;
    std::string mode = "always";     // always | low | hotkey
    std::string corner = "top-right"; // top-left | top-right | bottom-left | bottom-right
    int         opacity = 70;        // panel background, %
    int         scale = 100;         // %
    bool        showNames = true;
    std::string hotkey = "shift+tab"; // shift+tab (only in fullscreen games) | ctrl+shift+b | alt+b | ctrl+alt+b
};

struct Config {
    int  intervalSec = 5;
    int  lowThreshold = 20;
    int  midThreshold = 50;
    bool notifyLow = true;
    bool showOffline = true;
    int  iconStyle = STYLE_TEXT;        // tray icon style
    int  displayMode = MODE_WIDGET;     // taskbar widget / tray icons / both
    bool widgetPercent = true;          // "%" after the number in the widget
    bool widgetHover = true;            // hover highlight in the widget
    bool debugLog = false;
    COLORREF colHigh = RGB(76, 217, 100);
    COLORREF colMid = RGB(255, 196, 61);
    COLORREF colLow = RGB(255, 84, 84);
    COLORREF colCharging = RGB(77, 166, 255);
    COLORREF colOffline = RGB(140, 140, 140);
    COLORREF colText = RGB(255, 255, 255);   // text on the tray "badge" style
    std::wstring language = L"auto";         // auto | en | uk | ru
    ThemeSettings theme;
    OverlaySettings overlay;
    std::map<std::string, DeviceSettings> devices;
    std::vector<DongleSpec> customDongles;
};

std::wstring ConfigPath();
std::wstring ConfigDir();
void LoadConfig(Config& c);
void SaveConfig(const Config& c);

bool GetAutostart();
void SetAutostart(bool on);

// helpers
std::string  ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);
std::string  ColorToHex(COLORREF c);
bool         HexToColor(const std::string& s, COLORREF* out);
