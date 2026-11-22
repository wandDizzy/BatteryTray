// hidprobe — diagnostic for wireless dongles (battery protocol discovery)
// Read-only: enumerates HID collections, reads FEATURE reports, listens to INPUT reports.
// Never writes anything to devices.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <cstdio>
#include <string>
#include <vector>

static FILE* g_out = nullptr;
static void P(const char* fmt, ...) {
    va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a);
    va_start(a, fmt); vfprintf(g_out, fmt, a); va_end(a);
}
static void Hex(const unsigned char* b, DWORD n) {
    for (DWORD i = 0; i < n; i++) P("%02X ", b[i]);
    P("\n");
}

struct Target { USHORT vid, pid; const char* name; };
static const Target kTargets[] = {
    {0x3151, 0x402D, "Ajazz mouse (3151:402D)"},
    {0x379A, 0x1807, "Hator Skyfall 80 Pro Wireless (379A:1807)"},
    {0x046D, 0x0B1F, "Logitech G733 (046D:0B1F)"},
};

static const char* FindName(USHORT vid, USHORT pid) {
    for (auto& t : kTargets) if (t.vid == vid && t.pid == pid) return t.name;
    return nullptr;
}

static bool IsBatteryUsage(USHORT page, USHORT usage) {
    return (page == 0x06 && usage == 0x20) ||               // Generic Device Controls: Battery Strength
           (page == 0x85 && (usage == 0x65 || usage == 0x66)); // Battery System: SoC / RemainingCapacity
}

static void DumpValueCaps(HIDP_REPORT_TYPE type, const char* tname, PHIDP_PREPARSED_DATA pp,
                          USHORT count, std::vector<UCHAR>& batteryIds) {
    if (!count) return;
    std::vector<HIDP_VALUE_CAPS> vc(count);
    if (HidP_GetValueCaps(type, vc.data(), &count, pp) != HIDP_STATUS_SUCCESS) return;
    for (USHORT i = 0; i < count; i++) {
        USHORT u = vc[i].IsRange ? vc[i].Range.UsageMin : vc[i].NotRange.Usage;
        bool bat = IsBatteryUsage(vc[i].UsagePage, u);
        P("      %s value: reportId=%u page=0x%04X usage=0x%04X bits=%u count=%u%s\n", tname,
          vc[i].ReportID, vc[i].UsagePage, u, vc[i].BitSize, vc[i].ReportCount,
          bat ? "   <<< BATTERY (standard)" : "");
        if (bat && type == HidP_Feature) batteryIds.push_back(vc[i].ReportID);
    }
}

static void ProbeDevice(const wchar_t* path) {
    HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    bool rw = true;
    if (h == INVALID_HANDLE_VALUE) {
        rw = false;
        h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
    }
    HIDD_ATTRIBUTES at{sizeof(at)};
    if (!HidD_GetAttributes(h, &at)) { CloseHandle(h); return; }
    const char* known = FindName(at.VendorID, at.ProductID);
    char nameBuf[32];
    snprintf(nameBuf, sizeof(nameBuf), "%04X:%04X", at.VendorID, at.ProductID);
    const char* name = known ? known : nameBuf;   // every HID device is reported

    PHIDP_PREPARSED_DATA pp = nullptr;
    HIDP_CAPS caps{};
    if (!HidD_GetPreparsedData(h, &pp) || HidP_GetCaps(pp, &caps) != HIDP_STATUS_SUCCESS) {
        CloseHandle(h); return;
    }
    wchar_t prod[128] = L"";
    HidD_GetProductString(h, prod, sizeof(prod));
    char prodA[256];
    WideCharToMultiByte(CP_UTF8, 0, prod, -1, prodA, sizeof(prodA), nullptr, nullptr);
    char pathA[512];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, pathA, sizeof(pathA), nullptr, nullptr);

    P("\n=== %s  ver=%04X  product=\"%s\"\n", name, at.VersionNumber, prodA);
    P("    path: %s\n", pathA);
    P("    access: %s\n", rw ? "read/write" : "NO (system-owned, e.g. keyboard/mouse)");
    P("    top-level: page=0x%04X usage=0x%04X  in=%u out=%u feat=%u bytes\n", caps.UsagePage,
      caps.Usage, caps.InputReportByteLength, caps.OutputReportByteLength,
      caps.FeatureReportByteLength);

    std::vector<UCHAR> batIds;
    DumpValueCaps(HidP_Input, "input  ", pp, caps.NumberInputValueCaps, batIds);
    DumpValueCaps(HidP_Feature, "feature", pp, caps.NumberFeatureValueCaps, batIds);

    // Collect feature report IDs (from value + button caps)
    std::vector<UCHAR> featIds;
    auto addId = [&](UCHAR id) { for (auto x : featIds) if (x == id) return; featIds.push_back(id); };
    if (caps.NumberFeatureValueCaps) {
        USHORT n = caps.NumberFeatureValueCaps; std::vector<HIDP_VALUE_CAPS> v(n);
        if (HidP_GetValueCaps(HidP_Feature, v.data(), &n, pp) == HIDP_STATUS_SUCCESS)
            for (USHORT i = 0; i < n; i++) addId(v[i].ReportID);
    }
    if (caps.NumberFeatureButtonCaps) {
        USHORT n = caps.NumberFeatureButtonCaps; std::vector<HIDP_BUTTON_CAPS> v(n);
        if (HidP_GetButtonCaps(HidP_Feature, v.data(), &n, pp) == HIDP_STATUS_SUCCESS)
            for (USHORT i = 0; i < n; i++) addId(v[i].ReportID);
    }

    if (rw && caps.FeatureReportByteLength) {
        std::vector<unsigned char> buf(caps.FeatureReportByteLength);
        for (UCHAR id : featIds) {
            std::fill(buf.begin(), buf.end(), 0); buf[0] = id;
            if (HidD_GetFeature(h, buf.data(), (ULONG)buf.size())) {
                P("    GetFeature id=0x%02X: ", id); Hex(buf.data(), (DWORD)buf.size());
            } else {
                P("    GetFeature id=0x%02X: failed (err %lu)\n", id, GetLastError());
            }
        }
    }

    // Listen to input reports on vendor-defined collections (page >= 0xFF00)
    if (rw && caps.UsagePage >= 0xFF00 && caps.InputReportByteLength) {
        P("    listening 3s for spontaneous input reports...\n");
        std::vector<unsigned char> buf(caps.InputReportByteLength);
        OVERLAPPED ov{}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD start = GetTickCount(); int got = 0;
        while (GetTickCount() - start < 3000 && got < 30) {
            ResetEvent(ov.hEvent); DWORD rd = 0;
            if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &rd, &ov)) {
                if (GetLastError() != ERROR_IO_PENDING) break;
                DWORD el = GetTickCount() - start; DWORD left = el < 3000 ? 3000 - el : 0;
                if (WaitForSingleObject(ov.hEvent, left) != WAIT_OBJECT_0) { CancelIo(h); break; }
                if (!GetOverlappedResult(h, &ov, &rd, FALSE)) break;
            }
            P("      [%5lu ms] ", GetTickCount() - start); Hex(buf.data(), rd); got++;
        }
        if (!got) P("      (nothing received)\n");
        CloseHandle(ov.hEvent);
    }

    HidD_FreePreparsedData(pp);
    CloseHandle(h);
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    g_out = fopen("hidprobe_report.txt", "w");
    if (!g_out) { printf("cannot create report file\n"); return 1; }
    P("hidprobe v1 — read-only HID diagnostic\n");
    P("Tip: move the mouse / press keys while it listens.\n");

    GUID hidGuid; HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA ifd{sizeof(ifd)};
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        std::vector<BYTE> mem(need);
        auto det = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)mem.data();
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, nullptr))
            ProbeDevice(det->DevicePath);
    }
    SetupDiDestroyDeviceInfoList(set);
    P("\nDone. Report saved to hidprobe_report.txt — send it back.\n");
    fclose(g_out);
    printf("\nPress Enter to exit...");
    getchar();
    return 0;
}
