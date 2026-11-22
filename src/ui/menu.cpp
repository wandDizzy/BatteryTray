#include "menu.h"
#include "theme.h"
#include "widget.h"
#include "devpanel.h"
#include "../app.h"
#include "../i18n/i18n.h"
#include <shellapi.h>
#include <string>
#include <vector>

void ApplyMenuTheme(bool dark) {
    // undocumented but stable uxtheme exports used by Explorer itself for dark menus
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    auto setMode = (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    auto flush = (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (setMode) setMode(dark ? 2 : 3);
    if (flush) flush();
}

namespace {
enum {
    CMD_DEVICE = 1000,   // + index * 10 + action
    CMD_SHOW_OFFLINE = 100, CMD_WIDGET, CMD_TRAY, CMD_BOTH,
    CMD_THEME_SYS, CMD_THEME_DARK, CMD_THEME_LIGHT,
    CMD_LANG_AUTO, CMD_LANG_EN, CMD_LANG_UK, CMD_LANG_RU,
    CMD_INT_5, CMD_INT_10, CMD_INT_30, CMD_INT_60, CMD_INT_300,
    CMD_REFRESH, CMD_AUTOSTART, CMD_SETTINGS, CMD_CONFIG, CMD_REQUEST, CMD_EXIT,
};
enum { ACT_SETTINGS = 0, ACT_SHOW = 1, ACT_LEFT = 2, ACT_RIGHT = 3 };

UINT Check(bool on) { return MF_STRING | (on ? MF_CHECKED : MF_UNCHECKED); }
}

void ShowMainMenu(HWND owner) {
    Config& c = AppConfig();
    ApplyMenuTheme(IsDark(c));
    const std::vector<Device> devs = AppDevices();   // copy: polling may update the list while the menu is open

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T("menu_hint"));
    for (size_t i = 0; i < devs.size(); i++) {
        const Device& d = devs[i];
        const DeviceSettings& ds = AppDeviceSettings(d.id);
        std::wstring label = AppDisplayName(d) + L":  ";
        label += d.battery >= 0 ? std::to_wstring(d.battery) + L"%" : std::wstring(L"?");
        if (d.online && d.charging) label += L" ⚡";
        label += L"   [" + DeviceSourceText(d) + L"]";
        if (d.idle || !d.online) label += L"   (" + DeviceStatusText(d) + L")";
        HMENU sub = CreatePopupMenu();
        UINT base = CMD_DEVICE + (UINT)i * 10;
        AppendMenuW(sub, MF_STRING, base + ACT_SETTINGS, T("menu_device_settings"));
        AppendMenuW(sub, Check(!ds.hidden), base + ACT_SHOW, T("menu_show_device"));
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sub, MF_STRING | (i == 0 ? MF_GRAYED : 0), base + ACT_LEFT, T("menu_move_left"));
        AppendMenuW(sub, MF_STRING | (i + 1 == devs.size() ? MF_GRAYED : 0), base + ACT_RIGHT, T("menu_move_right"));
        AppendMenuW(m, MF_POPUP | (ds.hidden ? MF_UNCHECKED : MF_CHECKED), (UINT_PTR)sub, label.c_str());
    }
    if (devs.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T("no_devices"));
    AppendMenuW(m, Check(c.showOffline), CMD_SHOW_OFFLINE, T("menu_show_offline"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(m, MF_STRING, CMD_WIDGET, T("menu_widget"));
    AppendMenuW(m, MF_STRING, CMD_TRAY, T("menu_tray"));
    AppendMenuW(m, MF_STRING, CMD_BOTH, T("menu_both"));
    CheckMenuRadioItem(m, CMD_WIDGET, CMD_BOTH, CMD_WIDGET + c.displayMode, MF_BYCOMMAND);

    HMENU theme = CreatePopupMenu();
    AppendMenuW(theme, MF_STRING, CMD_THEME_SYS, T("theme_system"));
    AppendMenuW(theme, MF_STRING, CMD_THEME_DARK, T("theme_dark"));
    AppendMenuW(theme, MF_STRING, CMD_THEME_LIGHT, T("theme_light"));
    int tsel = c.theme.mode == "dark" ? 1 : c.theme.mode == "light" ? 2 : 0;
    CheckMenuRadioItem(theme, CMD_THEME_SYS, CMD_THEME_LIGHT, CMD_THEME_SYS + tsel, MF_BYCOMMAND);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)theme, T("menu_theme"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    HMENU lang = CreatePopupMenu();
    AppendMenuW(lang, MF_STRING, CMD_LANG_AUTO, T("lang_auto"));
    AppendMenuW(lang, MF_STRING, CMD_LANG_EN, L"English");
    AppendMenuW(lang, MF_STRING, CMD_LANG_UK, L"Українська");
    AppendMenuW(lang, MF_STRING, CMD_LANG_RU, L"Русский");
    int lsel = c.language == L"en" ? 1 : c.language == L"uk" ? 2 : c.language == L"ru" ? 3 : 0;
    CheckMenuRadioItem(lang, CMD_LANG_AUTO, CMD_LANG_RU, CMD_LANG_AUTO + lsel, MF_BYCOMMAND);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)lang, T("menu_language"));

    HMENU iv = CreatePopupMenu();
    const int vals[] = {5, 10, 30, 60, 300};
    for (int i = 0; i < 5; i++) {
        wchar_t b[32];
        if (vals[i] < 60) swprintf(b, 32, T("every_s"), vals[i]);
        else swprintf(b, 32, T("every_m"), vals[i] / 60);
        AppendMenuW(iv, MF_STRING, CMD_INT_5 + i, b);
        if (c.intervalSec == vals[i]) CheckMenuRadioItem(iv, CMD_INT_5, CMD_INT_300, CMD_INT_5 + i, MF_BYCOMMAND);
    }
    AppendMenuW(m, MF_POPUP, (UINT_PTR)iv, T("menu_interval"));
    AppendMenuW(m, MF_STRING, CMD_REFRESH, T("menu_refresh"));
    AppendMenuW(m, Check(GetAutostart()), CMD_AUTOSTART, T("menu_autostart"));
    AppendMenuW(m, MF_STRING, CMD_SETTINGS, T("menu_settings"));
    AppendMenuW(m, MF_STRING, CMD_CONFIG, T("menu_config"));
    AppendMenuW(m, MF_STRING, CMD_REQUEST, T("menu_request"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_EXIT, T("menu_exit"));

    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(owner);
    UINT cmd = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (!cmd) return;

    if (cmd >= CMD_DEVICE) {
        size_t i = (cmd - CMD_DEVICE) / 10;
        int act = (cmd - CMD_DEVICE) % 10;
        if (i >= devs.size()) return;
        std::string id = devs[i].id;
        switch (act) {
        case ACT_SETTINGS: {
            RECT r;
            if (!WidgetItemRect(id, &r)) { POINT q; GetCursorPos(&q); r = RECT{q.x - 1, q.y - 1, q.x + 1, q.y + 1}; }
            AppOpenFlyout(id, r);
            break;
        }
        case ACT_SHOW: AppDeviceSettings(id).hidden = !AppDeviceSettings(id).hidden; AppApply(); break;
        case ACT_LEFT: AppMoveDevice(id, -1); break;
        case ACT_RIGHT: AppMoveDevice(id, +1); break;
        }
        return;
    }
    switch (cmd) {
    case CMD_SHOW_OFFLINE: c.showOffline = !c.showOffline; AppApply(); break;
    case CMD_WIDGET: case CMD_TRAY: case CMD_BOTH: c.displayMode = (int)(cmd - CMD_WIDGET); AppApply(); break;
    case CMD_THEME_SYS: c.theme.mode = "system"; AppApply(); break;
    case CMD_THEME_DARK: c.theme.mode = "dark"; AppApply(); break;
    case CMD_THEME_LIGHT: c.theme.mode = "light"; AppApply(); break;
    case CMD_LANG_AUTO: c.language = L"auto"; AppApply(); break;
    case CMD_LANG_EN: c.language = L"en"; AppApply(); break;
    case CMD_LANG_UK: c.language = L"uk"; AppApply(); break;
    case CMD_LANG_RU: c.language = L"ru"; AppApply(); break;
    case CMD_INT_5: case CMD_INT_10: case CMD_INT_30: case CMD_INT_60: case CMD_INT_300:
        c.intervalSec = vals[cmd - CMD_INT_5]; AppApply(); AppRefreshNow(); break;
    case CMD_REFRESH: AppRefreshNow(); break;
    case CMD_AUTOSTART: SetAutostart(!GetAutostart()); break;
    case CMD_SETTINGS: AppOpenSettings(0); break;
    case CMD_CONFIG: ShellExecuteW(nullptr, L"open", ConfigDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case CMD_REQUEST: ShellExecuteW(nullptr, L"open", BT_REPO_URL L"/issues/new", nullptr, nullptr, SW_SHOWNORMAL); break;
    case CMD_EXIT: AppExit(); break;
    }
}
