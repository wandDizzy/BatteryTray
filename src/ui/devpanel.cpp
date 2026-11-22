#include "devpanel.h"
#include "tray_icons.h"
#include "../app.h"
#include "../i18n/i18n.h"

namespace {
struct IconColor { const char* key; unsigned rgb; };
const IconColor kIconColors[] = {
    {"#FFFFFF", 0xFFFFFF}, {"#3D8BFF", 0x3D8BFF}, {"#9A6BFF", 0x9A6BFF}, {"#FF5EA8", 0xFF5EA8},
    {"#FF5A5F", 0xFF5A5F}, {"#FF9534", 0xFF9534}, {"#34C77B", 0x34C77B}, {"#22B8C4", 0x22B8C4},
};
}

std::wstring DeviceStatusText(const Device& d) {
    if (!d.online) return T("not_connected");
    if (d.idle) return T("asleep");
    if (d.charging) return T("charging");
    if (d.battery < 0) return T("no_level");
    return T("online");
}

std::wstring DeviceSourceText(const Device& d) {
    if (d.source == "dongle" || d.source == "razer" || d.source == "headset" || d.source == "asus") return T("src_dongle");
    if (d.source == "hidpp") return T("src_hidpp");
    if (d.source == "bluetooth") return T("src_bluetooth");
    if (d.source == "xinput") return T("src_xinput");
    return T("src_hid");
}

Color DeviceLevelColor(const Device& d) { return FromCR(LevelColor(d, AppConfig())); }

float DevicePanelHeight(const Device&) { return 540.f; }

float DevicePanel(Ui& ui, UiWindow* host, const Rect& r, const Device& dev, bool showAllSettings) {
    const Theme& th = ui.th;
    const std::string id = dev.id;
    DeviceSettings& ds = AppDeviceSettings(id);
    Config& cfg = AppConfig();
    const float x = r.x, w = r.w;
    float y = r.y;
    auto ID = [&](const char* s, int i = 0) { return Ui::Id((id + s).c_str(), i); };

    // ---------------------------------------------------------- header
    COLORREF custom;
    bool hasCustom = AppGlyphColor(dev, &custom);
    Color glyphCol = hasCustom ? FromCR(custom) : th.accent;
    Rect tile(x, y, 52, 52);
    ui.Fill(tile, WithA(glyphCol, 0.16f), 14);
    ui.Stroke(tile, WithA(glyphCol, 0.30f), 14);
    ui.Icon(AppGlyph(dev), tile, 26, glyphCol);

    std::wstring name = AppDisplayName(dev);
    Rect nameR(x + 66, y + 2, w - 66, 28);
    bool nameHot = ui.Over(nameR);
    if (ui.Interact(ID("name"), nameR)) {
        std::wstring original = dev.name;
        Rect abs = ui.Abs(Rect(nameR.x - 10, nameR.y, nameR.w + 10, nameR.h));
        ui.Defer([host, abs, name, id, original]() {
            host->BeginEdit(abs, name, 17.f, [id, original](const std::wstring& t) {
                DeviceSettings& s = AppDeviceSettings(id);
                s.customName = (t.empty() || t == original) ? L"" : t;
                AppApply();
            });
        });
    }
    float nw = ui.Measure(name, 17.f, 600);
    if (nw > nameR.w - 24) nw = nameR.w - 24;
    ui.Text(name, Rect(nameR.x, nameR.y, nw + 2, nameR.h), 17.f, th.text, 600);
    float pen = ui.Anim(ID("pen"), nameHot ? 1.f : 0.f, 18.f);
    if (pen > 0.01f) ui.Icon(Ico::Edit, Rect(nameR.x + nw + 6, nameR.y, 20, nameR.h), 12, WithA(th.text2, pen));

    // status line: dot + status · source
    Color lc = DeviceLevelColor(dev);
    Color dot = !dev.online ? th.text3 : dev.idle ? Rgb(0xF2C230) : dev.charging ? th.accent : Rgb(0x34C77B);
    ui.Circle(x + 70, y + 41, 3.5f, dot);
    ui.Text(DeviceStatusText(dev) + L"  ·  " + DeviceSourceText(dev), Rect(x + 80, y + 31, w - 80, 20), 12.5f, th.text2);
    y += 68;

    // ---------------------------------------------------------- battery
    std::wstring pct = dev.battery >= 0 ? std::to_wstring(dev.battery) : L"—";
    float pw = ui.Measure(pct, 40.f, 700);
    ui.Text(pct, Rect(x, y, pw + 4, 48), 40.f, th.text, 700);
    if (dev.battery >= 0) ui.Text(L"%", Rect(x + pw + 2, y + 8, 30, 36), 20.f, th.text2, 600);
    if (dev.online && dev.charging) ui.Icon(Ico::Bolt, Rect(x + pw + 26, y + 6, 28, 40), 20, th.accent);
    std::wstring note = dev.idle || !dev.online ? std::wstring(T("last_known")) : L"";
    ui.Text(note, Rect(x + w * 0.45f, y, w * 0.55f, 48), 12.f, th.text3, 400, ALIGN_RIGHT);
    y += 54;
    ui.Bar(ID("bar"), Rect(x, y, w, 8), dev.battery >= 0 ? dev.battery / 100.f : 0.f, lc);
    y += 26;
    ui.Line(x, y, x + w, y, th.divider);
    y += 14;

    // ---------------------------------------------------------- icon choice
    ui.Text(T("dev_icon"), Rect(x, y, w, 20), 13.f, th.text2, 600);
    y += 26;
    const auto& glyphs = GlyphChoices();
    int perRow = 7;
    float gap = 6, bw = (w - gap * (perRow - 1)) / perRow;
    if (bw > 40) bw = 40;
    for (size_t i = 0; i < glyphs.size(); i++) {
        int row = (int)i / perRow, col = (int)i % perRow;
        Rect b(x + col * (bw + gap), y + row * (bw + gap), bw, bw);
        bool sel = ds.glyph == glyphs[i].key;
        int bid = ID("glyph", (int)i);
        bool click;
        if (glyphs[i].ch == 0) {   // "auto"
            click = ui.IconButton(bid, b, 0, sel);
            ui.Text(T("auto"), b, 10.5f, sel ? th.accent : th.text, 600, ALIGN_CENTER);
        } else {
            click = ui.IconButton(bid, b, glyphs[i].ch, sel, 17.f);
        }
        if (click) { ds.glyph = glyphs[i].key; AppApply(); }
    }
    int rows = ((int)glyphs.size() + perRow - 1) / perRow;
    y += rows * (bw + gap) + 10;

    // ---------------------------------------------------------- icon color
    ui.Text(T("dev_color"), Rect(x, y, w, 20), 13.f, th.text2, 600);
    y += 26;
    float sw = 30;
    float sx = x;
    {   // auto
        Rect b(sx, y, sw, sw);
        bool sel = ds.color.empty();
        if (ui.Swatch(ID("col_auto"), b, th.dark ? Rgb(0x3A3A3A) : Rgb(0xE6E6E6), sel)) { ds.color.clear(); AppApply(); }
        ui.Text(L"A", b, 12.f, th.text, 700, ALIGN_CENTER);
        sx += sw + 4;
    }
    bool presetSel = false;
    for (int i = 0; i < 8; i++) {
        Rect b(sx, y, sw, sw);
        bool sel = ds.color == kIconColors[i].key;
        presetSel |= sel;
        if (ui.Swatch(ID("col", i), b, Rgb(kIconColors[i].rgb), sel)) { ds.color = kIconColors[i].key; AppApply(); }
        sx += sw + 4;
    }
    {   // custom
        Rect b(sx, y, sw, sw);
        bool sel = !ds.color.empty() && !presetSel;
        COLORREF cc = RGB(255, 255, 255);
        HexToColor(ds.color, &cc);
        if (ui.Swatch(ID("col_custom"), b, sel ? FromCR(cc) : th.ctrl, sel)) {
            ui.Defer([host, id, cc]() {
                COLORREF c = cc;
                if (host->PickColor(c)) { AppDeviceSettings(id).color = ColorToHex(c); AppApply(); }
            });
        }
        ui.Icon(Ico::Add, b, 11, sel ? Rgb(0xFFFFFF) : th.text);
    }
    y += sw + 16;
    ui.Line(x, y, x + w, y, th.divider);
    y += 8;

    // ---------------------------------------------------------- toggles
    auto toggleRow = [&](const wchar_t* label, wchar_t icon, bool& v, int tid) {
        Rect row(x, y, w, 36);
        ui.Icon(icon, Rect(x, y, 20, 36), 14, th.text2);
        ui.Text(label, Rect(x + 28, y, w - 80, 36), 13.5f, th.text);
        bool changed = ui.Toggle(tid, Rect(x + w - 40, y, 40, 36), v);
        if (!changed && ui.Interact(tid + 99, Rect(x, y, w - 48, 36))) { v = !v; changed = true; }
        y += 38;
        (void)row;
        return changed;
    };
    bool show = !ds.hidden;
    if (toggleRow(T("dev_show"), Ico::Eye, show, ID("show"))) { ds.hidden = !show; AppApply(); }
    if (toggleRow(T("dev_notify"), Ico::Bell, ds.notify, ID("notify"))) AppApply();

    int thr = ds.lowThreshold >= 0 ? ds.lowThreshold : cfg.lowThreshold;
    float na = ui.Anim(ID("thr_anim"), ds.notify ? 1.f : 0.4f, 14.f);
    float sa = ui.alpha;
    ui.alpha *= na;
    ui.Text(T("dev_threshold"), Rect(x + 28, y, w - 90, 24), 12.5f, th.text2);
    ui.Text(std::to_wstring(thr) + L"%", Rect(x + w - 60, y, 60, 24), 12.5f, th.text, 600, ALIGN_RIGHT);
    y += 24;
    if (ui.Slider(ID("thr"), Rect(x + 28, y, w - 38, 24), thr, 5, 50)) { ds.lowThreshold = thr; AppApply(); }
    ui.alpha = sa;
    y += 36;

    // ---------------------------------------------------------- position / all settings
    if (ui.IconButton(ID("left"), Rect(x, y, 36, 32), Ico::Left, false, 12.f)) AppMoveDevice(id, -1);
    if (ui.IconButton(ID("right"), Rect(x + 42, y, 36, 32), Ico::Right, false, 12.f)) AppMoveDevice(id, +1);
    ui.Text(T("dev_position"), Rect(x + 86, y, w * 0.45f, 32), 12.f, th.text3);
    if (showAllSettings) {
        float bwid = ui.Measure(T("dev_all_settings"), 13.f) + 52;
        if (ui.Button(ID("all"), Rect(x + w - bwid, y, bwid, 32), T("dev_all_settings"), false, Ico::Settings))
            ui.Defer([]() { AppOpenSettings(0); });
    }
    y += 32;
    return y - r.y;
}
