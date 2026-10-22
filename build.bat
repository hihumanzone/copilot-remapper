@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo Building CopilotRemapper (Native Modern C++ Executable)
echo =======================================================

:: Stop any running instance
taskkill /F /IM CopilotRemapper.exe >nul 2>&1
timeout /t 1 /nobreak >nul 2>&1

:: Check for existing g++ in PATH
where g++.exe >nul 2>&1
if %errorlevel% equ 0 (
    set GXX=g++.exe
    set WINDRES=windres.exe
    goto :compile_gcc
)

:: Check for portable w64devkit in .tools
set TOOLS_GXX="%~dp0.tools\w64devkit\bin\g++.exe"
set TOOLS_WINDRES="%~dp0.tools\w64devkit\bin\windres.exe"

if exist %TOOLS_GXX% (
    set GXX=%TOOLS_GXX%
    set WINDRES=%TOOLS_WINDRES%
    goto :compile_gcc
)

:: Check for cl.exe (MSVC) in PATH
where cl.exe >nul 2>&1
if %errorlevel% equ 0 (
    goto :compile_msvc
)

:: Automatically bootstrap w64devkit
echo [INFO] No C++ compiler found in PATH.
echo [INFO] Bootstrapping portable w64devkit compiler toolchain...
if not exist "%~dp0.tools" mkdir "%~dp0.tools"

echo [INFO] Downloading w64devkit...
curl.exe -L -o "%~dp0.tools\w64devkit.exe" "https://github.com/skeeto/w64devkit/releases/download/v2.10.1/w64devkit-x64-2.10.1.7z.exe"
if %errorlevel% neq 0 (
    echo [ERROR] Failed to download w64devkit.
    exit /b 1
)

echo [INFO] Unpacking w64devkit...
start /wait "" "%~dp0.tools\w64devkit.exe" -o"%~dp0.tools" -y
del "%~dp0.tools\w64devkit.exe" >nul 2>&1

if not exist %TOOLS_GXX% (
    echo [ERROR] Failed to unpack w64devkit.
    exit /b 1
)

set GXX=%TOOLS_GXX%
set WINDRES=%TOOLS_WINDRES%

:compile_gcc
echo Using C++ Compiler: %GXX%
echo Compiling resources...
%WINDRES% -i "%~dp0resource.rc" -o "%~dp0resource.o"
if %errorlevel% neq 0 (
    echo [ERROR] Resource compilation failed.
    exit /b 1
)

echo Compiling C++ sources...
%GXX% -std=c++17 -O3 -ffunction-sections -fdata-sections -mwindows -municode -static -s -DUNICODE -D_UNICODE -DNDEBUG ^
    -I"%~dp0." -I"%~dp0src" ^
    "%~dp0src\main.cpp" ^
    "%~dp0src\Config.cpp" ^
    "%~dp0src\ActionRunner.cpp" ^
    "%~dp0src\KeyboardHook.cpp" ^
    "%~dp0src\Theme.cpp" ^
    "%~dp0src\IconHelper.cpp" ^
    "%~dp0src\SettingsWindow.cpp" ^
    "%~dp0src\TrayIcon.cpp" ^
    "%~dp0src\SingleInstance.cpp" ^
    "%~dp0src\ProcessManager.cpp" ^
    "%~dp0resource.o" ^
    -Wl,--gc-sections -lcomctl32 -ldwmapi -luxtheme -lole32 -luuid ^
    -o "%~dp0CopilotRemapper.exe"

set COMPILE_ERR=%errorlevel%
del "%~dp0resource.o" >nul 2>&1

if %COMPILE_ERR% neq 0 (
    echo [ERROR] Compilation failed.
    exit /b 1
)
goto success

:compile_msvc
echo Using MSVC Compiler: cl.exe
echo Compiling resources...
rc.exe /nologo /fo "%~dp0resource.res" "%~dp0resource.rc"
if %errorlevel% neq 0 (
    echo [ERROR] Resource compilation failed.
    exit /b 1
)

echo Compiling C++ sources with MSVC...
cl.exe /nologo /std:c++17 /O2 /GL /Gy /Gw /W3 /DUNICODE /D_UNICODE /DNDEBUG /EHsc ^
    /I"%~dp0." /I"%~dp0src" ^
    "%~dp0src\main.cpp" ^
    "%~dp0src\Config.cpp" ^
    "%~dp0src\ActionRunner.cpp" ^
    "%~dp0src\KeyboardHook.cpp" ^
    "%~dp0src\Theme.cpp" ^
    "%~dp0src\IconHelper.cpp" ^
    "%~dp0src\SettingsWindow.cpp" ^
    "%~dp0src\TrayIcon.cpp" ^
    "%~dp0src\SingleInstance.cpp" ^
    "%~dp0src\ProcessManager.cpp" ^
    "%~dp0resource.res" ^
    /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /OUT:"%~dp0CopilotRemapper.exe" ^
    user32.lib gdi32.lib shell32.lib comctl32.lib dwmapi.lib uxtheme.lib advapi32.lib ole32.lib uuid.lib

set COMPILE_ERR=%errorlevel%
del "%~dp0resource.res" >nul 2>&1
del "%~dp0*.obj" >nul 2>&1

if %COMPILE_ERR% neq 0 (
    echo [ERROR] MSVC Compilation failed.
    exit /b 1
)

:success
echo.
echo =======================================================
echo [SUCCESS] CopilotRemapper.exe built successfully!
echo Location: %~dp0CopilotRemapper.exe
echo =======================================================

endlocal
