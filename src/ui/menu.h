#pragma once
// Right-click menu (native Windows 11 menu, follows the app theme).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

void ShowMainMenu(HWND owner);
void ApplyMenuTheme(bool dark);
