#pragma once
// Per-device settings panel: shared by the taskbar flyout and the Settings window.
#include "ui.h"

float DevicePanelHeight(const Device& d);
// Draws the panel at r (width matters, height = DevicePanelHeight). Returns the height used.
float DevicePanel(Ui& ui, UiWindow* host, const Rect& r, const Device& d, bool showAllSettings);

std::wstring DeviceStatusText(const Device& d);
std::wstring DeviceSourceText(const Device& d);
Color DeviceLevelColor(const Device& d);
