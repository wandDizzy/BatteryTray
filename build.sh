#!/bin/sh
# Cross-build on Linux with MinGW-w64 (or inside MSYS2 on Windows):  ./build.sh
set -e
CXX=${CXX:-x86_64-w64-mingw32-g++}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
mkdir -p build
$WINDRES -I res res/app.rc -O coff -o build/app.res
$CXX -std=c++17 -O2 -s -municode -mwindows -static \
  -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
  src/*.cpp src/core/*.cpp src/config/*.cpp src/i18n/*.cpp src/ui/*.cpp build/app.res \
  -o build/BatteryTray.exe \
  -lhid -lsetupapi -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luser32 -ladvapi32 \
  -ld2d1 -ldwrite -ldwmapi -luxtheme -lwindowscodecs
ls -la build/BatteryTray.exe
