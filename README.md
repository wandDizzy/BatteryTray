# BatteryTray

Battery level of wireless **mice, keyboards and headsets** right on the Windows 11 taskbar —
one pictogram + percent per device, next to the clock.

*Заряд беспроводных мышек, клавиатур и наушников прямо на панели задач Windows 11.
Русское описание — ниже.*

- Native C++ / Win32, ~3–6 MB of RAM, no runtime to install
- Each device is its own pictogram: **left click** opens that device's panel
  (rename, icon, color, notifications, order), **right click** opens the menu
- Windows 11 look: Mica / Acrylic, dark & light themes, accent colors, your own background picture, animations
- Low-battery notifications, start with Windows, languages: English, Українська, Русский
- Settings in a readable `config.json`

## Supported devices

| Connection | How the level is read | Devices |
|---|---|---|
| 2.4 GHz receiver | GearHub status request (F7) on the receiver (exact % from the device) | Ajazz (`3151:402D`), Hator (`379A:1807`) and **other receivers on the same chips — detected automatically** |
| Logitech receiver / headset | HID++ 2.0 (Unified Battery, Battery Status, voltage) | Lightspeed / Unifying mice & keyboards, G-series headsets (G733 …) |
| Razer receiver / cable | Razer 90-byte report protocol (as documented by OpenRazer) | wireless Razer mice and keyboards (HyperSpeed) |
| SteelSeries base / receiver | vendor status request | Arctis 1 / 7 / 7X / 7P / 7+ / 9 / Pro, Arctis Nova 3P / 5 / 7 / Pro Wireless |
| Corsair receiver | vendor status request | VOID / VOID PRO / HS70 / HS80 wireless family |
| HyperX receiver | vendor status request | Cloud II Wireless, Cloud Alpha Wireless |
| SteelSeries mouse (receiver / cable) | vendor battery request (as documented by rivalcfg) | Aerox 3 / 5 / 9 Wireless, Prime Wireless, Rival 3 Wireless, Rival 650 |
| Gamepads | XInput (Windows, 4 steps) / Sony input reports | Xbox and Xbox-compatible wireless pads, DualShock 4, DualSense |
| Bluetooth | the value Windows itself stores | any device that reports battery to Windows |
| USB / HID | standard HID "Battery Strength" usage | devices that expose it |

Devices are found automatically when they are plugged in — nothing to set up. Your device still not detected? See [docs/adding-devices.md](docs/adding-devices.md) — often it can be added
in `config.json` without rebuilding.

## Download

Get `BatteryTray.exe` from **Releases** and run it. No installer.

## Build

Requirements: Windows 10/11, **CMake** and either **Visual Studio Build Tools** (C++ workload) or **MSYS2 MinGW-w64**.

- VS Code: open the folder, install the recommended extensions, press **Ctrl+Shift+B**
- Command line: `build.bat` (Visual Studio) or `./build.sh` (MSYS2 / Linux cross-compile)

## Project layout

```
src/
  main.cpp            app state, polling thread, message loop
  app.h               API shared by all UI parts
  core/               device protocols: dongle.cpp, hidpp.cpp (Logitech), bluetooth.cpp, hid_util
  config/             config.json (json.cpp is a tiny parser/writer)
  i18n/               translations (add a column to add a language)
  ui/
    ui.*              small Direct2D UI kit: controls, animations, Mica/Acrylic windows
    theme.*           colors, dark/light, accent
    widget.*          the taskbar widget
    flyout.*          popup over a pictogram
    devpanel.*        per-device settings panel
    settings_window.* the settings window
    menu.*            right-click menu
    tray_icons.*      notification area icons (optional mode)
res/                  icon, manifest, version info
tools/hidprobe/       diagnostic tool for adding new devices
```

## Credits

Headset protocols were reverse engineered by [HeadsetControl](https://github.com/Sapd/HeadsetControl),
the Razer report layout is documented by [OpenRazer](https://github.com/openrazer/openrazer),
SteelSeries mouse commands by [rivalcfg](https://github.com/flozz/rivalcfg),
the GearHub receiver status request (Ajazz, Hator, Attack Shark …) by [Joule](https://github.com/DillanCidral/Joule).
BatteryTray contains its own implementation of these protocols.

## License

MIT

---

## По-русски

**BatteryTray** показывает заряд беспроводных устройств прямо на панели задач, рядом с часами.
Каждое устройство — отдельная пиктограмма с процентом:

- **левый клик** — панель этого устройства: переименовать, выбрать значок и цвет, уведомления, порядок;
- устройства находятся автоматически при подключении приёмника — ничего настраивать не нужно;
- **правый клик** — меню: устройства, режим отображения, тема, язык, интервал опроса, автозапуск;
- окно настроек: темы (системная / тёмная / светлая), акцентный цвет, фон Mica / акрил / картинка, анимации.

Настройки лежат в `%APPDATA%\BatteryTray\config.json`. Если положить `config.json` рядом с exe,
программа будет хранить настройки там (портативный режим).
