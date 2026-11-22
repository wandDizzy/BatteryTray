#pragma once
#include "../config/config.h"
#include <vector>

int   TrayIconSize();
HICON RenderDeviceIcon(const Device& d, const Config& c, int size);
COLORREF LevelColor(const Device& d, const Config& c);
HICON RenderAppIcon(int size);   // generic icon (placeholder / widget mode)
