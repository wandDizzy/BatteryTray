#pragma once
// Colors and themes (dark / light, accent color, Windows 11 look).
#include "../config/config.h"
#include <vector>

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};

Color Rgb(unsigned rgb, float a = 1.f);       // 0xRRGGBB
Color FromCR(COLORREF c, float a = 1.f);
COLORREF ToCR(Color c);
Color Mix(Color a, Color b, float t);
Color WithA(Color c, float a);

struct Theme {
    bool  dark = true;
    Color accent, accentHover, onAccent;
    Color bg;           // window background when no backdrop is available
    Color layer;        // tint over the backdrop
    Color card, cardHover, cardPress, cardStroke;
    Color ctrl, ctrlHover, ctrlPress, ctrlStroke;
    Color text, text2, text3;
    Color divider;
    Color shadow;
};

bool  SystemDarkMode();          // "apps" mode in Windows settings
bool  TaskbarLightMode();        // "system" mode (taskbar)
COLORREF SystemAccentColor();
bool  IsDark(const Config& c);
Theme MakeTheme(const Config& c);

struct AccentPreset { const char* key; unsigned rgb; };
const std::vector<AccentPreset>& AccentPresets();
