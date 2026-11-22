#pragma once
// Popup that opens above a pictogram on the taskbar: settings for one device.
#include <string>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

void FlyoutShow(const std::string& deviceId, const RECT& anchor);
void FlyoutClose();
bool FlyoutIsOpenFor(const std::string& deviceId);
