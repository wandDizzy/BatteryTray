// The widget is a per-pixel-alpha layered window made a child of the taskbar (Shell_TrayWnd)
// and placed just left of the notification area. If that fails it falls back to a topmost popup.
#include "widget.h"
#include "theme.h"
#include "layered.h"
#include <commctrl.h>
#include <cmath>
#include <cstring>

namespace {
using namespace lay;

const wchar_t* kCls = L"BatteryTrayWidget";
HWND g_owner, g_w, g_tip;
bool g_child = false;
std::vector<WidgetItem> g_items;
std::vector<RECT> g_rects;
std::vector<float> g_hoverAnim;
int g_hover = -1, g_press = -1;
bool g_tracking = false;
bool g_hoverOn = true, g_animOn = true;
int g_toolCount = 0;
int g_width = 0, g_height = 0;
DWORD g_lastFrame = 0;

HWND Taskbar() { return FindWindowW(L"Shell_TrayWnd", nullptr); }

void UpdateTools() {
    if (!g_tip) return;
    TTTOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = g_w;
    for (int i = 0; i < g_toolCount; i++) {
        ti.uId = (UINT_PTR)i;
        SendMessageW(g_tip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    }
    g_toolCount = 0;
    for (size_t i = 0; i < g_rects.size() && i < g_items.size(); i++) {
        TTTOOLINFOW t{};
        t.cbSize = sizeof(t);
        t.uFlags = TTF_SUBCLASS;
        t.hwnd = g_w;
        t.uId = (UINT_PTR)i;
        t.rect = g_rects[i];
        t.lpszText = (LPWSTR)g_items[i].tip.c_str();
        SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&t);
        g_toolCount++;
    }
}

void Place() {
    if (!g_w || !g_width) return;
    HWND tb = Taskbar();
    if (!tb) return;
    RECT tbr;
    GetWindowRect(tb, &tbr);
    UINT dpi = GetDpiForWindow(tb);
    if (!dpi) dpi = 96;
    RECT nr = tbr;
    HWND notify = FindWindowExW(tb, nullptr, L"TrayNotifyWnd", nullptr);
    if (notify) GetWindowRect(notify, &nr);
    else nr.left = tbr.right - MulDiv(260, dpi, 96);
    int x = nr.left - g_width - MulDiv(4, dpi, 96);
    int y = tbr.top + ((tbr.bottom - tbr.top) - g_height) / 2;
    if (g_child) {
        POINT pt{x, y};
        ScreenToClient(tb, &pt);
        SetWindowPos(g_w, HWND_TOP, pt.x, pt.y, g_width, g_height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        SetWindowPos(g_w, HWND_TOPMOST, x, y, g_width, g_height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

bool Render() {   // returns true while hover animations are still running
    if (!g_w) return false;
    HWND tb = Taskbar();
    UINT dpi = tb ? GetDpiForWindow(tb) : 96;
    if (!dpi) dpi = 96;
    RECT tbr{0, 0, 0, MulDiv(48, dpi, 96)};
    if (tb) GetWindowRect(tb, &tbr);
    int H = tbr.bottom - tbr.top;
    if (H > MulDiv(80, dpi, 96) || H < MulDiv(24, dpi, 96)) H = MulDiv(48, dpi, 96);
    auto S = [&](int v) { return MulDiv(v, dpi, 96); };

    // animation step
    DWORD now = GetTickCount();
    float dt = g_lastFrame ? (now - g_lastFrame) / 1000.f : 0.016f;
    if (dt > 0.05f) dt = 0.05f;
    g_lastFrame = now;
    g_hoverAnim.resize(g_items.size(), 0.f);
    bool moving = false;
    for (size_t i = 0; i < g_items.size(); i++) {
        float target = (g_hoverOn && (int)i == g_hover) ? ((int)i == g_press ? 0.7f : 1.f) : 0.f;
        float& v = g_hoverAnim[i];
        if (!g_animOn) v = target;
        else {
            v += (target - v) * (1.f - expf(-18.f * dt));
            if (fabsf(target - v) < 0.01f) v = target; else moving = true;
        }
    }

    HFONT fIcon = IconFont(S(16));
    HFONT fBolt = IconFont(S(10));
    HFONT fText = TextFont(S(13));
    bool light = TaskbarLightMode();
    COLORREF fg = light ? RGB(25, 25, 25) : RGB(245, 245, 245);

    std::vector<WidgetItem> items = g_items;
    if (items.empty()) {
        WidgetItem w;
        w.glyph = 0xE83F;
        w.text = L"–";
        w.textColor = fg;
        items.push_back(w);
    }

    Painter measure(1, 1);
    const int pad = S(7), gap = S(5), sep = S(2);
    std::vector<int> widths;
    int total = 0;
    for (auto& it : items) {
        wchar_t g[2] = {it.glyph, 0};
        int w = pad + measure.Measure(g, fIcon).cx + gap + measure.Measure(it.text.c_str(), fText).cx;
        if (it.charging) w += S(2) + measure.Measure(L"\xE945", fBolt).cx;
        w += pad;
        widths.push_back(w);
        total += w;
    }
    total += sep * ((int)items.size() - 1);

    g_width = total;
    g_height = H;
    Painter p(total, H);
    g_rects.clear();
    int x = 0;
    int hIcon = p.TextH(fIcon), hText = p.TextH(fText), hBolt = p.TextH(fBolt);
    int bh = S(40) < H - S(4) ? S(40) : H - S(4);
    for (size_t i = 0; i < items.size(); i++) {
        const WidgetItem& it = items[i];
        float hv = i < g_hoverAnim.size() ? g_hoverAnim[i] : 0.f;
        if (hv > 0.003f) {
            p.Round(x, (H - bh) / 2, widths[i], bh, S(5), light ? RGB(0, 0, 0) : RGB(255, 255, 255),
                    (light ? 0.06f : 0.085f) * hv);
        }
        float a = it.dim ? 0.6f : 1.f;
        int cx = x + pad;
        wchar_t g[2] = {it.glyph, 0};
        p.Text(g, fIcon, cx, (H - hIcon) / 2, it.customGlyphColor ? it.glyphColor : fg, a);
        cx += p.Measure(g, fIcon).cx + gap;
        p.Text(it.text.c_str(), fText, cx, (H - hText) / 2 - S(1), it.textColor, a);
        cx += p.Measure(it.text.c_str(), fText).cx;
        if (it.charging) p.Text(L"\xE945", fBolt, cx + S(2), (H - hBolt) / 2, it.textColor);
        g_rects.push_back(RECT{x, 0, x + widths[i], H});
        x += widths[i] + sep;
    }
    p.Present(g_w);
    DeleteObject(fIcon);
    DeleteObject(fBolt);
    DeleteObject(fText);
    return moving;
}

void Animate() {
    if (Render()) SetTimer(g_w, 2, 15, nullptr);
    else KillTimer(g_w, 2);
}

int HitItem(int x, int y) {
    POINT pt{x, y};
    for (size_t i = 0; i < g_rects.size(); i++)
        if (PtInRect(&g_rects[i], pt)) return (int)i;
    return -1;
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        int i = HitItem((short)LOWORD(lp), (short)HIWORD(lp));
        if (!g_tracking) {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            g_tracking = true;
        }
        if (i != g_hover) { g_hover = i; Animate(); }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking = false;
        g_hover = -1;
        g_press = -1;
        Animate();
        return 0;
    case WM_LBUTTONDOWN:
        g_press = HitItem((short)LOWORD(lp), (short)HIWORD(lp));
        Animate();
        return 0;
    case WM_LBUTTONUP: {
        int i = HitItem((short)LOWORD(lp), (short)HIWORD(lp));
        if (i >= 0 && i == g_press && i < (int)g_items.size()) PostMessageW(g_owner, WM_WIDGET_CLICK, (WPARAM)i, 0);
        else if (g_items.empty()) PostMessageW(g_owner, WM_WIDGET_MENU, 0, 0);
        g_press = -1;
        Animate();
        return 0;
    }
    case WM_RBUTTONUP:
        PostMessageW(g_owner, WM_WIDGET_MENU, 0, 0);
        return 0;
    case WM_TIMER:
        if (wp == 2) { Animate(); return 0; }
        if (g_child && !IsWindow(GetParent(h))) { PostMessageW(g_owner, WM_WIDGET_REATTACH, 0, 0); return 0; }
        Place();
        return 0;
    case WM_SETTINGCHANGE:
    case WM_DPICHANGED:
        Render();
        Place();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void Create() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kCls;
        RegisterClassW(&wc);
        reg = true;
    }
    g_w = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCls, L"BatteryTray", WS_POPUP, 0, 0, 1, 1,
                          nullptr, nullptr, inst, nullptr);
    g_child = false;
    HWND tb = Taskbar();
    if (tb) {
        SetWindowLongPtrW(g_w, GWL_STYLE, WS_CHILD | WS_CLIPSIBLINGS);
        if (SetParent(g_w, tb) && GetParent(g_w) == tb) g_child = true;
        else SetWindowLongPtrW(g_w, GWL_STYLE, WS_POPUP);
    }
    if (!g_child) SetWindowLongPtrW(g_w, GWL_EXSTYLE, GetWindowLongPtrW(g_w, GWL_EXSTYLE) | WS_EX_TOPMOST);
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, g_w, nullptr, inst, nullptr);
    SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 400);
    g_toolCount = 0;
    SetTimer(g_w, 1, 2000, nullptr);
}

} // namespace

void WidgetUpdate(HWND owner, const std::vector<WidgetItem>& items, bool hoverHighlight, bool animations) {
    g_owner = owner;
    g_items = items;
    g_hoverOn = hoverHighlight;
    g_animOn = animations;
    if (!g_w) Create();
    Animate();
    UpdateTools();
    Place();
}

void WidgetDestroy() {
    if (g_tip) { DestroyWindow(g_tip); g_tip = nullptr; }
    if (g_w) { DestroyWindow(g_w); g_w = nullptr; }
    g_toolCount = 0;
    g_hover = g_press = -1;
    g_hoverAnim.clear();
}

void WidgetReattach() {
    if (!g_w) return;
    WidgetDestroy();
    Create();
    Animate();
    UpdateTools();
    Place();
}

bool WidgetItemRect(const std::string& id, RECT* screen) {
    if (!g_w) return false;
    for (size_t i = 0; i < g_items.size() && i < g_rects.size(); i++) {
        if (g_items[i].id != id) continue;
        RECT r = g_rects[i];
        MapWindowPoints(g_w, nullptr, (POINT*)&r, 2);
        *screen = r;
        return true;
    }
    return false;
}

std::string WidgetItemId(int index) {
    return index >= 0 && index < (int)g_items.size() ? g_items[index].id : std::string();
}
