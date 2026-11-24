@echo off
setlocal
cd /d "%~dp0"
if not defined MSYS2_BIN set "MSYS2_BIN=D:\C++\MSYS2\ucrt64\bin"
set "PATH=%MSYS2_BIN%;%PATH%"
if not exist build mkdir build

set "MODE=%~1"
if "%MODE%"=="" set "MODE=all"
if /i "%MODE%"=="all" goto app
if /i "%MODE%"=="test" goto test
if /i "%MODE%"=="probe" goto probe
echo usage: build.bat [all^|test^|probe]
exit /b 1

:app
windres -I res res\app.rc -O coff -o build\app.res || exit /b 1
g++ -std=c++17 -fno-exceptions -fno-rtti -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffunction-sections -fdata-sections -I res -Os -s -municode -mwindows -static -Wl,--gc-sections src\main.cpp src\taskbar.cpp src\core.cpp build\app.res -luser32 -lshell32 -lgdi32 -o build\TaskbarClear.exe || exit /b 1
echo built: build\TaskbarClear.exe
exit /b 0

:test
g++ -std=c++17 -fno-exceptions -fno-rtti -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffunction-sections -fdata-sections -O2 -static tests\selftest.cpp src\core.cpp -o build\selftest.exe || exit /b 1
build\selftest.exe
exit /b %ERRORLEVEL%

:probe
g++ -std=c++17 -fno-exceptions -fno-rtti -finput-charset=UTF-8 -fexec-charset=UTF-8 -ffunction-sections -fdata-sections -O2 -static tests\taskbar_probe.cpp src\taskbar.cpp -luser32 -o build\taskbar_probe.exe || exit /b 1
echo built: build\taskbar_probe.exe
exit /b 0