cmake_minimum_required(VERSION 3.16)
project(BatteryTray VERSION 2.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(BatteryTray WIN32 ${SOURCES} res/app.rc)

target_compile_definitions(BatteryTray PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
                           _WIN32_WINNT=0x0A00 WINVER=0x0A00 _CRT_SECURE_NO_WARNINGS)
target_link_libraries(BatteryTray PRIVATE hid setupapi comctl32 comdlg32 shell32 ole32 uuid gdi32 user32
                      advapi32 d2d1 dwrite dwmapi uxtheme windowscodecs)

if(MSVC)
  target_compile_options(BatteryTray PRIVATE /W3 /utf-8 /MP)
  # static CRT: the exe runs on any Windows 10/11 without installing anything
  set_property(TARGET BatteryTray PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_link_options(BatteryTray PRIVATE /MANIFEST:NO)
else()
  target_compile_options(BatteryTray PRIVATE -municode)
  target_link_options(BatteryTray PRIVATE -municode -static -s)
endif()

# diagnostic tool for adding new devices
add_executable(hidprobe tools/hidprobe/hidprobe.cpp)
target_compile_definitions(hidprobe PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(hidprobe PRIVATE hid setupapi)
if(MSVC)
  target_compile_options(hidprobe PRIVATE /utf-8)
  set_property(TARGET hidprobe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_link_options(hidprobe PRIVATE -static -s)
endif()
