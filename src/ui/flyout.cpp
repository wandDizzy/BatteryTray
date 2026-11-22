#include "flyout.h"
#include "devpanel.h"
#include "../app.h"

namespace {

class Flyout;
Flyout* g_flyout = nullptr;

std::string g_lastClosedId;
DWORD g_lastClosedAt = 0;

class Flyout : public UiWindow {
public:
    std::string id;
    Device last;          // shown if the device disappears while the popup is open
    bool closing = false;

    void Frame(Ui& ui, const Rect& client) override {
        const Device* d = AppFindDevice(id);
        if (d) last = *d;
        int openId = Ui::Id("flyout_open");
        float open = ui.Anim(openId, closing ? 0.f : 1.f, closing ? 22.f : 13.f);
        if (closing && open <= 0.02f) {
            ui.Defer([this]() { DestroyWindow(hwnd); });
        }
        ui.alpha = open;
        ui.PushOffset(0, (1.f - open) * 14.f);
        DevicePanel(ui, this, Rect(18, 18, client.w - 36, client.h - 36), last, true);
        ui.PopOffset();
        ui.alpha = 1.f;
    }

    void OnDeactivate() override { StartClose(); }

    bool everActive = false;

    void Prime() {
        ui.SetAnim(Ui::Id("flyout_open"), 0.f);
        ui.SetAnim(Ui::Id((id + "bar").c_str(), 0), 0.f);
    }

    void StartClose() {
        if (closing) return;
        closing = true;
        if (!AppConfig().theme.animations) { DestroyWindow(hwnd); return; }
        Invalidate();
    }

    LRESULT Msg(UINT m, WPARAM wp, LPARAM lp) override {
        if (m == WM_KEYDOWN && wp == VK_ESCAPE && !Editing()) { StartClose(); return 0; }
        if (m == WM_ACTIVATE && LOWORD(wp) != WA_INACTIVE) everActive = true;
        if (m == WM_TIMER && wp == 7) {
            // closes the popup when another window took the focus (no global mouse hook needed)
            if (everActive && !modal_ && GetForegroundWindow() != hwnd) StartClose();
            return 0;
        }
        return UiWindow::Msg(m, wp, lp);
    }

    void OnClosed() override {
        g_lastClosedId = id;
        g_lastClosedAt = GetTickCount();
        if (g_flyout == this) g_flyout = nullptr;
    }
};

} // namespace

bool FlyoutIsOpenFor(const std::string& deviceId) { return g_flyout && g_flyout->id == deviceId; }

void FlyoutClose() {
    if (g_flyout) DestroyWindow(g_flyout->hwnd);
}

void FlyoutShow(const std::string& deviceId, const RECT& anchor) {
    // a click on the same pictogram that just closed the popup should not reopen it
    if (g_lastClosedId == deviceId && GetTickCount() - g_lastClosedAt < 350) return;
    if (g_flyout) {
        bool same = g_flyout->id == deviceId;
        DestroyWindow(g_flyout->hwnd);
        if (same) return;
    }
    const Device* d = AppFindDevice(deviceId);
    if (!d) return;

    POINT pt{(anchor.left + anchor.right) / 2, anchor.top};
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    UINT dpi = GetDpiForSystem();
    float s = dpi / 96.f;
    int w = (int)(356 * s), h = (int)((DevicePanelHeight(*d) + 36) * s);
    int x = pt.x - w / 2;
    RECT wa = mi.rcWork;
    if (x + w > wa.right - (int)(12 * s)) x = wa.right - w - (int)(12 * s);
    if (x < wa.left + (int)(12 * s)) x = wa.left + (int)(12 * s);
    int y = anchor.top - h - (int)(12 * s);
    if (anchor.top < (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2) y = anchor.bottom + (int)(12 * s);   // taskbar on top
    if (y < wa.top) y = wa.top + (int)(8 * s);

    auto* f = new Flyout();
    f->id = deviceId;
    f->last = *d;
    f->Prime();
    RECT rc{x, y, x + w, y + h};
    if (!f->Create(L"BatteryTrayFlyout", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"BatteryTray", rc, nullptr, true)) {
        delete f;
        return;
    }
    g_flyout = f;
    ShowWindow(f->hwnd, SW_SHOW);
    // make sure the popup gets keyboard focus even though the click came from the taskbar
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, me = GetCurrentThreadId();
    if (fgThread && fgThread != me) AttachThreadInput(me, fgThread, TRUE);
    SetForegroundWindow(f->hwnd);
    SetFocus(f->hwnd);
    if (fgThread && fgThread != me) AttachThreadInput(me, fgThread, FALSE);
    SetTimer(f->hwnd, 7, 150, nullptr);
}
