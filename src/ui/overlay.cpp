#include "overlay.h"
#include "layered.h"
#include "../i18n/i18n.h"
#include <cmath>

using namespace lay;

namespace {

const wchar_t* kCls = L"BatteryTrayOverlay";
HWND g_w;
std::vector<OverlayItem> g_items;
Config g_cfg;
bool g_userVisible = true;     // toggled by the hotkey
float g_alpha = 0.f, g_target = 0.f;
DWORD g_last = 0;

bool WantVisible() {
    if (!g_cfg.overlay.enabled) return false;
    if (g_cfg.overlay.mode == "hotkey") return g_userVisible;
    if (!g_userVisible) return false;
    if (g_cfg.overlay.mode == "low") {
        for (auto& it : g_items) if (it.low) return true;
        return false;
    }
    return !g_items.empty();
}

void Render() {
    if (!g_w) return;
    // which devices to list
    std::vector<OverlayItem> items;
    for (auto& it : g_items)
        if (g_cfg.overlay.mode != "low" || it.low) items.push_back(it);
    if (items.empty()) items = g_items;
    if (items.empty()) return;

    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    UINT dpi = GetDpiForSystem();
    float k = dpi / 96.f * g_cfg.overlay.scale / 100.f;
    auto S = [&](float v) { return (int)lroundf(v * k); };

    HFONT fIcon = IconFont(S(17));
    HFONT fBolt = IconFont(S(10));
    HFONT fName = TextFont(S(13));
    HFONT fPct = TextFont(S(14));
    Painter m(1, 1);

    const int pad = S(12), rowH = S(30), gap = S(10);
    int nameW = 0;
    if (g_cfg.overlay.showNames)
        for (auto& it : items) nameW = (std::max)(nameW, (int)m.Measure(it.name.c_str(), fName).cx);
    nameW = (std::min)(nameW, S(200));
    int pctW = m.Measure(L"100%", fPct).cx + S(14);
    int iconW = m.Measure(L"\xE962", fIcon).cx;
    int W = pad + iconW + gap + (nameW ? nameW + gap : 0) + pctW + pad;
    int H = pad + rowH * (int)items.size() + pad - S(4);

    Painter p(W, H);
    for (auto& px : p.img) px = Px{0, 0, 0, 0};
    float op = g_cfg.overlay.opacity / 100.f;
    p.Round(0, 0, W, H, S(10), RGB(20, 20, 24), op);

    int hIcon = p.TextH(fIcon), hName = p.TextH(fName), hPct = p.TextH(fPct), hBolt = p.TextH(fBolt);
    for (size_t i = 0; i < items.size(); i++) {
        const OverlayItem& it = items[i];
        int y = pad + rowH * (int)i;
        float a = it.dim ? 0.6f : 1.f;
        int x = pad;
        wchar_t g[2] = {it.glyph, 0};
        int iy = y + (rowH - S(6) - hIcon) / 2;
        p.Text(g, fIcon, x, iy, it.offline ? RGB(150, 150, 156) : it.customGlyphColor ? it.glyphColor : RGB(240, 240, 240), a);
        if (it.strike) {
            p.Slash(x + S(1), iy + hIcon - S(2), x + iconW - S(1), iy + S(2), S(2), RGB(232, 72, 77), 1.f);
        } else if (it.offline) {
            int r = S(3), bx = x + iconW - r - S(1), by = iy + hIcon - r * 2 - S(2);
            p.Round(bx, by, r * 2, r * 2, r, RGB(232, 72, 77), 1.f);
        }
        x += iconW + gap;
        if (nameW) {
            // clip long names
            std::wstring n = it.name;
            while (n.size() > 3 && m.Measure(n.c_str(), fName).cx > nameW) n = n.substr(0, n.size() - 2) + L"…";
            p.Text(n.c_str(), fName, x, y + (rowH - S(6) - hName) / 2, RGB(225, 225, 230), a);
            x += nameW + gap;
        }
        std::wstring pct = it.strike ? std::wstring(T("off_short")) : it.battery >= 0 ? std::to_wstring(it.battery) + L"%" : L"?";
        int tw = m.Measure(pct.c_str(), fPct).cx;
        int px = W - pad - tw;
        p.Text(pct.c_str(), fPct, px, y + (rowH - S(6) - hPct) / 2, it.color, a);
        if (it.charging) p.Text(L"\xE945", fBolt, px - S(13), y + (rowH - S(6) - hBolt) / 2, it.color);
        // level bar under the row
        int bx = pad + iconW + gap, bw = W - pad - bx, by = y + rowH - S(7);
        p.Round(bx, by, bw, S(3), S(1), RGB(255, 255, 255), 0.12f);
        if (it.battery > 0 && !it.strike) p.Round(bx, by, (std::max)(S(3), bw * it.battery / 100), S(3), S(1), it.color, 0.9f * a);
    }
    DeleteObject(fIcon);
    DeleteObject(fBolt);
    DeleteObject(fName);
    DeleteObject(fPct);

    RECT mr = mi.rcMonitor;
    int margin = S(18);
    const std::string& c = g_cfg.overlay.corner;
    POINT pos;
    pos.x = (c == "top-left" || c == "bottom-left") ? mr.left + margin : mr.right - W - margin;
    pos.y = (c == "top-left" || c == "top-right") ? mr.top + margin : mr.bottom - H - margin - S(48);
    p.Present(g_w, (BYTE)lroundf(g_alpha * 255), &pos);
    SetWindowPos(g_w, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Step() {
    DWORD now = GetTickCount();
    float dt = g_last ? (now - g_last) / 1000.f : 0.016f;
    if (dt > 0.05f) dt = 0.05f;
    g_last = now;
    if (!g_cfg.theme.animations) g_alpha = g_target;
    else {
        g_alpha += (g_target - g_alpha) * (1.f - expf(-12.f * dt));
        if (fabsf(g_target - g_alpha) < 0.01f) g_alpha = g_target;
    }
    if (g_alpha <= 0.001f && g_target == 0.f) {
        ShowWindow(g_w, SW_HIDE);
        KillTimer(g_w, 1);
        return;
    }
    Render();
    if (g_alpha != g_target) SetTimer(g_w, 1, 15, nullptr);
    else KillTimer(g_w, 1);
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER) {
        if (wp == 1) Step();
        if (wp == 2 && g_target > 0) SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, msg, wp, lp);
}

void Create() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = inst;
        wc.lpszClassName = kCls;
        RegisterClassW(&wc);
        reg = true;
    }
    g_w = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                          kCls, L"BatteryTray overlay", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
    SetTimer(g_w, 2, 2000, nullptr);   // games can push themselves above: re-assert topmost
}

void Apply() {
    g_target = WantVisible() ? 1.f : 0.f;
    if (g_target > 0 && !g_w) Create();
    if (!g_w) return;
    Step();
}

} // namespace

void OverlayUpdate(const std::vector<OverlayItem>& items, const Config& c) {
    bool modeChanged = c.overlay.mode != g_cfg.overlay.mode || c.overlay.enabled != g_cfg.overlay.enabled;
    g_items = items;
    g_cfg = c;
    if (modeChanged) g_userVisible = c.overlay.mode != "hotkey";
    if (!c.overlay.enabled && !g_w) return;
    Apply();
}

void OverlayToggle() {
    if (!g_cfg.overlay.enabled) return;
    g_userVisible = !g_userVisible;
    Apply();
}

void OverlayDestroy() {
    if (g_w) DestroyWindow(g_w);
    g_w = nullptr;
    g_alpha = g_target = 0;
}
