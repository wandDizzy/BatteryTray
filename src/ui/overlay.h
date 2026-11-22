#pragma once
// Game overlay: a click-through, always-on-top panel in a screen corner.
// Works over windowed and borderless-fullscreen games (not exclusive fullscreen).
#include "../config/config.h"
#include <string>
#include <vector>

struct OverlayItem {
    wchar_t      glyph = 0;
    bool         customGlyphColor = false;
    COLORREF     glyphColor = 0;
    std::wstring name;
    int          battery = -1;
    COLORREF     color = 0;
    bool         charging = false;
    bool         dim = false;
    bool         low = false;
};

void OverlayUpdate(const std::vector<OverlayItem>& items, const Config& c);
void OverlayToggle();      // Ctrl+Shift+B
void OverlayDestroy();
