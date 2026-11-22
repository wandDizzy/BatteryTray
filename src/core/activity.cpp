// Per-device activity, used to tell "asleep" from "switched off".
// Wireless receivers (GearHub: Ajazz, Hator ...) report the same "no link" flag in both cases.
// A mouse or keyboard only falls asleep after a while without use, so if the link drops while the
// device was in use moments ago, it was switched off; if it had been idle for a while, it fell asleep.
// Mouse use = the cursor moved; keyboard use = any other input (keys, clicks, wheel).
// Sampled once a second with GetCursorPos / GetLastInputInfo: practically free, no input hooks.
#include "devices.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <climits>

static std::atomic<DWORD> g_mouseAt{0}, g_keyAt{0};
static POINT g_lastPt{LONG_MIN, LONG_MIN};
static DWORD g_lastInput = 0;

void ActivityTick() {
    DWORD now = GetTickCount();
    POINT pt;
    bool moved = false;
    if (GetCursorPos(&pt)) {
        moved = g_lastPt.x != LONG_MIN && (pt.x != g_lastPt.x || pt.y != g_lastPt.y);
        g_lastPt = pt;
    }
    if (moved) g_mouseAt = now;
    LASTINPUTINFO li{sizeof(li)};
    if (GetLastInputInfo(&li)) {
        if (g_lastInput && li.dwTime != g_lastInput && !moved) g_keyAt = li.dwTime;
        g_lastInput = li.dwTime;
    }
}

// milliseconds since the last use of a device of this type (very large if never seen)
unsigned long MsSinceUse(DevType t) {
    DWORD at = t == DevType::Keyboard ? g_keyAt.load() : g_mouseAt.load();
    if (!at) return 0x7FFFFFFF;
    return GetTickCount() - at;
}
