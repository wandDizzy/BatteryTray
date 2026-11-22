// BatteryTray — battery level of wireless mice / keyboards / headsets on the Windows taskbar.
//
//   src/core     device protocols (vendor dongles, Logitech HID++, Bluetooth)
//   src/config   config.json
//   src/i18n     translations
//   src/ui       taskbar widget, tray icons, menu, flyout, settings window
//   src/main.cpp app state, polling thread, message loop
#include "app.h"
#include "core/hid_util.h"
#include "i18n/i18n.h"
#include "ui/flyout.h"
#include "ui/menu.h"
#include "ui/settings_window.h"
#include "ui/theme.h"
#include "ui/tray_icons.h"
#include "ui/ui.h"
#include "ui/widget.h"
#include <shellapi.h>
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <set>

static const wchar_t* kWndClass = L"BatteryTrayWnd";
enum { WM_TRAY = WM_APP + 1, WM_POLLED = WM_APP + 2 };
static const UINT kPlaceholderId = 1;
static const UINT kNotifyId = 2;

static HWND g_wnd;
static Config g_cfg;
static std::vector<Device> g_devices;            // ordered
static std::map<std::string, UINT> g_iconIds;
static std::set<UINT> g_shown;
static std::set<std::string> g_notified;
static UINT g_nextId = 10;
static UINT g_taskbarCreated;
static HANDLE g_wake, g_quit;
static std::atomic<int> g_interval{5};
static bool g_notifyActive;

// ===================================================================== app API

Config& AppConfig() { return g_cfg; }
const std::vector<Device>& AppDevices() { return g_devices; }
HWND AppWindow() { return g_wnd; }

const Device* AppFindDevice(const std::string& id) {
    for (auto& d : g_devices) if (d.id == id) return &d;
    return nullptr;
}

DeviceSettings& AppDeviceSettings(const std::string& id) { return g_cfg.devices[id]; }

std::wstring AppDisplayName(const Device& d) {
    auto it = g_cfg.devices.find(d.id);
    if (it != g_cfg.devices.end() && !it->second.customName.empty()) return it->second.customName;
    return d.name;
}

const std::vector<GlyphChoice>& GlyphChoices() {
    static const std::vector<GlyphChoice> v = {
        {"auto", 0},           {"mouse", Ico::Mouse},     {"keyboard", Ico::Keyboard}, {"headset", Ico::Headset},
        {"gamepad", Ico::Gamepad}, {"speaker", Ico::Speaker}, {"phone", Ico::Phone},       {"pen", Ico::Pen},
        {"device", Ico::Device},   {"bluetooth", Ico::Bluetooth},
    };
    return v;
}

wchar_t AppGlyph(const Device& d) {
    auto it = g_cfg.devices.find(d.id);
    if (it != g_cfg.devices.end() && it->second.glyph != "auto")
        for (auto& g : GlyphChoices())
            if (it->second.glyph == g.key && g.ch) return g.ch;
    switch (d.type) {
    case DevType::Mouse: return Ico::Mouse;
    case DevType::Keyboard: return Ico::Keyboard;
    case DevType::Headset: return Ico::Headset;
    case DevType::Gamepad: return Ico::Gamepad;
    default: return d.source == "bluetooth" ? Ico::Bluetooth : Ico::Device;
    }
}

bool AppGlyphColor(const Device& d, COLORREF* c) {
    auto it = g_cfg.devices.find(d.id);
    return it != g_cfg.devices.end() && HexToColor(it->second.color, c);
}

void AppRefreshNow() { SetEvent(g_wake); }
void AppOpenFlyout(const std::string& id, const RECT& anchor) { FlyoutShow(id, anchor); }
void AppOpenSettings(int page) { FlyoutClose(); SettingsShow(page); }
void AppShowMenu() { ShowMainMenu(g_wnd); }
void AppExit() { DestroyWindow(g_wnd); }

static void SortDevices() {
    // every device gets a stable position the first time it is seen
    int next = 0;
    for (auto& kv : g_cfg.devices) next = (std::max)(next, kv.second.order + 1);
    bool assigned = false;
    for (auto& d : g_devices) {
        DeviceSettings& s = g_cfg.devices[d.id];
        if (s.order < 0) { s.order = next++; assigned = true; }
    }
    std::stable_sort(g_devices.begin(), g_devices.end(), [](const Device& a, const Device& b) {
        return g_cfg.devices[a.id].order < g_cfg.devices[b.id].order;
    });
    if (assigned) SaveConfig(g_cfg);
}

void AppMoveDevice(const std::string& id, int dir) {
    for (size_t i = 0; i < g_devices.size(); i++) {
        if (g_devices[i].id != id) continue;
        size_t j = i + dir;
        if (dir < 0 && i == 0) return;
        if (j >= g_devices.size()) return;
        std::swap(g_cfg.devices[g_devices[i].id].order, g_cfg.devices[g_devices[j].id].order);
        SortDevices();
        AppApply();
        return;
    }
}

// ===================================================================== tray icons

static std::wstring Tip(const Device& d) {
    std::wstring t = AppDisplayName(d);
    wchar_t b[64];
    if (d.battery >= 0) { swprintf(b, 64, L" — %d%%", d.battery); t += b; }
    else t += std::wstring(L" — ") + T("unknown");
    if (!d.online) t += std::wstring(L" (") + T("not_connected") + L")";
    else if (d.idle) t += std::wstring(L" (") + T("asleep") + L")";
    else if (d.charging) t += std::wstring(L" (") + T("charging") + L")";
    return t;
}

static void SetIcon(UINT id, HICON icon, const std::wstring& tip) {
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = g_wnd;
    n.uID = id;
    n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    n.uCallbackMessage = WM_TRAY;
    n.hIcon = icon;
    wcsncpy(n.szTip, tip.c_str(), 127);
    if (g_shown.count(id)) {
        Shell_NotifyIconW(NIM_MODIFY, &n);
    } else if (Shell_NotifyIconW(NIM_ADD, &n)) {
        n.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &n);
        g_shown.insert(id);
    }
    DestroyIcon(icon);
}

static void RemoveIcon(UINT id) {
    if (!g_shown.count(id)) return;
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = g_wnd;
    n.uID = id;
    Shell_NotifyIconW(NIM_DELETE, &n);
    g_shown.erase(id);
}

static void Balloon(UINT id, const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = g_wnd;
    n.uID = id;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_WARNING;
    wcsncpy(n.szInfoTitle, title.c_str(), 63);
    wcsncpy(n.szInfo, text.c_str(), 255);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static bool Visible(const Device& d) {
    auto it = g_cfg.devices.find(d.id);
    if (it != g_cfg.devices.end() && it->second.hidden) return false;
    if (!d.online && !g_cfg.showOffline) return false;
    return true;
}

// ===================================================================== refresh UI

static void UpdateUi() {
    int size = TrayIconSize();
    bool tray = g_cfg.displayMode != MODE_WIDGET;
    bool widget = g_cfg.displayMode != MODE_TRAY;
    std::set<UINT> wanted;
    std::vector<WidgetItem> items;

    for (auto& d : g_devices) {
        // low-battery notification (once per discharge cycle), also for hidden devices
        const DeviceSettings& ds = g_cfg.devices[d.id];
        int thr = ds.lowThreshold >= 0 ? ds.lowThreshold : g_cfg.lowThreshold;
        bool low = d.online && !d.idle && !d.charging && d.battery >= 0 && d.battery <= thr;
        UINT& id = g_iconIds[d.id];
        if (!id) id = g_nextId++;
        if (low && g_cfg.notifyLow && ds.notify && !g_notified.count(d.id)) {
            wchar_t b[200];
            swprintf(b, 200, T("low_text"), AppDisplayName(d).c_str(), d.battery);
            UINT nid = id;
            if (!(tray && Visible(d))) {
                SetIcon(kNotifyId, RenderAppIcon(size), L"BatteryTray");
                nid = kNotifyId;
                g_notifyActive = true;
                SetTimer(g_wnd, 2, 20000, nullptr);
            }
            Balloon(nid, T("low_title"), b);
            g_notified.insert(d.id);
        } else if (d.charging || d.battery > thr + 5) {
            g_notified.erase(d.id);
        }

        if (!Visible(d)) continue;
        if (tray) {
            wanted.insert(id);
            SetIcon(id, RenderDeviceIcon(d, g_cfg, size), Tip(d));
        }
        WidgetItem w;
        w.id = d.id;
        w.glyph = AppGlyph(d);
        w.customGlyphColor = AppGlyphColor(d, &w.glyphColor);
        w.text = d.battery >= 0 ? std::to_wstring(d.battery) + (g_cfg.widgetPercent ? L"%" : L"") : L"?";
        w.textColor = LevelColor(d, g_cfg);
        w.charging = d.online && d.charging;
        w.dim = !d.online || d.idle;
        w.tip = Tip(d);
        items.push_back(w);
    }
    if (tray && wanted.empty()) {
        wanted.insert(kPlaceholderId);
        SetIcon(kPlaceholderId, RenderAppIcon(size), std::wstring(L"BatteryTray — ") + T("no_devices"));
    }
    if (g_notifyActive) wanted.insert(kNotifyId);
    std::vector<UINT> gone;
    for (UINT id : g_shown) if (!wanted.count(id)) gone.push_back(id);
    for (UINT id : gone) RemoveIcon(id);

    if (widget) WidgetUpdate(g_wnd, items, g_cfg.widgetHover, g_cfg.theme.animations);
    else WidgetDestroy();
}

void AppApply() {
    g_interval = g_cfg.intervalSec;
    SetLanguage(g_cfg.language);
    SetCustomDongles(g_cfg.customDongles);
    SetDebugLog(g_cfg.debugLog);
    SaveConfig(g_cfg);
    UpdateUi();
    UiWindow::ApplyThemeAll();
}

// ===================================================================== polling

static DWORD WINAPI PollThread(LPVOID) {
    HANDLE ev[2] = {g_quit, g_wake};
    DWORD lastBt = 0;
    std::vector<Device> bt;
    for (;;) {
        auto* list = new std::vector<Device>();
        PollDongles(*list);
        PollHidpp(*list);
        PollRazer(*list);
        PollHeadsets(*list);
        PollGamepads(*list);
        // Bluetooth values come from Windows and change slowly: refresh every 30 s
        DWORD now = GetTickCount();
        if (!lastBt || now - lastBt > 30000) { bt.clear(); PollBluetooth(bt); lastBt = now; }
        list->insert(list->end(), bt.begin(), bt.end());
        if (!PostMessageW(g_wnd, WM_POLLED, 0, (LPARAM)list)) delete list;
        DWORD r = WaitForMultipleObjects(2, ev, FALSE, (DWORD)g_interval * 1000);
        if (r == WAIT_OBJECT_0) return 0;
        if (r == WAIT_OBJECT_0 + 1) lastBt = 0;   // manual refresh: Bluetooth too
    }
}

// ===================================================================== window

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_POLLED: {
        auto* list = (std::vector<Device>*)lp;
        g_devices = std::move(*list);
        delete list;
        SortDevices();
        UpdateUi();
        UiWindow::RepaintAll();
        return 0;
    }
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU: ShowMainMenu(h); break;
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONUP: {
            UINT id = HIWORD(lp);
            for (auto& kv : g_iconIds)
                if (kv.second == id) {
                    POINT p;
                    GetCursorPos(&p);
                    AppOpenFlyout(kv.first, RECT{p.x - 8, p.y - 8, p.x + 8, p.y + 8});
                    return 0;
                }
            AppOpenSettings(0);
            break;
        }
        }
        return 0;
    case WM_WIDGET_MENU:
        ShowMainMenu(h);
        return 0;
    case WM_WIDGET_CLICK: {
        std::string id = WidgetItemId((int)wp);
        RECT r;
        if (!id.empty() && WidgetItemRect(id, &r)) AppOpenFlyout(id, r);
        return 0;
    }
    case WM_WIDGET_REATTACH:
        WidgetReattach();
        return 0;
    case WM_DEVICECHANGE:   // receiver plugged / unplugged -> refresh soon
        SetTimer(h, 1, 1500, nullptr);
        return TRUE;
    case WM_TIMER:
        KillTimer(h, wp);
        if (wp == 1) SetEvent(g_wake);
        else if (wp == 2) { g_notifyActive = false; RemoveIcon(kNotifyId); }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC) SetTimer(h, 1, 3000, nullptr);
        return TRUE;
    case WM_SETTINGCHANGE:
        if (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) {
            UpdateUi();
            UiWindow::ApplyThemeAll();
        }
        return 0;
    case WM_DESTROY: {
        UiWindow::CloseAll();
        WidgetDestroy();
        std::vector<UINT> ids(g_shown.begin(), g_shown.end());
        for (UINT id : ids) RemoveIcon(id);
        PostQuitMessage(0);
        return 0;
    }
    }
    if (msg == g_taskbarCreated && g_taskbarCreated) {   // explorer restarted
        g_shown.clear();
        WidgetReattach();
        UpdateUi();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    HANDLE once = CreateMutexW(nullptr, TRUE, L"BatteryTray.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // already running: open its settings instead of starting twice
        HWND other = FindWindowW(kWndClass, nullptr);
        if (other) PostMessageW(other, WM_TRAY, 0, MAKELPARAM(WM_LBUTTONUP, 0));
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    LoadConfig(g_cfg);
    g_interval = g_cfg.intervalSec;
    SetLanguage(g_cfg.language);
    SetCustomDongles(g_cfg.customDongles);
    SetDebugLog(g_cfg.debugLog);
    ApplyMenuTheme(IsDark(g_cfg));

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kWndClass;
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(0, kWndClass, L"BatteryTray", 0, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(g_wnd, g_taskbarCreated, MSGFLT_ALLOW, nullptr);

    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_quit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE th = CreateThread(nullptr, 0, PollThread, nullptr, 0, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    SetEvent(g_quit);
    WaitForSingleObject(th, 3000);
    CoUninitialize();
    CloseHandle(once);
    return 0;
}
