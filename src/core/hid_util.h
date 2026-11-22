#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

struct HidIface {
    std::wstring   path;
    std::wstring   product;
    unsigned short vid = 0, pid = 0;
    unsigned short usagePage = 0, usage = 0;
    unsigned short inLen = 0, outLen = 0, featLen = 0;
    unsigned char  inReportId = 0;          // report ID of the first input report (0 = none)
    // Standard battery usage found in a FEATURE report (0 = none)
    bool           hasStdBattery = false;
    unsigned char  stdBatteryReportId = 0;
    unsigned short stdBatteryPage = 0, stdBatteryUsage = 0;
    long           stdBatteryMin = 0, stdBatteryMax = 100;
};

// Enumerates present HID collections. Opens them with zero access, so it
// also works for system-owned mice/keyboards.
std::vector<HidIface> EnumHid();

HANDLE OpenHidRW(const std::wstring& path, bool overlapped);

std::string HexId(unsigned short v);

int InterfaceNumber(const std::wstring& path);   // "mi_03" -> 3, -1 if the path has none

// Simple request/response over a vendor collection:
// writes an OUTPUT report (req[0] = report ID) and reads INPUT reports.
// Responses are normalized like hidapi: a leading report ID of 0 is removed.
struct HidPort {
    HANDLE h = INVALID_HANDLE_VALUE;
    HANDLE ev = nullptr;
    int inLen = 0, outLen = 0;
    bool Open(const HidIface& f);
    void Close();
    bool Write(const std::vector<unsigned char>& req);
    int  Read(std::vector<unsigned char>& out, DWORD timeoutMs);   // bytes, 0 = timeout, -1 = error
    ~HidPort() { Close(); }
};

// Debug log: active only when a file named "BatteryTray.debug" sits next to the exe.
// Output goes to "BatteryTray.log" in the same folder.
void DbgLog(const char* fmt, ...);
void SetDebugLog(bool on);   // force logging (from config); a "BatteryTray.debug" file next to the exe also enables it
std::string HexDump(const unsigned char* b, int n);
