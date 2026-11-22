@echo off
rem Build with Visual Studio Build Tools (or Visual Studio) + CMake.
rem Run from "x64 Native Tools Command Prompt" or just double-click if CMake finds Visual Studio.
cmake -S . -B build -A x64 || goto :err
cmake --build build --config Release || goto :err
copy /Y build\Release\BatteryTray.exe BatteryTray.exe >nul
echo.
echo Done: BatteryTray.exe
goto :eof
:err
echo Build failed.
exit /b 1
