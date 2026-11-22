#pragma once
// Helpers for per-pixel-alpha layered windows drawn with GDI (taskbar widget, game overlay).
// GDI draws white shapes/text on black into a DIB; the brightness becomes coverage for
// compositing a colored layer, which gives anti-aliased output with a real alpha channel.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <vector>

namespace lay {

inline HFONT MakeFont(const wchar_t* face, int px, int weight, bool* ok = nullptr) {
    HFONT f = CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    if (ok) {
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(dc, f);
        wchar_t got[LF_FACESIZE] = L"";
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, o);
        DeleteDC(dc);
        *ok = _wcsicmp(got, face) == 0;
    }
    return f;
}

inline HFONT IconFont(int px) {
    bool ok = false;
    HFONT f = MakeFont(L"Segoe Fluent Icons", px, FW_NORMAL, &ok);
    if (ok) return f;
    DeleteObject(f);
    return MakeFont(L"Segoe MDL2 Assets", px, FW_NORMAL);
}

inline HFONT TextFont(int px) {
    bool ok = false;
    HFONT f = MakeFont(L"Segoe UI Variable Text", px, FW_SEMIBOLD, &ok);
    if (ok) return f;
    DeleteObject(f);
    return MakeFont(L"Segoe UI", px, FW_SEMIBOLD);
}

struct Px { float r, g, b, a; };

// GDI draws white shapes/text on black into a DIB; the brightness becomes coverage
// for compositing a colored layer -> anti-aliased output with a real alpha channel.
struct Painter {
    int w, h;
    std::vector<Px> img;
    HDC dc;
    HBITMAP bmp, old;
    unsigned* bits = nullptr;
    Painter(int w_, int h_) : w(w_), h(h_), img((size_t)w_ * h_, Px{0, 0, 0, 1.f / 255}) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        dc = CreateCompatibleDC(nullptr);
        bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
        old = (HBITMAP)SelectObject(dc, bmp);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
    }
    ~Painter() { SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc); }

    SIZE Measure(const wchar_t* s, HFONT f) {
        SelectObject(dc, f);
        SIZE sz{};
        GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
        return sz;
    }
    int TextH(HFONT f) {
        SelectObject(dc, f);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        return tm.tmHeight;
    }
    void Composite(COLORREF c, float alpha) {
        GdiFlush();
        float cr = GetRValue(c) / 255.f, cg = GetGValue(c) / 255.f, cb = GetBValue(c) / 255.f;
        for (size_t i = 0; i < img.size(); i++) {
            unsigned p = bits[i];
            unsigned m = (p >> 16) & 0xFF, g2 = (p >> 8) & 0xFF, b2 = p & 0xFF;
            if (g2 > m) m = g2;
            if (b2 > m) m = b2;
            if (!m) continue;
            float a = m / 255.f * alpha;
            Px& d = img[i];
            d.r = cr * a + d.r * (1 - a);
            d.g = cg * a + d.g * (1 - a);
            d.b = cb * a + d.b * (1 - a);
            d.a = a + d.a * (1 - a);
        }
    }
    void Text(const wchar_t* s, HFONT f, int x, int y, COLORREF c, float alpha = 1.f) {
        memset(bits, 0, (size_t)w * h * 4);
        SelectObject(dc, f);
        TextOutW(dc, x, y, s, (int)wcslen(s));
        Composite(c, alpha);
    }
    // thick straight line made of small round dots (for the "disconnected" slash)
    void Slash(int x0, int y0, int x1, int y1, int th, COLORREF c, float alpha) {
        int dx = x1 - x0, dy = y1 - y0;
        int n = (std::max)(std::abs(dx), std::abs(dy));
        for (int i = 0; i <= n; i++) {
            int x = x0 + dx * i / (n ? n : 1), y = y0 + dy * i / (n ? n : 1);
            Round(x - th / 2, y - th / 2, th, th, th / 2, c, alpha);
        }
    }

    void Round(int x, int y, int rw, int rh, int rad, COLORREF c, float alpha) {
        if (alpha <= 0.003f) return;
        // supersample the rounded rectangle 4x for smooth corners
        const int S = 4;
        std::vector<unsigned char> m((size_t)rw * rh, 0);
        for (int j = 0; j < rh; j++)
            for (int i = 0; i < rw; i++) {
                int cov = 0;
                for (int sj = 0; sj < S; sj++)
                    for (int si = 0; si < S; si++) {
                        float px = i + (si + 0.5f) / S, py = j + (sj + 0.5f) / S;
                        float dx = 0, dy = 0;
                        if (px < rad) dx = rad - px; else if (px > rw - rad) dx = px - (rw - rad);
                        if (py < rad) dy = rad - py; else if (py > rh - rad) dy = py - (rh - rad);
                        if (dx * dx + dy * dy <= (float)rad * rad) cov++;
                    }
                m[(size_t)j * rw + i] = (unsigned char)(cov * 255 / (S * S));
            }
        float cr = GetRValue(c) / 255.f, cg = GetGValue(c) / 255.f, cb = GetBValue(c) / 255.f;
        for (int j = 0; j < rh; j++)
            for (int i = 0; i < rw; i++) {
                int X = x + i, Y = y + j;
                if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
                float a = m[(size_t)j * rw + i] / 255.f * alpha;
                if (a <= 0) continue;
                Px& d = img[(size_t)Y * w + X];
                d.r = cr * a + d.r * (1 - a);
                d.g = cg * a + d.g * (1 - a);
                d.b = cb * a + d.b * (1 - a);
                d.a = a + d.a * (1 - a);
            }
    }
    void Present(HWND wnd, BYTE constAlpha = 255, POINT* pos = nullptr) {
        for (size_t i = 0; i < img.size(); i++) {
            const Px& p = img[i];
            auto c = [](float v) { return (unsigned)lround(fminf(fmaxf(v, 0.f), 1.f) * 255); };
            bits[i] = (c(p.a) << 24) | (c(p.r) << 16) | (c(p.g) << 8) | c(p.b);
        }
        SIZE sz{w, h};
        POINT src{0, 0};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, constAlpha, AC_SRC_ALPHA};
        HDC screen = GetDC(nullptr);
        UpdateLayeredWindow(wnd, screen, pos, &sz, dc, &src, 0, &bf, ULW_ALPHA);
        ReleaseDC(nullptr, screen);
    }
};


} // namespace lay
