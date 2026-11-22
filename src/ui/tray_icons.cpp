// Tray icon rendering: shapes and text are drawn as masks at 4x and downsampled,
// which gives clean anti-aliased edges with a real alpha channel.
#include "tray_icons.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <cwchar>

namespace {
const int SS = 4;   // supersampling factor

struct Canvas {
    int s;          // size in final pixels
    int S;          // supersampled size
    HDC dc = nullptr;
    HBITMAP bmp = nullptr, old = nullptr;
    unsigned* px = nullptr;
    explicit Canvas(int size) : s(size), S(size * SS) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = S;
        bi.bmiHeader.biHeight = -S;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        HDC screen = GetDC(nullptr);
        dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&px, nullptr, 0);
        old = (HBITMAP)SelectObject(dc, bmp);
    }
    ~Canvas() { SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc); }
    void Clear() { memset(px, 0, (size_t)S * S * 4); }
    std::vector<float> Mask() {
        GdiFlush();
        std::vector<float> m((size_t)s * s, 0.f);
        for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++) {
                unsigned sum = 0;
                for (int j = 0; j < SS; j++)
                    for (int i = 0; i < SS; i++) sum += (px[(y * SS + j) * S + x * SS + i] >> 16) & 0xFF;
                m[(size_t)y * s + x] = sum / (255.f * SS * SS);
            }
        return m;
    }
    void FillRect(double x0, double y0, double x1, double y1) {
        RECT r{(LONG)lround(x0 * S), (LONG)lround(y0 * S), (LONG)lround(x1 * S), (LONG)lround(y1 * S)};
        HBRUSH b = CreateSolidBrush(RGB(255, 255, 255));
        ::FillRect(dc, &r, b);
        DeleteObject(b);
    }
    void FillRound(double x0, double y0, double x1, double y1, double rad) {
        HBRUSH b = CreateSolidBrush(RGB(255, 255, 255));
        HPEN p = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
        int rr = (int)lround(rad * S * 2);
        RoundRect(dc, (int)lround(x0 * S), (int)lround(y0 * S), (int)lround(x1 * S) + 1, (int)lround(y1 * S) + 1, rr, rr);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(b); DeleteObject(p);
    }
    void Text(const wchar_t* t, double maxW, double maxH) {
        int h = (int)(S * maxH * 1.45);
        HFONT f = nullptr;
        SIZE ext{};
        TEXTMETRICW tm{};
        int digitH = 0;
        for (; h > 4; h -= 2) {
            if (f) DeleteObject(f);
            f = CreateFontW(-h, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                            ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            SelectObject(dc, f);
            GetTextExtentPoint32W(dc, t, (int)wcslen(t), &ext);
            GetTextMetricsW(dc, &tm);
            digitH = tm.tmAscent - tm.tmInternalLeading;
            if (ext.cx <= S * maxW && digitH <= S * maxH) break;
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        int x = (S - ext.cx) / 2;
        int y = (S - digitH) / 2 - tm.tmInternalLeading;
        TextOutW(dc, x, y, t, (int)wcslen(t));
        DeleteObject(f);
    }
};

struct Rgba { float r, g, b, a; };

void Over(std::vector<Rgba>& img, const std::vector<float>& m, COLORREF c) {
    float kr = GetRValue(c) / 255.f, kg = GetGValue(c) / 255.f, kb = GetBValue(c) / 255.f;
    for (size_t i = 0; i < img.size(); i++) {
        float a = m[i];
        img[i].r = kr * a + img[i].r * (1 - a);
        img[i].g = kg * a + img[i].g * (1 - a);
        img[i].b = kb * a + img[i].b * (1 - a);
        img[i].a = a + img[i].a * (1 - a);
    }
}

HICON ToIcon(const std::vector<Rgba>& img, int s) {
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = s;
    bi.bV5Height = -s;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
    unsigned* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, (BITMAPINFO*)&bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    for (int i = 0; i < s * s; i++) {
        const Rgba& p = img[i];
        float a = p.a > 1 ? 1 : p.a;
        auto un = [&](float v) { return a > 0 ? (unsigned)lround(fminf(v / a, 1.f) * 255) : 0u; };
        bits[i] = ((unsigned)lround(a * 255) << 24) | (un(p.r) << 16) | (un(p.g) << 8) | un(p.b);
    }
    HBITMAP mask = CreateBitmap(s, s, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}
} // namespace

COLORREF LevelColor(const Device& d, const Config& c) {
    // A sleeping/offline device keeps its last known level color; only "unknown" is grey.
    if (d.battery < 0) return c.colOffline;
    if (d.online && d.charging) return c.colCharging;
    if (d.battery <= c.lowThreshold) return c.colLow;
    if (d.battery <= c.midThreshold) return c.colMid;
    return c.colHigh;
}

int TrayIconSize() {
    int s = GetSystemMetrics(SM_CXSMICON);
    return s < 16 ? 16 : s;
}

HICON RenderDeviceIcon(const Device& d, const Config& c, int s) {
    Canvas cv(s);
    std::vector<Rgba> img((size_t)s * s, Rgba{0, 0, 0, 0});
    COLORREF col = LevelColor(d, c);
    wchar_t num[8];
    if (d.battery < 0) wcscpy(num, L"?");
    else swprintf(num, 8, L"%d", d.battery);

    switch (c.iconStyle) {
    case STYLE_BADGE:
        cv.Clear(); cv.FillRound(0, 0, 1, 1, 0.22); Over(img, cv.Mask(), col);
        cv.Clear(); cv.Text(num, 0.92, 0.62); Over(img, cv.Mask(), c.colText);
        break;
    case STYLE_BATTERY: {
        const double x0 = 0.20, x1 = 0.80, y0 = 0.14, y1 = 0.98, w = 0.09;
        cv.Clear();
        cv.FillRect(0.37, 0.02, 0.63, y0);
        cv.FillRect(x0, y0, x1, y0 + w);
        cv.FillRect(x0, y1 - w, x1, y1);
        cv.FillRect(x0, y0, x0 + w, y1);
        cv.FillRect(x1 - w, y0, x1, y1);
        if (d.battery > 0) {
            double inTop = y0 + w + 0.04, inBot = y1 - w - 0.04;
            double top = inBot - (inBot - inTop) * d.battery / 100.0;
            cv.FillRect(x0 + w + 0.04, top, x1 - w - 0.04, inBot);
        }
        Over(img, cv.Mask(), col);
        break;
    }
    default:
        cv.Clear(); cv.Text(num, 1.0, 0.80); Over(img, cv.Mask(), col);
        break;
    }
    return ToIcon(img, s);
}

HICON RenderAppIcon(int s) {
    Device d;
    d.battery = 60;
    d.online = true;
    Config c;
    c.iconStyle = STYLE_BATTERY;
    c.colHigh = RGB(200, 200, 200);
    return RenderDeviceIcon(d, c, s);
}
