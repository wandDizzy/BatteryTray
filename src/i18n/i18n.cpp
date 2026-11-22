#include "i18n.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <map>

namespace {

struct Entry { const char* key; const wchar_t* en; const wchar_t* uk; const wchar_t* ru; };

const Entry kTable[] = {
    {"app", L"BatteryTray", L"BatteryTray", L"BatteryTray"},
    {"no_devices", L"No supported devices found", L"Підтримуваних пристроїв не знайдено", L"Поддерживаемые устройства не найдены"},
    {"online", L"Connected", L"На зв'язку", L"На связи"},
    {"asleep", L"Asleep", L"Спить", L"Спит"},
    {"not_connected", L"Not connected", L"Не на зв'язку", L"Не на связи"},
    {"charging", L"Charging", L"Заряджається", L"Заряжается"},
    {"unknown", L"unknown", L"невідомо", L"неизвестно"},
    {"last_known", L"last known level", L"останній відомий рівень", L"последний известный уровень"},
    {"low_title", L"Low battery", L"Низький заряд", L"Низкий заряд"},
    {"low_text", L"%s: %d%%", L"%s: %d%%", L"%s: %d%%"},

    // context menu
    {"menu_hint", L"Click a device to open its settings", L"Натисніть на пристрій, щоб відкрити його налаштування", L"Нажмите на устройство, чтобы открыть его настройки"},
    {"menu_device_settings", L"Device settings…", L"Налаштування пристрою…", L"Настройки устройства…"},
    {"menu_show_device", L"Show on the taskbar", L"Показувати на панелі задач", L"Показывать на панели задач"},
    {"menu_move_left", L"Move left", L"Перемістити ліворуч", L"Переместить влево"},
    {"menu_move_right", L"Move right", L"Перемістити праворуч", L"Переместить вправо"},
    {"menu_show_offline", L"Show disconnected devices", L"Показувати відключені пристрої", L"Показывать отключённые устройства"},
    {"menu_widget", L"Show on the taskbar (icon + percent)", L"Показувати на панелі задач (значок + відсоток)", L"Показывать на панели задач (значок + процент)"},
    {"menu_tray", L"Show as notification area icons", L"Показувати значками в області сповіщень", L"Показывать значками в области уведомлений"},
    {"menu_both", L"Both", L"Обидва варіанти", L"Оба варианта"},
    {"menu_theme", L"Theme", L"Тема", L"Тема"},
    {"menu_language", L"Language", L"Мова", L"Язык"},
    {"menu_interval", L"Check battery every", L"Перевіряти заряд кожні", L"Проверять заряд каждые"},
    {"menu_refresh", L"Refresh now", L"Оновити зараз", L"Обновить сейчас"},
    {"menu_autostart", L"Start with Windows", L"Запускати разом з Windows", L"Запускать вместе с Windows"},
    {"menu_settings", L"Settings…", L"Налаштування…", L"Настройки…"},
    {"menu_config", L"Open config folder", L"Відкрити папку налаштувань", L"Открыть папку настроек"},
    {"menu_request", L"Request support for a device…", L"Запросити підтримку пристрою…", L"Запросить поддержку устройства…"},
    {"menu_exit", L"Exit", L"Вийти", L"Выход"},
    {"every_s", L"%d sec", L"%d с", L"%d сек"},
    {"every_m", L"%d min", L"%d хв", L"%d мин"},

    {"theme_system", L"System", L"Системна", L"Системная"},
    {"theme_dark", L"Dark", L"Темна", L"Тёмная"},
    {"theme_light", L"Light", L"Світла", L"Светлая"},
    {"lang_auto", L"System", L"Системна", L"Системный"},

    // settings window
    {"settings_title", L"BatteryTray settings", L"Налаштування BatteryTray", L"Настройки BatteryTray"},
    {"nav_devices", L"Devices", L"Пристрої", L"Устройства"},
    {"nav_appearance", L"Appearance", L"Вигляд", L"Внешний вид"},
    {"nav_general", L"General", L"Загальні", L"Общие"},
    {"nav_about", L"About", L"Про програму", L"О программе"},
    {"dev_none", L"No devices yet. Plug in a receiver or connect a Bluetooth device.", L"Пристроїв поки немає. Підключіть приймач або Bluetooth-пристрій.", L"Устройств пока нет. Подключите приёмник или Bluetooth-устройство."},
    {"src_dongle", L"2.4 GHz receiver", L"Приймач 2,4 ГГц", L"Приёмник 2,4 ГГц"},
    {"src_hidpp", L"Logitech receiver", L"Приймач Logitech", L"Приёмник Logitech"},
    {"src_bluetooth", L"Bluetooth", L"Bluetooth", L"Bluetooth"},
    {"src_hid", L"USB / HID", L"USB / HID", L"USB / HID"},
    {"src_xinput", L"Xbox / XInput", L"Xbox / XInput", L"Xbox / XInput"},
    {"dev_icon", L"Icon", L"Значок", L"Значок"},
    {"dev_color", L"Icon color", L"Колір значка", L"Цвет значка"},
    {"auto", L"Auto", L"Авто", L"Авто"},
    {"custom", L"Custom", L"Свій", L"Свой"},
    {"dev_show", L"Show on the taskbar", L"Показувати на панелі задач", L"Показывать на панели задач"},
    {"dev_notify", L"Low battery notification", L"Сповіщення про низький заряд", L"Уведомление о низком заряде"},
    {"dev_threshold", L"Notify at", L"Сповіщати при", L"Уведомлять при"},
    {"dev_position", L"Position on the taskbar", L"Позиція на панелі задач", L"Позиция на панели задач"},
    {"dev_all_settings", L"All settings", L"Усі налаштування", L"Все настройки"},
    {"dev_rename_hint", L"Click the name to rename", L"Натисніть на назву, щоб перейменувати", L"Нажмите на название, чтобы переименовать"},
    {"dev_reset_name", L"Reset name", L"Скинути назву", L"Сбросить название"},

    {"ap_theme", L"Theme", L"Тема", L"Тема"},
    {"ap_accent", L"Accent color", L"Акцентний колір", L"Акцентный цвет"},
    {"ap_background", L"Window background", L"Фон вікна", L"Фон окна"},
    {"bg_mica", L"Mica", L"Mica", L"Mica"},
    {"bg_acrylic", L"Acrylic", L"Акрил", L"Акрил"},
    {"bg_solid", L"Solid", L"Суцільний", L"Сплошной"},
    {"bg_image", L"Picture", L"Зображення", L"Картинка"},
    {"ap_choose_image", L"Choose picture…", L"Вибрати зображення…", L"Выбрать картинку…"},
    {"ap_blur", L"Blur", L"Розмиття", L"Размытие"},
    {"ap_dim", L"Darken", L"Затемнення", L"Затемнение"},
    {"ap_display", L"Where to show", L"Де показувати", L"Где показывать"},
    {"disp_widget", L"Taskbar", L"Панель задач", L"Панель задач"},
    {"disp_tray", L"Tray", L"Трей", L"Трей"},
    {"disp_both", L"Both", L"Обидва", L"Оба"},
    {"ap_tray_style", L"Tray icon style", L"Стиль значків у треї", L"Стиль значков в трее"},
    {"style_number", L"Number", L"Число", L"Число"},
    {"style_badge", L"Badge", L"Плашка", L"Плашка"},
    {"style_battery", L"Battery", L"Батарейка", L"Батарейка"},
    {"ap_level_colors", L"Battery level colors", L"Кольори рівня заряду", L"Цвета уровня заряда"},
    {"col_high", L"High", L"Високий", L"Высокий"},
    {"col_mid", L"Medium", L"Середній", L"Средний"},
    {"col_low", L"Low", L"Низький", L"Низкий"},
    {"col_chg", L"Charging", L"Зарядка", L"Зарядка"},
    {"ap_percent", L"Show the % sign", L"Показувати знак %", L"Показывать знак %"},
    {"ap_hover", L"Highlight on hover", L"Підсвічування при наведенні", L"Подсветка при наведении"},
    {"ap_anim", L"Animations", L"Анімації", L"Анимации"},

    {"gen_interval", L"Check battery every", L"Перевіряти заряд кожні", L"Проверять заряд каждые"},
    {"gen_low", L"Low level", L"Низький рівень", L"Низкий уровень"},
    {"gen_mid", L"Medium level", L"Середній рівень", L"Средний уровень"},
    {"gen_notify", L"Low battery notifications", L"Сповіщення про низький заряд", L"Уведомления о низком заряде"},
    {"gen_autostart", L"Start with Windows", L"Запускати разом з Windows", L"Запускать вместе с Windows"},
    {"gen_offline", L"Show disconnected devices", L"Показувати відключені пристрої", L"Показывать отключённые устройства"},
    {"gen_language", L"Language", L"Мова", L"Язык"},
    {"gen_debug", L"Write a debug log", L"Вести журнал налагодження", L"Вести отладочный журнал"},
    {"gen_debug_desc", L"Saves raw device answers to BatteryTray.log next to the program", L"Зберігає відповіді пристроїв у BatteryTray.log поруч із програмою", L"Сохраняет ответы устройств в BatteryTray.log рядом с программой"},

    {"about_desc", L"Battery level of wireless mice, keyboards and headsets right on the taskbar.", L"Заряд бездротових мишок, клавіатур і навушників прямо на панелі задач.", L"Заряд беспроводных мышек, клавиатур и наушников прямо на панели задач."},
    {"about_version", L"Version", L"Версія", L"Версия"},
    {"about_supported", L"Supported: Logitech, Razer, SteelSeries, Corsair, HyperX, Ajazz, Hator and similar receivers, Xbox-compatible and PlayStation gamepads, Bluetooth", L"Підтримка: Logitech, Razer, SteelSeries, Corsair, HyperX, Ajazz, Hator і схожі приймачі, геймпади Xbox-сумісні та PlayStation, Bluetooth", L"Поддержка: Logitech, Razer, SteelSeries, Corsair, HyperX, Ajazz, Hator и похожие приёмники, геймпады Xbox-совместимые и PlayStation, Bluetooth"},
    {"about_github", L"Open on GitHub", L"Відкрити на GitHub", L"Открыть на GitHub"},
    {"about_request", L"Request device support", L"Запросити підтримку пристрою", L"Запросить поддержку устройства"},
    {"about_config", L"Open config folder", L"Відкрити папку налаштувань", L"Открыть папку настроек"},
};

int g_col = 0;   // 0 en, 1 uk, 2 ru
std::wstring g_code = L"en";
std::map<std::string, std::wstring> g_missing;

} // namespace

void SetLanguage(const std::wstring& code) {
    std::wstring c = code;
    if (c.empty() || c == L"auto") {
        LANGID l = PRIMARYLANGID(GetUserDefaultUILanguage());
        c = l == LANG_UKRAINIAN ? L"uk" : l == LANG_RUSSIAN ? L"ru" : L"en";
    }
    g_code = c;
    g_col = c == L"uk" ? 1 : c == L"ru" ? 2 : 0;
}

std::wstring CurrentLanguage() { return g_code; }

const wchar_t* T(const char* key) {
    for (auto& e : kTable) {
        if (strcmp(e.key, key) == 0) return g_col == 1 ? e.uk : g_col == 2 ? e.ru : e.en;
    }
    auto& m = g_missing[key];
    if (m.empty()) m.assign(key, key + strlen(key));
    return m.c_str();
}
