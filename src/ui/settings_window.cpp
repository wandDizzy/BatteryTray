#include "settings_window.h"
#include "devpanel.h"
#include "../app.h"
#include "../i18n/i18n.h"
#include <shellapi.h>

namespace {

class SettingsWindow;
SettingsWindow* g_settings = nullptr;

class SettingsWindow : public UiWindow {
public:
    int page = 0;
    std::string expanded;          // device id expanded on the Devices page
    float scrollTarget = 0;
    float contentH = 0;

    void SetPage(int p) {
        if (p == page) return;
        page = p;
        scrollTarget = 0;
        ui.SetAnim(Ui::Id("page_anim"), 0.f);
        ui.SetAnim(Ui::Id("scroll"), 0.f);
    }

    // ------------------------------------------------------------ helpers
    // Card row: icon + title (+ description). Returns the rectangle for the control on the right.
    Rect Row(Ui& ui, float& y, float x, float w, wchar_t icon, const std::wstring& title,
             const std::wstring& desc = L"", float h = 0) {
        const Theme& th = ui.th;
        if (h <= 0) h = desc.empty() ? 56.f : 66.f;
        Rect card(x, y, w, h);
        ui.Card(0, card);
        ui.Icon(icon, Rect(x + 16, y, 24, h), 17, th.text);
        if (desc.empty()) {
            ui.Text(title, Rect(x + 56, y, w * 0.5f, h), 14.f, th.text);
        } else {
            ui.Text(title, Rect(x + 56, y + h / 2 - 21, w * 0.55f, 22), 14.f, th.text);
            ui.Text(desc, Rect(x + 56, y + h / 2 + 1, w * 0.55f, 20), 12.f, th.text2);
        }
        y += h + 6;
        return Rect(x + w * 0.45f, card.y, w * 0.55f - 16, h);
    }

    void Header(Ui& ui, float& y, float x, const std::wstring& s) {
        ui.Text(s, Rect(x, y, 600, 22), 14.f, ui.th.text, 600);
        y += 30;
    }

    // ------------------------------------------------------------ pages
    void PageDevices(Ui& ui, float x, float& y, float w) {
        const Theme& th = ui.th;
        const auto& devs = AppDevices();
        if (devs.empty()) {
            ui.Card(0, Rect(x, y, w, 90));
            ui.Icon(Ico::Device, Rect(x, y + 12, w, 36), 26, th.text3);
            ui.Text(T("dev_none"), Rect(x, y + 50, w, 24), 13.f, th.text2, 400, ALIGN_CENTER);
            y += 96;
            return;
        }
        for (auto& d : devs) {
            bool open = expanded == d.id;
            int eid = Ui::Id((d.id + "_expand").c_str());
            float ex = ui.Anim(eid, open ? 1.f : 0.f, 12.f);
            float panelH = DevicePanelHeight(d) + 20;
            float h = 76 + panelH * ex;
            Rect card(x, y, w, h);
            Rect head(x, y, w, 76);
            if (ui.Card(Ui::Id((d.id + "_card").c_str()), head, true)) {
                expanded = open ? "" : d.id;
            }
            ui.Fill(card, WithA(th.card, th.card.a * ex), 8);
            ui.Stroke(card, WithA(th.cardStroke, th.cardStroke.a * ex), 8);

            COLORREF cc;
            Color gc = AppGlyphColor(d, &cc) ? FromCR(cc) : th.accent;
            Rect tile(x + 16, y + 14, 48, 48);
            ui.Fill(tile, WithA(gc, 0.15f), 12);
            ui.Icon(AppGlyph(d), tile, 22, gc);
            ui.Text(AppDisplayName(d), Rect(x + 78, y + 15, w * 0.45f, 24), 15.f, th.text, 600);
            ui.Text(DeviceStatusText(d) + L"  ·  " + DeviceSourceText(d), Rect(x + 78, y + 39, w * 0.45f, 20), 12.f,
                    th.text2);
            if (AppDeviceSettings(d.id).hidden)
                ui.Icon(0xED1A, Rect(x + 78 + ui.Measure(AppDisplayName(d), 15.f, 600) + 6, y + 15, 20, 24), 12, th.text3);

            std::wstring pct = d.battery >= 0 ? std::to_wstring(d.battery) + L"%" : L"—";
            ui.Text(pct, Rect(x + w - 230, y + 14, 70, 48), 20.f, th.text, 700, ALIGN_RIGHT);
            ui.Bar(Ui::Id((d.id + "_minibar").c_str()), Rect(x + w - 148, y + 35, 90, 6),
                   d.battery >= 0 ? d.battery / 100.f : 0.f, DeviceLevelColor(d));
            if (d.online && d.charging) ui.Icon(Ico::Bolt, Rect(x + w - 54, y + 14, 16, 48), 13, th.accent);
            float rot = ex;
            ui.Icon(rot > 0.5f ? 0xE70E : 0xE70D, Rect(x + w - 40, y + 14, 24, 48), 11, th.text2);

            if (ex > 0.01f) {
                ui.PushClip(Rect(x, y + 76, w, panelH * ex));   // local coords: Direct2D applies the offset
                float sa = ui.alpha;
                ui.alpha *= ex;
                ui.Line(x + 16, y + 76, x + w - 16, y + 76, th.divider);
                DevicePanel(ui, this, Rect(x + 24, y + 92, w - 48, panelH), d, false);
                ui.alpha = sa;
                ui.PopClip();
            }
            y += h + 8;
        }
    }

    void PageAppearance(Ui& ui, float x, float& y, float w) {
        Config& c = AppConfig();
        const Theme& th = ui.th;

        Header(ui, y, x, T("ap_theme"));
        {
            Rect ctl = Row(ui, y, x, w, Ico::Brush, T("ap_theme"));
            int sel = c.theme.mode == "dark" ? 1 : c.theme.mode == "light" ? 2 : 0;
            if (ui.Segmented(Ui::Id("seg_theme"), Rect(ctl.r() - 330, ctl.cy() - 16, 330, 32),
                             {T("theme_system"), T("theme_dark"), T("theme_light")}, sel)) {
                c.theme.mode = sel == 1 ? "dark" : sel == 2 ? "light" : "system";
                AppApply();
            }
        }
        {
            Rect card(x, y, w, 70);
            ui.Card(0, card);
            ui.Icon(Ico::Color, Rect(x + 16, y, 24, 70), 17, th.text);
            ui.Text(T("ap_accent"), Rect(x + 56, y, 200, 70), 14.f, th.text);
            float sx = x + w - 16 - 10 * 34;
            bool sys = c.theme.accent == "system";
            if (ui.Swatch(Ui::Id("acc_sys"), Rect(sx, y + 19, 32, 32), FromCR(SystemAccentColor()), sys)) {
                c.theme.accent = "system"; AppApply();
            }
            ui.Icon(0xE7F8, Rect(sx, y + 19, 32, 32), 11, Rgb(0xFFFFFF));
            sx += 34;
            bool presetSel = sys;
            const auto& presets = AccentPresets();
            for (size_t i = 0; i < presets.size(); i++) {
                bool sel = c.theme.accent == presets[i].key;
                presetSel |= sel;
                if (ui.Swatch(Ui::Id("acc", (int)i), Rect(sx, y + 19, 32, 32), Rgb(presets[i].rgb), sel)) {
                    c.theme.accent = presets[i].key; AppApply();
                }
                sx += 34;
            }
            COLORREF cur = RGB(255, 255, 255);
            bool customSel = !presetSel && HexToColor(c.theme.accent, &cur);
            if (ui.Swatch(Ui::Id("acc_custom"), Rect(sx, y + 19, 32, 32), customSel ? FromCR(cur) : th.ctrl, customSel)) {
                ui.Defer([this, cur]() {
                    COLORREF col = cur;
                    if (PickColor(col)) { AppConfig().theme.accent = ColorToHex(col); AppApply(); }
                });
            }
            ui.Icon(Ico::Add, Rect(sx, y + 19, 32, 32), 11, customSel ? Rgb(0xFFFFFF) : th.text);
            y += 76;
        }
        {
            int sel = c.theme.background == "acrylic" ? 1 : c.theme.background == "solid" ? 2 : c.theme.background == "image" ? 3 : 0;
            float extra = sel == 3 ? 132.f : 0.f;
            float ea = ui.Anim(Ui::Id("bg_extra"), extra, 14.f);
            Rect card(x, y, w, 60 + ea);
            ui.Card(0, card);
            ui.Icon(Ico::Picture, Rect(x + 16, y, 24, 60), 17, th.text);
            ui.Text(T("ap_background"), Rect(x + 56, y, 200, 60), 14.f, th.text);
            if (ui.Segmented(Ui::Id("seg_bg"), Rect(x + w - 16 - 420, y + 14, 420, 32),
                             {T("bg_mica"), T("bg_acrylic"), T("bg_solid"), T("bg_image")}, sel)) {
                const char* v[] = {"mica", "acrylic", "solid", "image"};
                c.theme.background = v[sel];
                if (sel == 3 && c.theme.image.empty()) {
                    ui.Defer([this]() {
                        std::wstring p;
                        if (PickImage(p)) AppConfig().theme.image = p;
                        else AppConfig().theme.background = "mica";
                        AppApply();
                        UiWindow::ApplyThemeAll();
                    });
                }
                AppApply();
                ui.Defer([]() { UiWindow::ApplyThemeAll(); });
            }
            if (ea > 1.f) {
                ui.PushClip(Rect(x, y + 60, w, ea));
                float yy = y + 66;
                std::wstring file = c.theme.image;
                size_t sl = file.find_last_of(L"\\/");
                if (sl != std::wstring::npos) file = file.substr(sl + 1);
                ui.Text(file.empty() ? L"—" : file, Rect(x + 56, yy, w - 300, 32), 12.5f, th.text2);
                float bw = ui.Measure(T("ap_choose_image"), 13.f) + 52;
                if (ui.Button(Ui::Id("pick_img"), Rect(x + w - 16 - bw, yy, bw, 32), T("ap_choose_image"), false, Ico::Folder)) {
                    ui.Defer([this]() {
                        std::wstring p;
                        if (PickImage(p)) { AppConfig().theme.image = p; AppApply(); }
                    });
                }
                yy += 44;
                ui.Text(T("ap_blur"), Rect(x + 56, yy, 120, 24), 12.5f, th.text2);
                int blur = c.theme.imageBlur;
                if (ui.Slider(Ui::Id("blur"), Rect(x + 180, yy, w - 260, 24), blur, 0, 60)) { c.theme.imageBlur = blur; AppApply(); }
                ui.Text(std::to_wstring(blur), Rect(x + w - 70, yy, 54, 24), 12.5f, th.text, 600, ALIGN_RIGHT);
                yy += 34;
                ui.Text(T("ap_dim"), Rect(x + 56, yy, 120, 24), 12.5f, th.text2);
                int dim = c.theme.imageDim;
                if (ui.Slider(Ui::Id("dim"), Rect(x + 180, yy, w - 260, 24), dim, 0, 90)) { c.theme.imageDim = dim; AppApply(); }
                ui.Text(std::to_wstring(dim) + L"%", Rect(x + w - 70, yy, 54, 24), 12.5f, th.text, 600, ALIGN_RIGHT);
                ui.PopClip();
            }
            y += 66 + ea;
        }
        {
            Rect ctl = Row(ui, y, x, w, 0xE7F4, T("ap_anim"));
            bool v = c.theme.animations;
            if (ui.Toggle(Ui::Id("anim"), Rect(ctl.r() - 40, ctl.y, 40, ctl.h), v)) { c.theme.animations = v; AppApply(); }
        }

        y += 12;
        Header(ui, y, x, T("ap_display"));
        {
            Rect ctl = Row(ui, y, x, w, 0xE7F4, T("ap_display"));
            int sel = c.displayMode;
            if (ui.Segmented(Ui::Id("seg_disp"), Rect(ctl.r() - 360, ctl.cy() - 16, 360, 32),
                             {T("disp_widget"), T("disp_tray"), T("disp_both")}, sel)) {
                c.displayMode = sel; AppApply();
            }
        }
        {
            Rect ctl = Row(ui, y, x, w, Ico::Battery, T("ap_tray_style"));
            int sel = c.iconStyle;
            if (ui.Segmented(Ui::Id("seg_style"), Rect(ctl.r() - 360, ctl.cy() - 16, 360, 32),
                             {T("style_number"), T("style_badge"), T("style_battery")}, sel)) {
                c.iconStyle = sel; AppApply();
            }
        }
        {
            Rect ctl = Row(ui, y, x, w, 0xE8D2, T("ap_percent"));
            bool v = c.widgetPercent;
            if (ui.Toggle(Ui::Id("pct"), Rect(ctl.r() - 40, ctl.y, 40, ctl.h), v)) { c.widgetPercent = v; AppApply(); }
        }
        {
            Rect ctl = Row(ui, y, x, w, 0xE7C9, T("ap_hover"));
            bool v = c.widgetHover;
            if (ui.Toggle(Ui::Id("hover"), Rect(ctl.r() - 40, ctl.y, 40, ctl.h), v)) { c.widgetHover = v; AppApply(); }
        }
        {
            Rect ctl = Row(ui, y, x, w, 0xE703, T("ap_offline"));
            int sel = !c.showOffline ? 2 : c.offlineStyle == "dim" ? 1 : 0;
            if (ui.Segmented(Ui::Id("seg_off"), Rect(ctl.r() - 380, ctl.cy() - 16, 380, 32),
                             {T("off_strike"), T("off_dim"), T("off_hide")}, sel)) {
                c.showOffline = sel != 2;
                if (sel != 2) c.offlineStyle = sel == 1 ? "dim" : "strike";
                AppApply();
            }
        }
        {
            Rect card(x, y, w, 70);
            ui.Card(0, card);
            ui.Icon(Ico::Color, Rect(x + 16, y, 24, 70), 17, th.text);
            ui.Text(T("ap_level_colors"), Rect(x + 56, y, 220, 70), 14.f, th.text);
            COLORREF* cols[4] = {&c.colHigh, &c.colMid, &c.colLow, &c.colCharging};
            const char* names[4] = {"col_high", "col_mid", "col_low", "col_chg"};
            float sx = x + w - 16 - 4 * 96;
            for (int i = 0; i < 4; i++) {
                Rect sw(sx + i * 96, y + 19, 32, 32);
                if (ui.Swatch(Ui::Id("lvl", i), sw, FromCR(*cols[i]), false)) {
                    COLORREF* target = cols[i];
                    ui.Defer([this, target]() {
                        COLORREF col = *target;
                        if (PickColor(col)) { *target = col; AppApply(); }
                    });
                }
                ui.Text(T(names[i]), Rect(sw.r() + 6, y, 56, 70), 12.f, th.text2);
            }
            y += 76;
        }

        // ---------------------------------------------------------- game overlay
        y += 12;
        Header(ui, y, x, T("ov_title"));
        {
            Rect ctl = Row(ui, y, x, w, Ico::Gamepad, T("ov_enable"), T("ov_desc"));
            bool v = c.overlay.enabled;
            if (ui.Toggle(Ui::Id("ov_on"), Rect(ctl.r() - 40, ctl.y, 40, ctl.h), v)) { c.overlay.enabled = v; AppApply(); }
        }
        float oa = ui.Anim(Ui::Id("ov_rows"), c.overlay.enabled ? 1.f : 0.35f, 14.f);
        float sa = ui.alpha;
        ui.alpha *= oa;
        {
            Rect ctl = Row(ui, y, x, w, 0xE916, T("ov_mode"));
            int sel = c.overlay.mode == "low" ? 1 : c.overlay.mode == "hotkey" ? 2 : 0;
            if (ui.Segmented(Ui::Id("ov_mode"), Rect(ctl.r() - 380, ctl.cy() - 16, 380, 32),
                             {T("ov_always"), T("ov_low"), T("ov_hotkey")}, sel)) {
                const char* v[] = {"always", "low", "hotkey"};
                c.overlay.mode = v[sel];
                AppApply();
            }
        }
        {
            Rect ctl = Row(ui, y, x, w, 0xE92E, T("ov_key"), T("ov_key_note"));
            const char* keys[] = {"shift+tab", "ctrl+shift+b", "alt+b", "ctrl+alt+b"};
            int sel = 0;
            for (int i = 0; i < 4; i++) if (c.overlay.hotkey == keys[i]) sel = i;
            if (ui.Segmented(Ui::Id("ov_key"), Rect(ctl.r() - 440, ctl.cy() - 16, 440, 32),
                             {L"Shift+Tab", L"Ctrl+Shift+B", L"Alt+B", L"Ctrl+Alt+B"}, sel)) {
                c.overlay.hotkey = keys[sel];
                AppApply();
            }
        }
        if (c.overlay.hotkey == "shift+tab") {
            Rect ctl = Row(ui, y, x, w, Ico::Gamepad, T("ov_games_only"), T("ov_games_only_note"));
            bool v = c.overlay.gamesOnly;
            if (ui.Toggle(Ui::Id("ov_games"), Rect(ctl.r() - 40, ctl.y, 40, ctl.h), v)) { c.overlay.gamesOnly = v; AppApply(); }
        }
        {
            Rect ctl = Row(ui, y, x, w, 0xE7F4, T("ov_corner"));
            const char* keys[] = {"top-left", "top-right", "bottom-left", "bottom-right"};
            int sel = 1;
            for (int i = 0; i < 4; i++) if (c.overlay.corner == keys[i]) sel = i;
            if (ui.Segmented(Ui::Id("ov_corner"), Rect(ctl.r() - 480, ctl.cy() - 16, 480, 32),
                             {T("corner_tl"), T("corner_tr"), T("corner_bl"), T("corner_br")}, sel)) {
                c.overlay.corner = keys[sel];
                AppApply();
            }
        }
        auto sliderRow = [&](const wchar_t* title, wchar_t icon, int& v, int lo, int hi, int sid) {
            Rect ctl = Row(ui, y, x, w, icon, title);
            int t = v;
            if (ui.Slider(sid, Rect(ctl.r() - 300, ctl.cy() - 12, 240, 24), t, lo, hi)) { v = t; AppApply(); }
            ui.Text(std::to_wstring(v) + L"%", Rect(ctl.r() - 50, ctl.y, 50, ctl.h), 13.f, th.text, 600, ALIGN_RIGHT);
        };
        sliderRow(T("ov_opacity"), Ico::Picture, c.overlay.opacity, 0, 100, Ui::Id("ov_op"));
        sliderRow(T("ov_size"), 0xE740, c.overlay.scale, 60, 200, Ui::Id("ov_sz"));
        {
            Rect ctl = Row(ui, y, x, w, 0xE8EC, T("ov_names"));
            bool v = c.overlay.showNames;
            if (ui.Toggle(Ui::Id("ov_names"), Rect(ctl.r() - 40, ctl.y, 40, ctl.h), v)) { c.overlay.showNames = v; AppApply(); }
        }
        ui.alpha = sa;
    }

    void PageGeneral(Ui& ui, float x, float& y, float w) {
        Config& c = AppConfig();
        const Theme& th = ui.th;
        {
            Rect ctl = Row(ui, y, x, w, Ico::Clock, T("gen_interval"));
            const int vals[] = {5, 10, 30, 60, 300};
            int sel = 0;
            for (int i = 0; i < 5; i++) if (c.intervalSec == vals[i]) sel = i;
            wchar_t b[5][32];
            swprintf(b[0], 32, T("every_s"), 5);
            swprintf(b[1], 32, T("every_s"), 10);
            swprintf(b[2], 32, T("every_s"), 30);
            swprintf(b[3], 32, T("every_m"), 1);
            swprintf(b[4], 32, T("every_m"), 5);
            if (ui.Segmented(Ui::Id("seg_int"), Rect(ctl.r() - 400, ctl.cy() - 16, 400, 32), {b[0], b[1], b[2], b[3], b[4]}, sel)) {
                c.intervalSec = vals[sel]; AppApply(); AppRefreshNow();
            }
        }
        auto sliderRow = [&](const wchar_t* title, wchar_t icon, int& v, int lo, int hi, int sid) {
            Rect ctl = Row(ui, y, x, w, icon, title);
            int t = v;
            if (ui.Slider(sid, Rect(ctl.r() - 300, ctl.cy() - 12, 240, 24), t, lo, hi)) { v = t; AppApply(); }
            ui.Text(std::to_wstring(v) + L"%", Rect(ctl.r() - 50, ctl.y, 50, ctl.h), 13.f, th.text, 600, ALIGN_RIGHT);
        };
        sliderRow(T("gen_low"), Ico::Battery, c.lowThreshold, 5, 50, Ui::Id("low"));
        sliderRow(T("gen_mid"), Ico::Battery, c.midThreshold, 20, 80, Ui::Id("mid"));
        if (c.midThreshold < c.lowThreshold) c.midThreshold = c.lowThreshold;

        auto toggleRow = [&](const wchar_t* title, wchar_t icon, bool v, int tid, const std::wstring& desc = L"") -> int {
            Rect ctl = Row(ui, y, x, w, icon, title, desc);
            bool t = v;
            return ui.Toggle(tid, Rect(ctl.r() - 40, ctl.y, 40, ctl.h), t) ? (t ? 1 : 0) : -1;
        };
        int r;
        if ((r = toggleRow(T("gen_notify"), Ico::Bell, c.notifyLow, Ui::Id("notify"))) >= 0) { c.notifyLow = r; AppApply(); }
        if ((r = toggleRow(T("gen_autostart"), 0xE7E8, GetAutostart(), Ui::Id("auto"))) >= 0) {
            bool on = r == 1;
            ui.Defer([on]() { SetAutostart(on); });
        }
        if ((r = toggleRow(T("gen_offline"), Ico::Eye, c.showOffline, Ui::Id("offline"))) >= 0) { c.showOffline = r; AppApply(); }
        {
            Rect ctl = Row(ui, y, x, w, Ico::Globe, T("gen_language"));
            int sel = c.language == L"en" ? 1 : c.language == L"uk" ? 2 : c.language == L"ru" ? 3 : 0;
            if (ui.Segmented(Ui::Id("seg_lang"), Rect(ctl.r() - 420, ctl.cy() - 16, 420, 32),
                             {T("lang_auto"), L"English", L"Українська", L"Русский"}, sel)) {
                const wchar_t* v[] = {L"auto", L"en", L"uk", L"ru"};
                c.language = v[sel];
                AppApply();
            }
        }
        if ((r = toggleRow(T("gen_debug"), Ico::Bug, c.debugLog, Ui::Id("debug"), T("gen_debug_desc"))) >= 0) {
            c.debugLog = r; AppApply();
        }
    }

    void PageAbout(Ui& ui, float x, float& y, float w) {
        const Theme& th = ui.th;
        Rect card(x, y, w, 150);
        ui.Card(0, card);
        Rect tile(x + 24, y + 24, 72, 72);
        ui.Gradient(tile, Mix(th.accent, Rgb(0xFFFFFF), 0.25f), th.accent, 18, false);
        ui.Icon(Ico::Battery, tile, 34, th.onAccent);
        ui.Text(L"BatteryTray", Rect(x + 116, y + 26, w - 140, 34), 24.f, th.text, 700);
        ui.Text(std::wstring(T("about_version")) + L" " + FromUtf8(BT_VERSION), Rect(x + 116, y + 60, w - 140, 20), 12.5f, th.text2);
        ui.Text(T("about_desc"), Rect(x + 116, y + 84, w - 140, 40), 13.f, th.text, 400, ALIGN_LEFT, true);
        ui.Text(T("about_supported"), Rect(x + 24, y + 118, w - 48, 22), 12.f, th.text3);
        y += 162;
        float bx = x;
        auto btn = [&](const wchar_t* label, wchar_t g, int bid) {
            float bw = ui.Measure(label, 13.f) + 54;
            bool c = ui.Button(bid, Rect(bx, y, bw, 36), label, bid == Ui::Id("gh"), g);
            bx += bw + 8;
            return c;
        };
        // opened after the frame: ShellExecute pumps messages and must not run inside painting
        if (btn(T("about_github"), Ico::Link, Ui::Id("gh")))
            ui.Defer([]() { ShellExecuteW(nullptr, L"open", BT_REPO_URL, nullptr, nullptr, SW_SHOWNORMAL); });
        if (btn(T("about_request"), Ico::Add, Ui::Id("req")))
            ui.Defer([]() { ShellExecuteW(nullptr, L"open", BT_REPO_URL L"/issues/new", nullptr, nullptr, SW_SHOWNORMAL); });
        if (btn(T("about_config"), Ico::Folder, Ui::Id("cfg")))
            ui.Defer([]() { ShellExecuteW(nullptr, L"open", ConfigDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); });
        y += 48;
    }

    // ------------------------------------------------------------ frame
    void Frame(Ui& ui, const Rect& client) override {
        const Theme& th = ui.th;
        const float side = 232;

        // sidebar
        float y = 18;
        ui.Icon(Ico::Battery, Rect(20, y, 28, 28), 18, th.accent);
        ui.Text(L"BatteryTray", Rect(52, y, side - 60, 28), 14.f, th.text, 600);
        y += 52;
        struct Nav { const char* key; wchar_t icon; };
        const Nav navs[] = {{"nav_devices", Ico::Mouse}, {"nav_appearance", Ico::Brush}, {"nav_general", Ico::Settings},
                            {"nav_about", Ico::Info}};
        float selY = ui.Anim(Ui::Id("nav_sel"), y + page * 44.f, 16.f);
        for (int i = 0; i < 4; i++) {
            Rect r(12, y + i * 44.f, side - 24, 40);
            int nid = Ui::Id("nav", i);
            if (ui.Interact(nid, r)) SetPage(i);
            float h = ui.Anim(nid, ui.hot == nid ? 1.f : 0.f, 18.f);
            if (i != page) ui.Fill(r, WithA(th.cardHover, th.cardHover.a * h), 6);
        }
        Rect selR(12, selY, side - 24, 40);
        ui.Fill(selR, th.cardHover, 6);
        ui.Fill(Rect(12, selY + 12, 3, 16), th.accent, 1.5f);
        for (int i = 0; i < 4; i++) {
            Rect r(12, y + i * 44.f, side - 24, 40);
            ui.Icon(navs[i].icon, Rect(r.x + 14, r.y, 20, r.h), 15, i == page ? th.accent : th.text);
            ui.Text(T(navs[i].key), Rect(r.x + 46, r.y, r.w - 50, r.h), 13.5f, th.text, i == page ? 600 : 400);
        }

        // content
        Rect area(side, 0, client.w - side, client.h);
        if (ui.Over(area) && ui.wheel != 0) {
            scrollTarget -= ui.wheel * 90.f;
        }
        float maxScroll = contentH - area.h + 30;
        if (maxScroll < 0) maxScroll = 0;
        if (scrollTarget > maxScroll) scrollTarget = maxScroll;
        if (scrollTarget < 0) scrollTarget = 0;
        float scroll = ui.Anim(Ui::Id("scroll"), scrollTarget, 16.f);

        float pa = ui.Anim(Ui::Id("page_anim"), 1.f, 10.f);
        ui.PushClip(area);
        ui.alpha = pa;
        ui.PushOffset(0, -scroll + (1.f - pa) * 18.f);
        const float cx = side + 12, cw = (std::min)(client.w - side - 44, 900.f);
        float cy = 22;
        const char* titles[] = {"nav_devices", "nav_appearance", "nav_general", "nav_about"};
        ui.Text(T(titles[page]), Rect(cx, cy, cw, 40), 28.f, th.text, 600);
        cy += 58;
        float start = cy;
        switch (page) {
        case 0: PageDevices(ui, cx, cy, cw); break;
        case 1: PageAppearance(ui, cx, cy, cw); break;
        case 2: PageGeneral(ui, cx, cy, cw); break;
        default: PageAbout(ui, cx, cy, cw); break;
        }
        contentH = cy - start + 80;
        ui.PopOffset();
        ui.alpha = 1.f;
        ui.PopClip();

        // scrollbar
        if (maxScroll > 1) {
            float vis = area.h / (contentH + 30);
            float bh = (std::max)(40.f, area.h * vis);
            float by = (area.h - bh) * (scroll / maxScroll);
            float hv = ui.Anim(Ui::Id("sb_hot"), ui.Over(Rect(client.w - 16, 0, 16, client.h)) ? 1.f : 0.f, 14.f);
            ui.Fill(Rect(client.w - 7 - hv * 2, by + 4, 3 + hv * 2, bh - 8), WithA(th.text3, 0.6f + hv * 0.3f), 3);
        }
    }

    LRESULT Msg(UINT m, WPARAM wp, LPARAM lp) override {
        if (m == WM_GETMINMAXINFO) {
            auto* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = (LONG)(820 * scale);
            mm->ptMinTrackSize.y = (LONG)(540 * scale);
            return 0;
        }
        return UiWindow::Msg(m, wp, lp);
    }

    void OnClosed() override {
        if (g_settings == this) g_settings = nullptr;
    }
};

} // namespace

void SettingsShow(int page) {
    if (g_settings) {
        g_settings->SetPage(page);
        ShowWindow(g_settings->hwnd, SW_RESTORE);
        SetForegroundWindow(g_settings->hwnd);
        g_settings->Invalidate();
        return;
    }
    UINT dpi = GetDpiForSystem();
    float s = dpi / 96.f;
    int w = (int)(1000 * s), h = (int)(700 * s);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (w > wa.right - wa.left - 40) w = wa.right - wa.left - 40;
    if (h > wa.bottom - wa.top - 40) h = wa.bottom - wa.top - 40;
    int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.top + (wa.bottom - wa.top - h) / 2;
    auto* win = new SettingsWindow();
    win->page = page;
    RECT rc{x, y, x + w, y + h};
    if (!win->Create(L"BatteryTraySettings", WS_OVERLAPPEDWINDOW, 0, T("settings_title"), rc, nullptr, false)) {
        delete win;
        return;
    }
    g_settings = win;
    ShowWindow(win->hwnd, SW_SHOW);
    SetForegroundWindow(win->hwnd);
}

void SettingsClose() {
    if (g_settings) DestroyWindow(g_settings->hwnd);
}
