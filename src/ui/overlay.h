#pragma once
// Game overlay: a click-through, always-on-top panel. By hotkey it opens big at the top center
// (like the Steam / Discord overlay); "always" / "low battery" keep a small widget in a corner.
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
    bool         offline = false;
    bool         strike = false;
    bool         low = false;
};

void OverlayUpdate(const std::vector<OverlayItem>& items, const Config& c);
void OverlayToggle();      // the overlay hotkey
void OverlayPreview();     // show it for 5 s (settings button), even when it is off
void OverlayDestroy();
