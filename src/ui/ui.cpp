#include "ui.h"
#include "../app.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <windowsx.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#define BT_DWMWA_WINDOW_CORNER_PREFERENCE 33
#define BT_DWMWA_SYSTEMBACKDROP_TYPE 38
#define WM_UI_DEFERRED (WM_APP + 50)
#define WM_UI_ENDEDIT (WM_APP + 51)

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// ===================================================================== factories

namespace gfx {
static ID2D1Factory* g_d2d;
static IDWriteFactory* g_dw;

ID2D1Factory* D2D() {
    if (!g_d2d) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, (void**)&g_d2d);
    return g_d2d;
}
IDWriteFactory* DWrite() {
    if (!g_dw) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw);
    return g_dw;
}
static bool HasFamily(const wchar_t* name) {
    IDWriteFontCollection* col = nullptr;
    if (FAILED(DWrite()->GetSystemFontCollection(&col, FALSE)) || !col) return false;
    UINT32 idx = 0;
    BOOL exists = FALSE;
    col->FindFamilyName(name, &idx, &exists);
    col->Release();
    return exists != FALSE;
}
const wchar_t* IconFont() {
    static const wchar_t* f = HasFamily(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    return f;
}
const wchar_t* TextFont() {
    static const wchar_t* f = HasFamily(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    return f;
}
} // namespace gfx

static D2D1_COLOR_F DC(Color c, float alpha) { return D2D1::ColorF(c.r, c.g, c.b, c.a * alpha); }
static D2D1_RECT_F DR(const Rect& r) { return D2D1::RectF(r.x, r.y, r.r(), r.b()); }

// ===================================================================== Ui

Ui::~Ui() {
    for (auto& kv : formats_) kv.second->Release();
}

int Ui::Id(const char* s, int i) {
    unsigned h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    h = (h ^ (unsigned)i) * 16777619u;
    return (int)(h & 0x7FFFFFFF) | 1;
}

float Ui::Anim(int id, float target, float speed) {
    auto it = anims_.find(id);
    if (it == anims_.end()) { anims_[id] = target; return target; }
    float& v = it->second;
    if (!animationsOn) { v = target; return v; }
    float k = 1.f - expf(-speed * dt);
    v += (target - v) * k;
    if (fabsf(target - v) < 0.002f * (fabsf(target) > 1.f ? fabsf(target) : 1.f)) v = target;
    else animating = true;
    return v;
}

void Ui::SetAnim(int id, float value) { anims_[id] = value; }

void Ui::BeginFrame() {
    hot = 0;
    animating = false;
    offX_ = offY_ = 0;
    offStack_.clear();
}

void Ui::EndFrame() {
    if (released) active = 0;
    pressed = released = false;
    wheel = 0;
}

bool Ui::Interact(int id, const Rect& r) {
    bool over = Over(r);
    if (over) hot = id;
    if (over && pressed) active = id;
    return released && active == id && over;
}

void Ui::SetColor(Color c) { brush->SetColor(DC(c, alpha)); }

void Ui::Fill(const Rect& r, Color c, float radius) {
    if (c.a * alpha <= 0.001f) return;
    SetColor(c);
    if (radius > 0) rt->FillRoundedRectangle(D2D1::RoundedRect(DR(r), radius, radius), brush);
    else rt->FillRectangle(DR(r), brush);
}

void Ui::Stroke(const Rect& r, Color c, float radius, float width) {
    if (c.a * alpha <= 0.001f) return;
    SetColor(c);
    Rect q = r.Inset(width / 2);
    if (radius > 0) rt->DrawRoundedRectangle(D2D1::RoundedRect(DR(q), radius, radius), brush, width);
    else rt->DrawRectangle(DR(q), brush, width);
}

void Ui::Circle(float cx, float cy, float rad, Color c) {
    SetColor(c);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), brush);
}

void Ui::CircleStroke(float cx, float cy, float rad, Color c, float width) {
    SetColor(c);
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), brush, width);
}

void Ui::Line(float x1, float y1, float x2, float y2, Color c, float width) {
    SetColor(c);
    rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, width);
}

void Ui::Gradient(const Rect& r, Color a, Color b, float radius, bool horizontal) {
    D2D1_GRADIENT_STOP stops[2] = {{0.f, DC(a, alpha)}, {1.f, DC(b, alpha)}};
    ID2D1GradientStopCollection* col = nullptr;
    if (FAILED(rt->CreateGradientStopCollection(stops, 2, &col))) return;
    ID2D1LinearGradientBrush* g = nullptr;
    D2D1_POINT_2F p0 = D2D1::Point2F(r.x, r.y);
    D2D1_POINT_2F p1 = horizontal ? D2D1::Point2F(r.r(), r.y) : D2D1::Point2F(r.x, r.b());
    if (SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(p0, p1), col, &g))) {
        if (radius > 0) rt->FillRoundedRectangle(D2D1::RoundedRect(DR(r), radius, radius), g);
        else rt->FillRectangle(DR(r), g);
        g->Release();
    }
    col->Release();
}

void Ui::Shadow(const Rect& r, float radius, float spread, float strength) {
    for (int i = 1; i <= 4; i++) {
        float d = spread * i / 4.f;
        Fill(Rect(r.x - d, r.y - d + d * 0.5f, r.w + 2 * d, r.h + 2 * d), WithA(th.shadow, th.shadow.a * strength / 6.f),
             radius + d);
    }
}

IDWriteTextFormat* Ui::Format(float size, int weight, bool icon) {
    long long key = ((long long)(size * 100) << 20) | ((long long)weight << 1) | (icon ? 1 : 0);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second;
    IDWriteTextFormat* f = nullptr;
    gfx::DWrite()->CreateTextFormat(icon ? gfx::IconFont() : gfx::TextFont(), nullptr, (DWRITE_FONT_WEIGHT)weight,
                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
    if (f && !icon) {
        IDWriteInlineObject* sign = nullptr;
        gfx::DWrite()->CreateEllipsisTrimmingSign(f, &sign);
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        f->SetTrimming(&trim, sign);
        if (sign) sign->Release();
    }
    formats_[key] = f;
    return f;
}

void Ui::Text(const std::wstring& s, const Rect& r, float size, Color c, int weight, int align, bool wrap) {
    if (s.empty()) return;
    IDWriteTextFormat* f = Format(size, weight, false);
    if (!f) return;
    f->SetTextAlignment(align == ALIGN_CENTER ? DWRITE_TEXT_ALIGNMENT_CENTER
                        : align == ALIGN_RIGHT ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                               : DWRITE_TEXT_ALIGNMENT_LEADING);
    f->SetParagraphAlignment(wrap ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    SetColor(c);
    rt->DrawText(s.c_str(), (UINT32)s.size(), f, DR(r), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Ui::Measure(const std::wstring& s, float size, int weight) {
    IDWriteTextFormat* f = Format(size, weight, false);
    if (!f || s.empty()) return 0;
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    IDWriteTextLayout* l = nullptr;
    if (FAILED(gfx::DWrite()->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 10000, 1000, &l))) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    l->Release();
    return m.widthIncludingTrailingWhitespace;
}

void Ui::Icon(wchar_t g, const Rect& r, float size, Color c) {
    IDWriteTextFormat* f = Format(size, 400, true);
    if (!f) return;
    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    SetColor(c);
    wchar_t s[2] = {g, 0};
    rt->DrawText(s, 1, f, DR(r), brush);
}

// ----------------------------------------------------------------- controls

bool Ui::Button(int id, const Rect& r, const std::wstring& label, bool accent, wchar_t glyph) {
    bool click = Interact(id, r);
    float h = Anim(id, hot == id ? 1.f : 0.f, 18.f);
    bool press = active == id && down && hot == id;
    Color fill, txt;
    if (accent) {
        fill = press ? WithA(th.accent, 0.8f) : Mix(th.accent, th.accentHover, h);
        txt = th.onAccent;
    } else {
        fill = press ? th.ctrlPress : Mix(th.ctrl, th.ctrlHover, h);
        txt = press ? th.text2 : th.text;
    }
    Fill(r, fill, 6);
    if (!accent) Stroke(r, th.ctrlStroke, 6);
    float tw = Measure(label, 13.f, 400);
    float gw = glyph ? 16.f + (label.empty() ? 0 : 8.f) : 0;
    float x = r.cx() - (tw + gw) / 2;
    if (glyph) Icon(glyph, Rect(x, r.y, 16, r.h), 14.f, txt);
    Text(label, Rect(x + gw, r.y, tw + 2, r.h), 13.f, txt, 400, ALIGN_LEFT);
    return click;
}

bool Ui::IconButton(int id, const Rect& r, wchar_t glyph, bool selected, float glyphSize) {
    bool click = Interact(id, r);
    float h = Anim(id, hot == id ? 1.f : 0.f, 18.f);
    float s = Anim(id + 1, selected ? 1.f : 0.f, 16.f);
    Color base = Mix(th.ctrl, th.ctrlHover, h);
    Fill(r, Mix(base, WithA(th.accent, 0.22f), s), 6);
    Stroke(r, Mix(th.ctrlStroke, th.accent, s), 6, 1.f + s * 0.5f);
    if (glyph) Icon(glyph, r, glyphSize, Mix(th.text, th.accent, s));
    return click;
}

bool Ui::Toggle(int id, const Rect& r, bool& v) {
    bool click = Interact(id, r);
    if (click) v = !v;
    float on = Anim(id, v ? 1.f : 0.f, 16.f);
    float h = Anim(id + 1, hot == id ? 1.f : 0.f, 18.f);
    Rect tr(r.x, r.cy() - 10, 40, 20);
    Fill(tr, WithA(th.accent, on), 10);
    Stroke(tr, WithA(th.text2, 1.f - on), 10, 1.f);
    float rad = 5.f + h * 1.5f;
    float kx = tr.x + 10 + on * 20;
    Circle(kx, tr.cy(), rad, Mix(th.text2, th.onAccent, on));
    return click;
}

bool Ui::Slider(int id, const Rect& r, int& v, int lo, int hi) {
    Interact(id, r);
    bool changed = false;
    if (active == id && down) {
        float f = (mx - offX_ - r.x) / r.w;
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        int nv = lo + (int)lroundf(f * (hi - lo));
        if (nv != v) { v = nv; changed = true; }
    }
    if (hot == id && wheel != 0) {
        int nv = v + (wheel > 0 ? 1 : -1);
        nv = nv < lo ? lo : nv > hi ? hi : nv;
        if (nv != v) { v = nv; changed = true; }
    }
    float frac = hi > lo ? (float)(v - lo) / (hi - lo) : 0;
    float fa = Anim(id + 2, frac, 24.f);
    float h = Anim(id + 1, (hot == id || active == id) ? 1.f : 0.f, 18.f);
    float cy = r.cy();
    Fill(Rect(r.x, cy - 2, r.w, 4), th.ctrlStroke, 2);
    Fill(Rect(r.x, cy - 2, r.w * fa, 4), th.accent, 2);
    float tx = r.x + r.w * fa;
    Circle(tx, cy, 10, th.dark ? Rgb(0x454545) : Rgb(0xFFFFFF));
    CircleStroke(tx, cy, 10, th.ctrlStroke, 1);
    Circle(tx, cy, 5.f + h * 2.f - (active == id ? 1.5f : 0), th.accent);
    return changed;
}

bool Ui::Segmented(int id, const Rect& r, const std::vector<std::wstring>& items, int& sel) {
    if (items.empty()) return false;
    bool changed = false;
    float w = r.w / items.size();
    Fill(r, th.ctrl, 7);
    Stroke(r, th.ctrlStroke, 7);
    float px = Anim(id, r.x + w * sel, 20.f);
    Rect pill(px + 3, r.y + 3, w - 6, r.h - 6);
    Fill(pill, th.dark ? Rgb(0xFFFFFF, 0.10f) : Rgb(0xFFFFFF, 1.f), 5);
    if (!th.dark) Stroke(pill, th.ctrlStroke, 5);
    Fill(Rect(pill.cx() - 8, pill.b() - 3, 16, 3), th.accent, 1.5f);
    for (size_t i = 0; i < items.size(); i++) {
        Rect cell(r.x + w * i, r.y, w, r.h);
        int cid = id + 7 + (int)i * 3;
        if (Interact(cid, cell) && (int)i != sel) { sel = (int)i; changed = true; }
        float h = Anim(cid, hot == cid ? 1.f : 0.f, 18.f);
        Color c = (int)i == sel ? th.text : Mix(th.text2, th.text, h);
        Text(items[i], cell.Inset(4, 0), 12.5f, c, (int)i == sel ? 600 : 400, ALIGN_CENTER);
    }
    return changed;
}

bool Ui::Swatch(int id, const Rect& r, Color c, bool selected) {
    bool click = Interact(id, r);
    float h = Anim(id, hot == id ? 1.f : 0.f, 18.f);
    float s = Anim(id + 1, selected ? 1.f : 0.f, 16.f);
    float rad = r.w / 2 - 4 + h * 1.5f - s * 1.5f;
    Circle(r.cx(), r.cy(), rad, c);
    CircleStroke(r.cx(), r.cy(), rad, th.ctrlStroke, 1);
    if (s > 0.01f) CircleStroke(r.cx(), r.cy(), r.w / 2 - 1, WithA(th.text, s), 2.f);
    return click;
}

bool Ui::Card(int id, const Rect& r, bool clickable) {
    bool click = false;
    float h = 0;
    if (clickable) {
        click = Interact(id, r);
        h = Anim(id, hot == id ? 1.f : 0.f, 18.f);
    }
    Fill(r, Mix(th.card, th.cardHover, h), 8);
    Stroke(r, th.cardStroke, 8);
    return click;
}

void Ui::Bar(int id, const Rect& r, float frac, Color c) {
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    float f = Anim(id, frac, 6.f);
    Fill(r, th.ctrl, r.h / 2);
    if (f > 0.002f) {
        Rect fr(r.x, r.y, (std::max)(r.h, r.w * f), r.h);
        Gradient(fr, Mix(c, Rgb(0xFFFFFF), th.dark ? 0.25f : 0.15f), c, r.h / 2);
    }
}

bool Ui::Link(int id, const Rect& r, const std::wstring& s, float size) {
    bool click = Interact(id, r);
    float h = Anim(id, hot == id ? 1.f : 0.f, 18.f);
    Text(s, r, size, Mix(th.accent, th.text, h * 0.3f), 400, ALIGN_LEFT);
    return click;
}

void Ui::PushClip(const Rect& r) { rt->PushAxisAlignedClip(DR(r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE); }
void Ui::PopClip() { rt->PopAxisAlignedClip(); }

void Ui::PushOffset(float dx, float dy) {
    offStack_.push_back({offX_, offY_});
    offX_ += dx;
    offY_ += dy;
    rt->SetTransform(D2D1::Matrix3x2F::Translation(offX_, offY_));
}

void Ui::PopOffset() {
    if (offStack_.empty()) return;
    offX_ = offStack_.back().first;
    offY_ = offStack_.back().second;
    offStack_.pop_back();
    rt->SetTransform(D2D1::Matrix3x2F::Translation(offX_, offY_));
}

// ===================================================================== background picture

namespace {
struct Picture {
    std::wstring key;
    UINT w = 0, h = 0;
    std::vector<uint32_t> px;   // premultiplied BGRA
};
Picture g_pic;

void BoxBlur(std::vector<uint32_t>& img, int w, int h, int r) {
    if (r < 1) return;
    std::vector<uint32_t> tmp(img.size());
    auto pass = [&](const std::vector<uint32_t>& src, std::vector<uint32_t>& dst, bool horiz) {
        int n = horiz ? w : h, lines = horiz ? h : w;
        for (int l = 0; l < lines; l++) {
            auto at = [&](int i) -> uint32_t {
                i = i < 0 ? 0 : i >= n ? n - 1 : i;
                return horiz ? src[(size_t)l * w + i] : src[(size_t)i * w + l];
            };
            int sum[4] = {0, 0, 0, 0};
            for (int i = -r; i <= r; i++) {
                uint32_t p = at(i);
                for (int c = 0; c < 4; c++) sum[c] += (p >> (c * 8)) & 0xFF;
            }
            int div = 2 * r + 1;
            for (int i = 0; i < n; i++) {
                uint32_t o = 0;
                for (int c = 0; c < 4; c++) o |= (uint32_t)(sum[c] / div) << (c * 8);
                if (horiz) dst[(size_t)l * w + i] = o; else dst[(size_t)i * w + l] = o;
                uint32_t pin = at(i + r + 1), pout = at(i - r);
                for (int c = 0; c < 4; c++) sum[c] += (int)((pin >> (c * 8)) & 0xFF) - (int)((pout >> (c * 8)) & 0xFF);
            }
        }
    };
    for (int k = 0; k < 3; k++) {
        pass(img, tmp, true);
        pass(tmp, img, false);
    }
}

bool LoadPicture(const std::wstring& path, int blur) {
    std::wstring key = path + L"|" + std::to_wstring(blur);
    if (g_pic.key == key) return !g_pic.px.empty();
    g_pic = Picture();
    g_pic.key = key;
    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = false;
    if (SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame))) {
        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (w && h) {
            double k = w > 1280 ? 1280.0 / w : 1.0;
            UINT sw = (UINT)(w * k), sh = (UINT)(h * k);
            if (SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
                SUCCEEDED(scaler->Initialize(frame, sw, sh, WICBitmapInterpolationModeFant)) &&
                SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                           WICBitmapPaletteTypeCustom))) {
                g_pic.w = sw;
                g_pic.h = sh;
                g_pic.px.resize((size_t)sw * sh);
                if (SUCCEEDED(conv->CopyPixels(nullptr, sw * 4, sw * sh * 4, (BYTE*)g_pic.px.data()))) {
                    BoxBlur(g_pic.px, (int)sw, (int)sh, blur * (int)sw / 1280 / 2);
                    ok = true;
                } else {
                    g_pic.px.clear();
                }
            }
        }
    }
    SafeRelease(conv);
    SafeRelease(scaler);
    SafeRelease(frame);
    SafeRelease(dec);
    wic->Release();
    return ok;
}
} // namespace

// ===================================================================== UiWindow

static std::vector<UiWindow*> g_windows;

UiWindow::~UiWindow() {
    SafeRelease(bgBitmap_);
    SafeRelease(ui.brush);
    SafeRelease(target_);
}

void UiWindow::RepaintAll() { for (auto* w : g_windows) w->Invalidate(); }
void UiWindow::ApplyThemeAll() {
    for (auto* w : g_windows) { w->ApplyTheme(); w->Invalidate(); }
}
void UiWindow::CloseAll() {
    auto copy = g_windows;
    for (auto* w : copy) DestroyWindow(w->hwnd);
}

bool UiWindow::Create(const wchar_t* cls, DWORD style, DWORD exStyle, const wchar_t* title, RECT pxRect, HWND owner,
                      bool popup) {
    static std::vector<std::wstring> registered;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (std::find(registered.begin(), registered.end(), cls) == registered.end()) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = Proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = cls;
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        wc.hIconSm = wc.hIcon;
        if (popup) wc.style = CS_DROPSHADOW;
        RegisterClassExW(&wc);
        registered.push_back(cls);
    }
    popup_ = popup;
    hwnd = CreateWindowExW(exStyle, cls, title, style, pxRect.left, pxRect.top, pxRect.right - pxRect.left,
                           pxRect.bottom - pxRect.top, owner, nullptr, inst, this);
    if (!hwnd) return false;
    g_windows.push_back(this);
    scale = GetDpiForWindow(hwnd) / 96.f;
    QueryPerformanceCounter(&lastTick_);
    ApplyTheme();
    return true;
}

void UiWindow::Invalidate() {
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

void UiWindow::ApplyTheme() {
    if (!hwnd) return;
    const Config& c = AppConfig();
    BOOL dark = IsDark(c);
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corner = popup_ ? 2 : 0;   // round
    if (popup_) DwmSetWindowAttribute(hwnd, BT_DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    int type = 1;   // none
    if (c.theme.background == "mica") type = popup_ ? 3 : 2;
    else if (c.theme.background == "acrylic") type = 3;
    HRESULT hr = DwmSetWindowAttribute(hwnd, BT_DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
    backdrop_ = SUCCEEDED(hr) && type != 1;
    MARGINS m = backdrop_ ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &m);
}

void UiWindow::CreateTarget() {
    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    gfx::D2D()->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)),
                                       &target_);
    if (!target_) return;
    float dpi = scale * 96.f;
    target_->SetDpi(dpi, dpi);
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    SafeRelease(ui.brush);
    target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &ui.brush);
    SafeRelease(bgBitmap_);
    bgKey_.clear();
}

void UiWindow::DrawBackground(const Rect& client) {
    const Config& c = AppConfig();
    const Theme& th = ui.th;
    if (c.theme.background == "image" && !c.theme.image.empty()) {
        std::wstring key = c.theme.image + L"|" + std::to_wstring(c.theme.imageBlur);
        if (bgKey_ != key) {
            SafeRelease(bgBitmap_);
            bgKey_ = key;
            if (LoadPicture(c.theme.image, c.theme.imageBlur)) {
                D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
                target_->CreateBitmap(D2D1::SizeU(g_pic.w, g_pic.h), g_pic.px.data(), g_pic.w * 4, bp, &bgBitmap_);
            }
        }
        if (bgBitmap_) {
            target_->Clear(DC(th.bg, 1));
            D2D1_SIZE_F s = bgBitmap_->GetSize();
            float k = (std::max)(client.w / s.width, client.h / s.height);
            float w = s.width * k, h = s.height * k;
            target_->DrawBitmap(bgBitmap_, D2D1::RectF(client.cx() - w / 2, client.cy() - h / 2, client.cx() + w / 2,
                                                       client.cy() + h / 2), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            ui.Fill(client, WithA(th.bg, c.theme.imageDim / 100.f));
            return;
        }
    }
    if (backdrop_) {
        target_->Clear(D2D1::ColorF(0, 0, 0, 0));
        if (popup_) ui.Fill(client, WithA(th.bg, 0.25f));
    } else {
        target_->Clear(DC(th.bg, 1));
    }
}

void UiWindow::Paint() {
    if (!target_) CreateTarget();
    if (!target_) { ValidateRect(hwnd, nullptr); return; }

    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = (float)(now.QuadPart - lastTick_.QuadPart) / (float)freq.QuadPart;
    lastTick_ = now;
    ui.dt = dt > 0.05f ? 0.05f : dt;

    const Config& cfg = AppConfig();
    ui.th = MakeTheme(cfg);
    ui.animationsOn = cfg.theme.animations;
    ui.rt = target_;
    ui.alpha = 1.f;
    ui.inputBlocked = editing_;

    RECT rc;
    GetClientRect(hwnd, &rc);
    Rect client(0, 0, rc.right / scale, rc.bottom / scale);

    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    DrawBackground(client);
    ui.BeginFrame();
    Frame(ui, client);
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    if (editing_) DrawEdit();
    ui.EndFrame();
    HRESULT hr = target_->EndDraw();
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) {
        SafeRelease(bgBitmap_);
        SafeRelease(ui.brush);
        SafeRelease(target_);
    }
    ValidateRect(hwnd, nullptr);

    if (ui.animating && !timer_) { SetTimer(hwnd, 1, 15, nullptr); timer_ = true; }
    if (!ui.animating && timer_) { KillTimer(hwnd, 1); timer_ = false; }
    if (!ui.deferred.empty()) PostMessageW(hwnd, WM_UI_DEFERRED, 0, 0);
}

LRESULT UiWindow::Msg(UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        Paint();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (target_) target_->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        Invalidate();
        return 0;
    case WM_DPICHANGED: {
        scale = HIWORD(wp) / 96.f;
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (target_) target_->SetDpi(scale * 96.f, scale * 96.f);
        Invalidate();
        return 0;
    }
    case WM_MOUSEMOVE:
        ui.mx = GET_X_LPARAM(lp) / scale;
        ui.my = GET_Y_LPARAM(lp) / scale;
        if (!tracking_) {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&t);
            tracking_ = true;
        }
        Invalidate();
        return 0;
    case WM_MOUSELEAVE:
        tracking_ = false;
        if (!ui.down) { ui.mx = ui.my = -1e6f; }
        Invalidate();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        ui.mx = GET_X_LPARAM(lp) / scale;
        ui.my = GET_Y_LPARAM(lp) / scale;
        if (editing_) {
            if (editRect_.Has(ui.mx, ui.my)) {   // click inside the field: move the caret
                selectAll_ = false;
                caret_ = EditHit(ui.mx);
                caretOn_ = true;
                Invalidate();
                return 0;
            }
            EndEdit(true);   // click outside: save and let the click through
        }
        ui.down = true;
        ui.pressed = true;
        SetCapture(hwnd);
        Invalidate();
        UpdateWindow(hwnd);
        return 0;
    case WM_LBUTTONUP:
        ui.mx = GET_X_LPARAM(lp) / scale;
        ui.my = GET_Y_LPARAM(lp) / scale;
        ui.down = false;
        ui.released = true;
        ReleaseCapture();
        Invalidate();
        UpdateWindow(hwnd);
        return 0;
    case WM_MOUSEWHEEL:
        if (editing_) EndEdit(true);
        ui.wheel += GET_WHEEL_DELTA_WPARAM(wp) / 120.f;
        Invalidate();
        return 0;
    case WM_TIMER:
        if (wp == 1) Invalidate();
        if (wp == 2) { caretOn_ = !caretOn_; Invalidate(); }
        return 0;
    case WM_KEYDOWN:
        if (editing_ && EditKey(wp)) return 0;
        break;
    case WM_CHAR:
        if (editing_) { EditChar((wchar_t)wp); return 0; }
        break;
    case WM_KILLFOCUS:
        if (editing_) EndEdit(true);
        break;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && !modal_) OnDeactivate();
        return 0;
    case WM_SETTINGCHANGE:
        if (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) { ApplyTheme(); Invalidate(); }
        return 0;
    case WM_UI_DEFERRED: {
        auto work = std::move(ui.deferred);
        ui.deferred.clear();
        for (auto& f : work) f();
        Invalidate();
        return 0;
    }
    case WM_UI_ENDEDIT:
        EndEdit(wp != 0);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        OnClosed();
        return 0;
    }
    return DefWindowProcW(hwnd, m, wp, lp);
}

LRESULT CALLBACK UiWindow::Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    UiWindow* self;
    if (m == WM_NCCREATE) {
        self = (UiWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (UiWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcW(h, m, wp, lp);
    if (m == WM_NCDESTROY) {
        g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), self), g_windows.end());
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        self->hwnd = nullptr;
        delete self;
        return DefWindowProcW(h, m, wp, lp);
    }
    return self->Msg(m, wp, lp);
}

// ----------------------------------------------------------------- inline text field

void UiWindow::BeginEdit(const Rect& r, const std::wstring& text, float fontSize,
                         std::function<void(const std::wstring&)> done) {
    if (editing_) EndEdit(true);
    editing_ = true;
    editText_ = text;
    caret_ = text.size();
    selectAll_ = true;
    caretOn_ = true;
    editRect_ = r;
    editSize_ = fontSize;
    editScroll_ = 0;
    editDone_ = std::move(done);
    ui.SetAnim(Ui::Id("edit_open"), 0.f);
    SetFocus(hwnd);
    SetTimer(hwnd, 2, 530, nullptr);
    Invalidate();
}

void UiWindow::EndEdit(bool commit) {
    if (!editing_) return;
    editing_ = false;
    KillTimer(hwnd, 2);
    auto done = std::move(editDone_);
    editDone_ = nullptr;
    std::wstring text = editText_;
    // trim spaces
    while (!text.empty() && text.back() == L' ') text.pop_back();
    while (!text.empty() && text.front() == L' ') text.erase(0, 1);
    if (commit && done) done(text);
    Invalidate();
}

void UiWindow::EditInsert(const std::wstring& s) {
    if (selectAll_) { editText_.clear(); caret_ = 0; selectAll_ = false; }
    for (wchar_t c : s) {
        if (c < 32 || editText_.size() >= 48) continue;
        editText_.insert(editText_.begin() + caret_, c);
        caret_++;
    }
}

void UiWindow::EditChar(wchar_t ch) {
    if (ch >= 32 && ch != 127) EditInsert(std::wstring(1, ch));
    caretOn_ = true;
    Invalidate();
}

bool UiWindow::EditKey(WPARAM vk) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    switch (vk) {
    case VK_RETURN: EndEdit(true); return true;
    case VK_ESCAPE: EndEdit(false); return true;
    case VK_BACK:
        if (selectAll_) { editText_.clear(); caret_ = 0; selectAll_ = false; }
        else if (caret_ > 0) { editText_.erase(caret_ - 1, 1); caret_--; }
        break;
    case VK_DELETE:
        if (selectAll_) { editText_.clear(); caret_ = 0; selectAll_ = false; }
        else if (caret_ < editText_.size()) editText_.erase(caret_, 1);
        break;
    case VK_LEFT: if (selectAll_) { caret_ = 0; selectAll_ = false; } else if (caret_ > 0) caret_--; break;
    case VK_RIGHT: if (selectAll_) { caret_ = editText_.size(); selectAll_ = false; } else if (caret_ < editText_.size()) caret_++; break;
    case VK_HOME: caret_ = 0; selectAll_ = false; break;
    case VK_END: caret_ = editText_.size(); selectAll_ = false; break;
    case 'A': if (!ctrl) return false; selectAll_ = true; caret_ = editText_.size(); break;
    case 'V': {
        if (!ctrl) return false;
        if (OpenClipboard(hwnd)) {
            HANDLE hd = GetClipboardData(CF_UNICODETEXT);
            if (hd) {
                const wchar_t* p = (const wchar_t*)GlobalLock(hd);
                if (p) { EditInsert(p); GlobalUnlock(hd); }
            }
            CloseClipboard();
        }
        break;
    }
    case 'C': {
        if (!ctrl) return false;
        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            size_t bytes = (editText_.size() + 1) * sizeof(wchar_t);
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (g) {
                memcpy(GlobalLock(g), editText_.c_str(), bytes);
                GlobalUnlock(g);
                SetClipboardData(CF_UNICODETEXT, g);
            }
            CloseClipboard();
        }
        break;
    }
    default: return false;
    }
    caretOn_ = true;
    Invalidate();
    return true;
}

size_t UiWindow::EditHit(float x) {
    float local = x - (editRect_.x + 10) + editScroll_;
    size_t best = 0;
    float bestD = 1e9f;
    for (size_t i = 0; i <= editText_.size(); i++) {
        float w = ui.Measure(editText_.substr(0, i), editSize_, 600);
        float d = fabsf(w - local);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void UiWindow::DrawEdit() {
    const Theme& th = ui.th;
    float open = ui.Anim(Ui::Id("edit_open"), 1.f, 22.f);
    float saved = ui.alpha;
    ui.alpha = open;
    Rect r = editRect_;
    Rect field(r.x, r.y - 3, r.w, r.h + 6);
    // Windows 11 text box: filled field, thin border, accent underline when focused
    ui.Shadow(field, 6, 6, 0.6f);
    ui.Fill(field, th.dark ? Rgb(0x2B2B2D) : Rgb(0xFFFFFF), 6);
    ui.Stroke(field, th.ctrlStroke, 6);
    ui.Fill(Rect(field.x + 1, field.b() - 2, field.w - 2, 2), th.accent, 1);

    Rect inner(field.x + 10, field.y, field.w - 20, field.h);
    float caretX = ui.Measure(editText_.substr(0, caret_), editSize_, 600);
    if (caretX - editScroll_ > inner.w - 2) editScroll_ = caretX - inner.w + 2;
    if (caretX - editScroll_ < 0) editScroll_ = caretX;
    ui.PushClip(inner);
    float tw = ui.Measure(editText_, editSize_, 600);
    if (selectAll_ && !editText_.empty())
        ui.Fill(Rect(inner.x - editScroll_ - 1, inner.cy() - editSize_ * 0.75f, tw + 2, editSize_ * 1.5f),
                WithA(th.accent, 0.45f), 3);
    ui.Text(editText_, Rect(inner.x - editScroll_, inner.y, tw + 40, inner.h), editSize_, th.text, 600);
    if (caretOn_ && !selectAll_) {
        float cx = inner.x + caretX - editScroll_;
        ui.Fill(Rect(cx, inner.cy() - editSize_ * 0.7f, 1.5f, editSize_ * 1.4f), th.text, 0);
    }
    ui.PopClip();
    ui.alpha = saved;
}

// ----------------------------------------------------------------- dialogs

bool UiWindow::PickColor(COLORREF& c) {
    static COLORREF custom[16];
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.rgbResult = c;
    cc.lpCustColors = custom;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    modal_ = true;
    BOOL ok = ChooseColorW(&cc);
    modal_ = false;
    if (ok) c = cc.rgbResult;
    if (hwnd) SetForegroundWindow(hwnd);
    return ok != FALSE;
}

bool UiWindow::PickImage(std::wstring& path) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = hwnd;
    of.lpstrFilter = L"Images\0*.jpg;*.jpeg;*.png;*.bmp;*.webp;*.jfif\0All files\0*.*\0";
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    modal_ = true;
    BOOL ok = GetOpenFileNameW(&of);
    modal_ = false;
    if (ok) path = buf;
    return ok != FALSE;
}
