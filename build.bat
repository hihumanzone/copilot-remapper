@echo off
setlocal
echo =======================================================
echo Building CopilotRemapper (Native Windows Executable)
echo =======================================================

set CSC="C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if exist %CSC% goto compile

set CSC="C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if exist %CSC% goto compile

echo [ERROR] C# Compiler (csc.exe) not found!
exit /b 1

:compile
echo Using C# Compiler: %CSC%
echo Stopping any running instance of CopilotRemapper...
taskkill /F /IM CopilotRemapper.exe >nul 2>&1

echo Compiling Program.cs with High-DPI Manifest...

%CSC% /target:winexe /optimize+ /platform:anycpu /win32manifest:"%~dp0app.manifest" /r:System.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll /out:"%~dp0CopilotRemapper.exe" "%~dp0Program.cs"

if errorlevel 1 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

echo.
echo =======================================================
echo [SUCCESS] CopilotRemapper.exe built successfully!
echo Location: %~dp0CopilotRemapper.exe
echo =======================================================

endlocal
