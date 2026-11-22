// Vendor 2.4 GHz dongles (Ajazz, Hator and others on the same chip family)
// and devices that expose the standard HID "Battery Strength" usage.
#include "devices.h"
#include "hid_util.h"
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <map>
#include <mutex>
#include <cwctype>

static std::vector<DongleSpec> g_custom;
static std::mutex g_customMx;

const std::vector<DongleSpec>& BuiltinDongles() {
    static const std::vector<DongleSpec> v = [] {
        std::vector<DongleSpec> t;
        // GearHub receivers (status via command F7, see below). Verified on real hardware:
        //   Ajazz mouse:     00 00 00 64 01 00 ..  -> mouse 100 %, keyboard slot empty
        //   Hator keyboard:  00 00 64 00 00 01 ..  -> keyboard 100 %, mouse slot empty
        // Off flag = 1 means the receiver has no link to the device (switched off or out of range);
        // the receiver then keeps the last known level, which we show as "not connected".
        DongleSpec a; a.vid = 0x3151; a.pid = 0x402D; a.name = L"AJAZZ AJ159 PRO"; a.type = DevType::Mouse;
        a.battByte = -1; t.push_back(a);
        DongleSpec h; h.vid = 0x379A; h.pid = 0x1807; h.name = L"HATOR Skyfall 80 Pro Wireless"; h.type = DevType::Keyboard;
        h.battByte = -1; t.push_back(h);
        return t;
    }();
    return v;
}

void SetCustomDongles(const std::vector<DongleSpec>& specs) {
    std::lock_guard<std::mutex> lk(g_customMx);
    g_custom = specs;
}

static const DongleSpec* FindSpec(unsigned short vid, unsigned short pid, std::vector<DongleSpec>& custom) {
    for (auto& s : custom) if (s.vid == vid && s.pid == pid) return &s;   // user entries override built-ins
    for (auto& s : BuiltinDongles()) if (s.vid == vid && s.pid == pid) return &s;
    return nullptr;
}

static DevType TypeFromUsage(unsigned short page, unsigned short usage) {
    if (page == 0x01 && usage == 0x02) return DevType::Mouse;
    if (page == 0x01 && usage == 0x06) return DevType::Keyboard;
    return DevType::Other;
}

static bool PathHas(const std::wstring& path, const wchar_t* part) {
    std::wstring p = path;
    for (auto& c : p) c = towlower(c);
    return p.find(part) != std::wstring::npos;
}

// GearHub-family receivers (shared by many Chinese brands: Ajazz, Hator, Attack Shark, Lingbao ...).
// After a feature report with command F7 the receiver returns its status:
//   [0] report ID, [1] ready, [2] keyboard %, [3] mouse %, [4] keyboard off, [5] mouse off
// Layout and request as documented by the Joule project; verified here on 3151:402D and 379A:1807.
static bool IsGearHub(unsigned short vid) { return vid == 0x3151 || vid == 0x379A; }

static bool IsGearHubStatus(const std::vector<unsigned char>& b) {
    if (b.size() < 7) return false;
    if (b[1] > 1 || b[2] > 100 || b[3] > 100 || b[4] > 1 || b[5] > 1 || b[6] > 1) return false;
    for (int i = 1; i <= 6; i++) if (b[i]) return true;   // 64 zeros is "no answer", not a state
    return false;
}

void PollDongles(std::vector<Device>& out) {
    std::vector<DongleSpec> custom;
    { std::lock_guard<std::mutex> lk(g_customMx); custom = g_custom; }

    auto ifaces = EnumHid();
    std::map<std::string, bool> seen;

    for (auto& f : ifaces) {
        // 1) Vendor receiver with a status block (GearHub family: Ajazz, Hator, Attack Shark, ...)
        if (f.usagePage == 0xFFFF && f.usage == 0x0002 && f.featLen >= 9) {
            std::string base = "dongle:" + HexId(f.vid) + ":" + HexId(f.pid);
            if (seen[base]) continue;
            const DongleSpec* s = FindSpec(f.vid, f.pid, custom);
            bool gearhub = IsGearHub(f.vid) || (s && s->battByte < 0);
            HANDLE h = OpenHidRW(f.path, false);
            if (h == INVALID_HANDLE_VALUE) continue;
            std::vector<unsigned char> buf(f.featLen, 0);
            bool ok = false;
            if (gearhub) {
                // ask the receiver for its status (command F7), then read the answer
                std::vector<unsigned char> req(f.featLen, 0);
                req[1] = 0xF7;
                if (HidD_SetFeature(h, req.data(), (ULONG)req.size())) {
                    for (int i = 0; i < 4 && !ok; i++) {
                        Sleep(30);
                        std::fill(buf.begin(), buf.end(), 0);
                        ok = HidD_GetFeature(h, buf.data(), (ULONG)buf.size()) && IsGearHubStatus(buf);
                    }
                }
            } else {
                ok = HidD_GetFeature(h, buf.data(), (ULONG)buf.size()) != FALSE;
            }
            CloseHandle(h);
            DbgLog("dongle %04X:%04X %s %s", f.vid, f.pid, gearhub ? "F7" : "raw", HexDump(buf.data(), 16).c_str());
            if (!ok) continue;
            seen[base] = true;

            if (gearhub) {
                // [2] keyboard %, [3] mouse %, [4]/[5] = 1 when keyboard/mouse is off (report ID at [0])
                const DongleSpec* named = s;
                struct Slot { int pct, off; DevType type; const char* suffix; };
                const Slot slots[2] = {{buf[3], buf[5], DevType::Mouse, ":m"}, {buf[2], buf[4], DevType::Keyboard, ":k"}};
                for (auto& sl : slots) {
                    if (sl.pct == 0 || sl.pct > 100) continue;   // no such device paired
                    Device d;
                    d.id = base + (named && named->type == sl.type ? "" : sl.suffix);
                    if (named && named->type == sl.type && !named->name.empty()) d.name = named->name;
                    else {
                        std::wstring brand = f.product;
                        size_t sp = brand.find(L' ');
                        if (sp != std::wstring::npos) brand = brand.substr(0, sp);
                        d.name = (brand.empty() ? L"2.4G" : brand) + (sl.type == DevType::Mouse ? L" Mouse" : L" Keyboard");
                    }
                    d.type = sl.type;
                    d.source = "dongle";
                    d.battery = sl.pct;
                    // "no link": asleep if the device had been idle for a while, otherwise switched off.
                    // Decided once when the link drops and kept until it comes back. Measured on real
                    // hardware: an Ajazz AJ159 falls asleep ~20 s after the last move, and the poll sees
                    // it up to one interval later, so 15 s of idle can only be sleep.
                    static std::map<std::string, int> linkState;   // 0 online, 1 asleep, 2 off
                    int& stt = linkState[d.id];
                    int prev = stt;
                    if (sl.off == 0) stt = 0;
                    else if (stt == 0) stt = MsSinceUse(sl.type) >= 15000 ? 1 : 2;
                    if (stt != prev)
                        DbgLog("dongle %s link=%s idle_ms=%lu", d.id.c_str(), stt == 0 ? "ok" : stt == 1 ? "asleep" : "off",
                               (unsigned long)MsSinceUse(sl.type));
                    d.online = stt != 2;
                    d.idle = stt == 1;
                    out.push_back(d);
                }
                continue;
            }

            // custom layout from config.json
            if (!s || s->battByte >= (int)buf.size()) continue;
            Device d;
            d.id = base;
            d.name = s->name.empty() ? f.product : s->name;
            d.type = s->type;
            d.source = "dongle";
            int b = buf[s->battByte];
            d.battery = (b >= 0 && b <= 100) ? b : -1;
            d.online = true;
            if (s->idleByte >= 0 && s->idleByte < (int)buf.size()) d.idle = buf[s->idleByte] == s->idleValue;
            if (s->chargeByte >= 0 && s->chargeByte < (int)buf.size()) d.charging = buf[s->chargeByte] == s->chargeValue;
            out.push_back(d);
            continue;
        }

        // 2) Any device with the standard HID battery usage in a feature report
        if (f.hasStdBattery) {
            std::string id = "hid:" + HexId(f.vid) + ":" + HexId(f.pid);
            if (seen[id]) continue;
            HANDLE h = OpenHidRW(f.path, false);
            if (h == INVALID_HANDLE_VALUE) continue;
            PHIDP_PREPARSED_DATA pp = nullptr;
            if (HidD_GetPreparsedData(h, &pp)) {
                std::vector<char> buf(f.featLen, 0);
                buf[0] = (char)f.stdBatteryReportId;
                ULONG val = 0;
                if (HidD_GetFeature(h, buf.data(), (ULONG)buf.size()) &&
                    HidP_GetUsageValue(HidP_Feature, f.stdBatteryPage, 0, f.stdBatteryUsage, &val, pp,
                                       buf.data(), (ULONG)buf.size()) == HIDP_STATUS_SUCCESS) {
                    Device d;
                    d.id = id;
                    d.name = f.product.empty() ? L"HID device" : f.product;
                    d.type = TypeFromUsage(f.usagePage, f.usage);
                    d.source = "hid";
                    long range = f.stdBatteryMax - f.stdBatteryMin;
                    long pct = range > 0 ? ((long)val - f.stdBatteryMin) * 100 / range : (long)val;
                    d.battery = pct < 0 ? 0 : pct > 100 ? 100 : (int)pct;
                    d.online = true;
                    out.push_back(d);
                    seen[id] = true;
                }
                HidD_FreePreparsedData(pp);
            }
            CloseHandle(h);
        }
    }
}
