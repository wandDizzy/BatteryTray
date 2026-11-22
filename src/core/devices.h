#pragma once
#include <string>
#include <vector>

enum class DevType { Other = 0, Mouse, Keyboard, Headset, Gamepad };

struct Device {
    std::string  id;            // stable key, e.g. "dongle:3151:402D", "hidpp:046D:0B1F:FF", "bt:{guid}"
    std::wstring name;
    DevType      type = DevType::Other;
    int          battery = -1;  // 0..100, -1 = unknown
    bool         online = false;
    bool         idle = false;      // dongle says the device is asleep or switched off (last level is kept)
    bool         charging = false;
    bool         wired = false;     // connected with a cable (the receiver has no radio link then)
    std::string  source;        // "dongle" | "hid" | "hidpp" | "bluetooth"
};

// Vendor dongles that expose a status block via a 65-byte FEATURE report
// on a vendor collection (usage page 0xFFFF, usage 0x0002).
// Byte indexes count the report-ID byte as index 0.
struct DongleSpec {
    unsigned short vid = 0, pid = 0;
    std::wstring   name;
    DevType        type = DevType::Other;
    int battByte = -1;
    int idleByte = -1, idleValue = 1;
    int chargeByte = -1, chargeValue = 1;
};

// Built-in table + user-defined entries from config.json
const std::vector<DongleSpec>& BuiltinDongles();
void SetCustomDongles(const std::vector<DongleSpec>& specs);

void PollDongles(std::vector<Device>& out);    // vendor dongles + standard HID battery usage
void PollHidpp(std::vector<Device>& out);      // Logitech HID++ 2.0
void PollBluetooth(std::vector<Device>& out);  // Windows Bluetooth battery property
void PollRazer(std::vector<Device>& out);      // Razer mice / keyboards (90-byte feature report protocol)
void PollHeadsets(std::vector<Device>& out);   // SteelSeries, Corsair, HyperX headsets
void PollEvision(std::vector<Device>& out);    // keyboards on the Evision receiver (Epomaker, VGN, VXE ...)
void PollAsus(std::vector<Device>& out);       // ASUS ROG / TUF mice and keyboards
void PollGamepads(std::vector<Device>& out);   // XInput (Xbox-compatible) and Sony DualShock 4 / DualSense

// activity.cpp: tells a sleeping device from a switched-off one
void  ActivityTick();              // call about once a second
void  ActivityKey();               // a keyboard event arrived (raw input)
unsigned long MsSinceUse(DevType t);       // ms since the user last used a mouse / keyboard
