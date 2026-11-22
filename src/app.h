#pragma once
// Application state shared by the taskbar widget, tray icons, menu and windows.
// Everything here is used from the UI thread only.
#include "config/config.h"
#include <string>
#include <vector>

#define BT_VERSION "2.0.6"
#define BT_REPO_URL L"https://github.com/wandDizzy/BatteryTray"

Config& AppConfig();
const std::vector<Device>& AppDevices();             // all devices from the last poll, in widget order
const Device* AppFindDevice(const std::string& id);
DeviceSettings& AppDeviceSettings(const std::string& id);

std::wstring AppDisplayName(const Device& d);
wchar_t      AppGlyph(const Device& d);               // resolved pictogram (Segoe Fluent Icons code point)
bool         AppGlyphColor(const Device& d, COLORREF* c);   // false = automatic

void AppApply();                    // settings changed: save, update widget/tray, repaint open windows
void AppRefreshNow();               // poll devices now
void AppMoveDevice(const std::string& id, int dir);   // -1 left, +1 right
void AppOpenFlyout(const std::string& id, const RECT& anchor);
void AppOpenSettings(int page = 0);
void AppShowMenu();
void AppExit();
HWND AppWindow();

// pictogram choices for the device panel
struct GlyphChoice { const char* key; wchar_t ch; };
const std::vector<GlyphChoice>& GlyphChoices();
