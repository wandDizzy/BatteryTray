# Adding a device

## 1. Run hidprobe

`hidprobe.exe` (in Releases, source in `tools/hidprobe`) only **reads** from devices. Run it with the
receiver plugged in and the device switched on, wait for `Done`, and look at `hidprobe_report.txt`.

## 2. Vendor receivers with a status block (Ajazz, Hator and similar)

Most of these are **detected automatically** (battery = first non-zero byte at index 2 or 3,
idle flag two bytes later). Add an entry below only if the automatic guess is wrong.

If the receiver has a collection with `page=0xFFFF usage=0x0002 feat=65` and `GetFeature id=0x00`
returns something like

```
00 00 00 64 01 00 01 02 ...      <- 0x64 = 100 %
```

find the byte that equals the battery level (compare with the vendor app), then switch the device off
and run hidprobe again: the byte that changes is the "idle" flag. Add the receiver to `config.json`:

```json
"customDongles": [
  {
    "vid": "3151", "pid": "402D",
    "name": "My mouse", "type": "mouse",
    "batteryByte": 3,
    "idleByte": 5, "idleValue": 1,
    "chargeByte": -1, "chargeValue": 1
  }
]
```

Byte indexes count from 0 (the first `00` is the report ID). `type`: `mouse`, `keyboard`, `headset`, `other`.

## 3. Anything else

Open an issue with the hidprobe report and the device model. If the vendor app shows the battery,
a USB capture (Wireshark + USBPcap) of the app reading it is the fastest way to add support.
