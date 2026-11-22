#pragma once
// Taskbar widget: one pictogram + percent per device, embedded in the taskbar
// next to the notification area. Each pictogram is its own button.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

enum { WM_WIDGET_MENU = WM_APP + 3, WM_WIDGET_CLICK = WM_APP + 4, WM_WIDGET_REATTACH = WM_APP + 5 };

struct WidgetItem {
    std::string  id;
    wchar_t      glyph = 0;
    bool         customGlyphColor = false;
    COLORREF     glyphColor = 0;
    std::wstring text;           // "87%"
    COLORREF     textColor = 0;
    bool         charging = false;
    bool         dim = false;    // asleep / not connected
    std::wstring tip;
};

void WidgetUpdate(HWND owner, const std::vector<WidgetItem>& items, bool hoverHighlight, bool animations);
void WidgetDestroy();
void WidgetReattach();
bool WidgetItemRect(const std::string& id, RECT* screen);   // for anchoring the flyout
std::string WidgetItemId(int index);
