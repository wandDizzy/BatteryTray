#include "hid_util.h"
#include <cwctype>
#include <cstring>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <cstdio>
#include <cstdarg>

std::string HexId(unsigned short v) {
    char b[8];
    snprintf(b, sizeof(b), "%04X", v);
    return b;
}

HANDLE OpenHidRW(const std::wstring& path, bool overlapped) {
    return CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                       overlapped ? FILE_FLAG_OVERLAPPED : 0, nullptr);
}

static bool IsBatteryUsage(USHORT page, USHORT usage) {
    return (page == 0x06 && usage == 0x20) ||                 // Generic Device Controls: Battery Strength
           (page == 0x85 && (usage == 0x65 || usage == 0x66)); // Battery System: AbsoluteSoC / RemainingCapacity
}

std::vector<HidIface> EnumHid() {
    std::vector<HidIface> out;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;

    SP_DEVICE_INTERFACE_DATA ifd{sizeof(ifd)};
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        if (!need) continue;
        std::vector<BYTE> mem(need);
        auto det = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(mem.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, nullptr)) continue;

        HANDLE h = CreateFileW(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        HidIface f;
        f.path = det->DevicePath;
        HIDD_ATTRIBUTES at{sizeof(at)};
        PHIDP_PREPARSED_DATA pp = nullptr;
        HIDP_CAPS caps{};
        if (HidD_GetAttributes(h, &at) && HidD_GetPreparsedData(h, &pp)) {
            if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                f.vid = at.VendorID;
                f.pid = at.ProductID;
                f.usagePage = caps.UsagePage;
                f.usage = caps.Usage;
                f.inLen = caps.InputReportByteLength;
                f.outLen = caps.OutputReportByteLength;
                f.featLen = caps.FeatureReportByteLength;

                if (caps.NumberInputValueCaps) {
                    USHORT ni = caps.NumberInputValueCaps;
                    std::vector<HIDP_VALUE_CAPS> iv(ni);
                    if (HidP_GetValueCaps(HidP_Input, iv.data(), &ni, pp) == HIDP_STATUS_SUCCESS && ni)
                        f.inReportId = iv[0].ReportID;
                }
                USHORT n = caps.NumberFeatureValueCaps;
                if (n) {
                    std::vector<HIDP_VALUE_CAPS> vc(n);
                    if (HidP_GetValueCaps(HidP_Feature, vc.data(), &n, pp) == HIDP_STATUS_SUCCESS) {
                        for (USHORT k = 0; k < n; k++) {
                            USHORT u = vc[k].IsRange ? vc[k].Range.UsageMin : vc[k].NotRange.Usage;
                            if (IsBatteryUsage(vc[k].UsagePage, u)) {
                                f.hasStdBattery = true;
                                f.stdBatteryReportId = vc[k].ReportID;
                                f.stdBatteryPage = vc[k].UsagePage;
                                f.stdBatteryUsage = u;
                                f.stdBatteryMin = vc[k].LogicalMin;
                                f.stdBatteryMax = vc[k].LogicalMax > vc[k].LogicalMin ? vc[k].LogicalMax : 100;
                                break;
                            }
                        }
                    }
                }
                wchar_t prod[128] = L"";
                if (HidD_GetProductString(h, prod, sizeof(prod))) f.product = prod;
                out.push_back(std::move(f));
            }
            HidD_FreePreparsedData(pp);
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

static int g_dbg = -1;
static bool g_dbgForce = false;

void SetDebugLog(bool on) { g_dbgForce = on; }
static std::wstring g_logPath;

static bool DbgEnabled() {
    if (g_dbg < 0) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir(exe);
        dir.erase(dir.find_last_of(L'\\') + 1);
        g_logPath = dir + L"BatteryTray.log";
        g_dbg = GetFileAttributesW((dir + L"BatteryTray.debug").c_str()) != INVALID_FILE_ATTRIBUTES;
        if (g_dbg) {   // keep the log small
            WIN32_FILE_ATTRIBUTE_DATA fa;
            if (GetFileAttributesExW(g_logPath.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 512 * 1024)
                DeleteFileW(g_logPath.c_str());
        }
    }
    return g_dbg == 1 || g_dbgForce;
}

void DbgLog(const char* fmt, ...) {
    if (!DbgEnabled()) return;
    FILE* f = _wfopen(g_logPath.c_str(), L"a");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a;
    va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fputc('\n', f);
    fclose(f);
}

std::string HexDump(const unsigned char* b, int n) {
    std::string s;
    char t[4];
    for (int i = 0; i < n; i++) { snprintf(t, sizeof(t), "%02X ", b[i]); s += t; }
    return s;
}

int InterfaceNumber(const std::wstring& path) {
    std::wstring p = path;
    for (auto& c : p) c = (wchar_t)towlower(c);
    size_t i = p.find(L"&mi_");
    if (i == std::wstring::npos) i = p.find(L"#mi_");
    if (i == std::wstring::npos || i + 6 > p.size()) return -1;
    return (int)wcstol(p.substr(i + 4, 2).c_str(), nullptr, 16);
}

bool HidPort::Open(const HidIface& f) {
    Close();
    h = OpenHidRW(f.path, true);
    if (h == INVALID_HANDLE_VALUE) return false;
    ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    inLen = f.inLen;
    outLen = f.outLen;
    return true;
}

void HidPort::Close() {
    if (h != INVALID_HANDLE_VALUE) { CancelIo(h); CloseHandle(h); h = INVALID_HANDLE_VALUE; }
    if (ev) { CloseHandle(ev); ev = nullptr; }
}

bool HidPort::Write(const std::vector<unsigned char>& req) {
    if (h == INVALID_HANDLE_VALUE || req.empty()) return false;
    size_t n = outLen > 0 ? (size_t)outLen : req.size();
    std::vector<unsigned char> buf(n > req.size() ? n : req.size(), 0);
    memcpy(buf.data(), req.data(), req.size());
    if (outLen > 0) buf.resize((size_t)outLen);
    OVERLAPPED ov{};
    ov.hEvent = ev;
    ResetEvent(ev);
    DWORD wr = 0;
    if (WriteFile(h, buf.data(), (DWORD)buf.size(), &wr, &ov)) return true;
    if (GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ev, 400) == WAIT_OBJECT_0 && GetOverlappedResult(h, &ov, &wr, FALSE)) return true;
        CancelIo(h);
        GetOverlappedResult(h, &ov, &wr, TRUE);
    }
    // collections without an interrupt OUT endpoint: send through the control pipe
    return HidD_SetOutputReport(h, buf.data(), (ULONG)buf.size()) != FALSE;
}

int HidPort::Read(std::vector<unsigned char>& out, DWORD timeoutMs) {
    if (h == INVALID_HANDLE_VALUE || inLen <= 0) return -1;
    std::vector<unsigned char> buf((size_t)inLen, 0);
    OVERLAPPED ov{};
    ov.hEvent = ev;
    ResetEvent(ev);
    DWORD rd = 0;
    if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &rd, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return -1;
        if (WaitForSingleObject(ev, timeoutMs) != WAIT_OBJECT_0) {
            CancelIo(h);
            GetOverlappedResult(h, &ov, &rd, TRUE);
            return 0;
        }
        if (!GetOverlappedResult(h, &ov, &rd, FALSE)) return -1;
    }
    size_t start = (rd > 0 && buf[0] == 0) ? 1 : 0;
    out.assign(buf.begin() + start, buf.begin() + rd);
    return (int)out.size();
}
