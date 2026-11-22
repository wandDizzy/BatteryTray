#pragma once
// Small immediate-mode UI kit on Direct2D + DirectWrite.
//
// A window derives from UiWindow and implements Frame(): every frame it lays out
// and draws its controls; controls return true when the user interacts with them.
// Frames are rendered on input and while animations are running (~60 fps), so an
// idle window costs nothing.
#include "theme.h"
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    Rect() = default;
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    float r() const { return x + w; }
    float b() const { return y + h; }
    float cx() const { return x + w / 2; }
    float cy() const { return y + h / 2; }
    bool Has(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    Rect Inset(float d) const { return Rect(x + d, y + d, w - 2 * d, h - 2 * d); }
    Rect Inset(float dx, float dy) const { return Rect(x + dx, y + dy, w - 2 * dx, h - 2 * dy); }
    Rect Offset(float dx, float dy) const { return Rect(x + dx, y + dy, w, h); }
};

enum Align { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

// Segoe Fluent Icons code points used across the app
namespace Ico {
const wchar_t Mouse = 0xE962, Keyboard = 0xE765, Headset = 0xE7F6, Gamepad = 0xE7FC, Speaker = 0xE7F5,
              Phone = 0xE8EA, Pen = 0xEDC6, Device = 0xE772, Bluetooth = 0xE702, Bolt = 0xE945,
              Settings = 0xE713, Edit = 0xE70F, Left = 0xE76B, Right = 0xE76C, Check = 0xE73E,
              Info = 0xE946, Color = 0xE790, Battery = 0xE83F, Picture = 0xE91B, Globe = 0xE774,
              Refresh = 0xE72C, Link = 0xE71B, Folder = 0xE8B7, Bug = 0xEBE8, Brush = 0xE771,
              Bell = 0xEA8F, Eye = 0xE890, Clock = 0xE916, Add = 0xE710, Close = 0xE711;
}

class Ui {
public:
    // ---- frame state (filled by UiWindow) ----
    ID2D1RenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    Theme th;
    float alpha = 1.f;                 // global opacity (page / window transitions)
    float mx = -1e6f, my = -1e6f;       // mouse in DIPs
    bool down = false;                 // left button held
    bool pressed = false, released = false;
    float wheel = 0;                   // wheel delta in notches
    float dt = 0.016f;
    bool animationsOn = true;
    bool animating = false;            // set by Anim() when something is still moving
    int hot = 0, active = 0;
    bool inputBlocked = false;

    static int Id(const char* s, int i = 0);

    // ---- animation ----
    float Anim(int id, float target, float speed = 14.f);
    void SetAnim(int id, float value);
    bool HasAnim(int id) const { return anims_.count(id) != 0; }

    // ---- input helpers ----
    bool Over(const Rect& r) const { return !inputBlocked && r.Has(mx - offX_, my - offY_); }   // r in local coords
    Rect Abs(const Rect& r) const { return r.Offset(offX_, offY_); }                             // local -> window
    bool Interact(int id, const Rect& r);      // true on click (press + release inside)
    bool IsActive(int id) const { return active == id; }

    // ---- primitives ----
    void Fill(const Rect& r, Color c, float radius = 0);
    void Stroke(const Rect& r, Color c, float radius = 0, float width = 1.f);
    void Circle(float cx, float cy, float rad, Color c);
    void CircleStroke(float cx, float cy, float rad, Color c, float width);
    void Line(float x1, float y1, float x2, float y2, Color c, float width = 1.f);
    void Gradient(const Rect& r, Color a, Color b, float radius, bool horizontal = true);
    void Text(const std::wstring& s, const Rect& r, float size, Color c, int weight = 400, int align = ALIGN_LEFT,
              bool wrap = false);
    float Measure(const std::wstring& s, float size, int weight = 400);
    void Icon(wchar_t g, const Rect& r, float size, Color c);
    void Shadow(const Rect& r, float radius, float spread, float strength);

    // ---- controls ----
    bool Button(int id, const Rect& r, const std::wstring& label, bool accent = false, wchar_t glyph = 0);
    bool IconButton(int id, const Rect& r, wchar_t glyph, bool selected = false, float glyphSize = 16.f);
    bool Toggle(int id, const Rect& r, bool& v);          // r = 40 x 20 switch
    bool Slider(int id, const Rect& r, int& v, int lo, int hi);
    bool Segmented(int id, const Rect& r, const std::vector<std::wstring>& items, int& sel);
    bool Swatch(int id, const Rect& r, Color c, bool selected);
    bool Card(int id, const Rect& r, bool clickable = false);   // returns click
    void Bar(int id, const Rect& r, float frac, Color c);
    bool Link(int id, const Rect& r, const std::wstring& s, float size);

    // ---- clipping / transform ----
    void PushClip(const Rect& r);
    void PopClip();
    void PushOffset(float dx, float dy);
    void PopOffset();

    // work to run after the frame (modal dialogs, window changes)
    std::vector<std::function<void()>> deferred;
    void Defer(std::function<void()> f) { deferred.push_back(std::move(f)); }

    // per-frame bookkeeping used by UiWindow
    void BeginFrame();
    void EndFrame();

private:
    void SetColor(Color c);
    IDWriteTextFormat* Format(float size, int weight, bool icon);
    std::map<int, float> anims_;
    std::map<long long, IDWriteTextFormat*> formats_;
    std::vector<D2D1_MATRIX_3X2_F> xform_;
    float offX_ = 0, offY_ = 0;
    std::vector<std::pair<float, float>> offStack_;
public:
    ~Ui();
};

class UiWindow {
public:
    virtual ~UiWindow();
    HWND hwnd = nullptr;
    float scale = 1.f;   // DPI / 96

    void Invalidate();
    void ApplyTheme();                    // dark title bar, backdrop, corners
    static void RepaintAll();
    static void ApplyThemeAll();
    static void CloseAll();

    // inline text field drawn by the window itself (r in window DIPs)
    void BeginEdit(const Rect& r, const std::wstring& text, float fontSize,
                   std::function<void(const std::wstring&)> done);
    bool Editing() const { return editing_; }

    // native color picker; keeps popups open while it is shown
    bool PickColor(COLORREF& c);
    // native "open picture" dialog
    bool PickImage(std::wstring& path);

    bool Create(const wchar_t* cls, DWORD style, DWORD exStyle, const wchar_t* title, RECT pxRect, HWND owner,
                bool popup);

protected:
    virtual void Frame(Ui& ui, const Rect& client) = 0;
    virtual LRESULT Msg(UINT m, WPARAM wp, LPARAM lp);
    virtual void OnDeactivate() {}
    virtual void OnClosed() {}
    bool popup_ = false;
    bool modal_ = false;      // a modal dialog is open (do not close on deactivate)
    Ui ui;

private:
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp);
    void Paint();
    void CreateTarget();
    void DrawBackground(const Rect& client);
    void EndEdit(bool commit);

    ID2D1HwndRenderTarget* target_ = nullptr;
    bool backdrop_ = false;
    LARGE_INTEGER lastTick_{};
    bool timer_ = false;
    bool tracking_ = false;
    bool painting_ = false;

    // background picture
    ID2D1Bitmap* bgBitmap_ = nullptr;
    std::wstring bgKey_;

    // inline text field
    bool editing_ = false;
    std::wstring editText_;
    size_t caret_ = 0;
    bool selectAll_ = false;
    bool caretOn_ = true;
    Rect editRect_;
    float editSize_ = 15.f;
    float editScroll_ = 0;
    std::function<void(const std::wstring&)> editDone_;
    void DrawEdit();
    bool EditKey(WPARAM vk);
    void EditChar(wchar_t ch);
    void EditInsert(const std::wstring& s);
    size_t EditHit(float x);
};

namespace gfx {
ID2D1Factory* D2D();
IDWriteFactory* DWrite();
const wchar_t* IconFont();
const wchar_t* TextFont();
}
