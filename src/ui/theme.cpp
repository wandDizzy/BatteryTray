#include "theme.h"
#include <cmath>

Color Rgb(unsigned rgb, float a) {
    return Color{((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
}
Color FromCR(COLORREF c, float a) { return Color{GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, a}; }
COLORREF ToCR(Color c) {
    auto b = [](float v) { return (BYTE)lroundf(fminf(fmaxf(v, 0.f), 1.f) * 255); };
    return RGB(b(c.r), b(c.g), b(c.b));
}
Color Mix(Color a, Color b, float t) {
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
Color WithA(Color c, float a) { c.a = a; return c; }

static DWORD RegDword(const wchar_t* key, const wchar_t* name, DWORD def) {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS) return v;
    return def;
}

bool SystemDarkMode() {
    return RegDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", 0) == 0;
}
bool TaskbarLightMode() {
    return RegDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme", 0) != 0;
}
COLORREF SystemAccentColor() {
    DWORD v = RegDword(L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", 0xFFD47800);   // ABGR
    return RGB(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF);
}

const std::vector<AccentPreset>& AccentPresets() {
    static const std::vector<AccentPreset> v = {
        {"blue", 0x3D8BFF}, {"violet", 0x9A6BFF}, {"pink", 0xFF5EA8}, {"red", 0xFF5A5F},
        {"orange", 0xFF9534}, {"yellow", 0xF2C230}, {"green", 0x34C77B}, {"teal", 0x22B8C4},
    };
    return v;
}

bool IsDark(const Config& c) {
    if (c.theme.mode == "dark") return true;
    if (c.theme.mode == "light") return false;
    return SystemDarkMode();
}

static float Luma(Color c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

Theme MakeTheme(const Config& c) {
    Theme t;
    t.dark = IsDark(c);

    Color acc = FromCR(SystemAccentColor());
    if (c.theme.accent != "system") {
        COLORREF cr;
        bool found = false;
        for (auto& p : AccentPresets())
            if (c.theme.accent == p.key) { acc = Rgb(p.rgb); found = true; }
        if (!found && HexToColor(c.theme.accent, &cr)) acc = FromCR(cr);
    }
    // Windows 11 uses a lighter accent on dark surfaces and a deeper one on light surfaces
    if (t.dark && Luma(acc) < 0.55f) acc = Mix(acc, Rgb(0xFFFFFF), 0.30f);
    if (!t.dark && Luma(acc) > 0.6f) acc = Mix(acc, Rgb(0x000000), 0.25f);
    t.accent = acc;
    t.accentHover = Mix(acc, t.dark ? Rgb(0x000000) : Rgb(0xFFFFFF), 0.10f);
    t.onAccent = Luma(acc) > 0.55f ? Rgb(0x000000, 0.9f) : Rgb(0xFFFFFF);

    if (t.dark) {
        t.bg = Rgb(0x1C1C1E);
        t.layer = Rgb(0x1C1C1E, 0.35f);
        t.card = Rgb(0xFFFFFF, 0.050f);
        t.cardHover = Rgb(0xFFFFFF, 0.080f);
        t.cardPress = Rgb(0xFFFFFF, 0.035f);
        t.cardStroke = Rgb(0xFFFFFF, 0.065f);
        t.ctrl = Rgb(0xFFFFFF, 0.065f);
        t.ctrlHover = Rgb(0xFFFFFF, 0.095f);
        t.ctrlPress = Rgb(0xFFFFFF, 0.045f);
        t.ctrlStroke = Rgb(0xFFFFFF, 0.09f);
        t.text = Rgb(0xFFFFFF, 0.96f);
        t.text2 = Rgb(0xFFFFFF, 0.70f);
        t.text3 = Rgb(0xFFFFFF, 0.45f);
        t.divider = Rgb(0xFFFFFF, 0.08f);
        t.shadow = Rgb(0x000000, 0.35f);
    } else {
        t.bg = Rgb(0xF3F3F3);
        t.layer = Rgb(0xF6F6F6, 0.45f);
        t.card = Rgb(0xFFFFFF, 0.72f);
        t.cardHover = Rgb(0xFFFFFF, 0.92f);
        t.cardPress = Rgb(0xFFFFFF, 0.55f);
        t.cardStroke = Rgb(0x000000, 0.07f);
        t.ctrl = Rgb(0xFFFFFF, 0.80f);
        t.ctrlHover = Rgb(0xF9F9F9, 0.95f);
        t.ctrlPress = Rgb(0xF0F0F0, 0.80f);
        t.ctrlStroke = Rgb(0x000000, 0.10f);
        t.text = Rgb(0x000000, 0.90f);
        t.text2 = Rgb(0x000000, 0.62f);
        t.text3 = Rgb(0x000000, 0.42f);
        t.divider = Rgb(0x000000, 0.08f);
        t.shadow = Rgb(0x000000, 0.15f);
    }
    return t;
}
