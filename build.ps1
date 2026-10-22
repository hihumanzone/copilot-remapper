# PowerShell build script for CopilotRemapper (Native Modern C++)
$ErrorActionPreference = "Stop"

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "Building CopilotRemapper (Native Modern C++ Executable)" -ForegroundColor Cyan
Write-Host "=======================================================" -ForegroundColor Cyan

# Stop running instance if any
Get-Process CopilotRemapper -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$toolsGxx = Join-Path $scriptDir ".tools\w64devkit\bin\g++.exe"
$toolsWindres = Join-Path $scriptDir ".tools\w64devkit\bin\windres.exe"

$compiler = $null
$windres = $null

if (Get-Command g++.exe -ErrorAction SilentlyContinue) {
    $compiler = "g++.exe"
    $windres = "windres.exe"
} elseif (Test-Path $toolsGxx) {
    $compiler = $toolsGxx
    $windres = $toolsWindres
} elseif (Get-Command cl.exe -ErrorAction SilentlyContinue) {
    $compiler = "cl.exe"
} else {
    Write-Host "[INFO] No C++ compiler found in PATH." -ForegroundColor Yellow
    Write-Host "[INFO] Bootstrapping portable w64devkit compiler toolchain..." -ForegroundColor Yellow

    $toolsDir = Join-Path $scriptDir ".tools"
    if (-not (Test-Path $toolsDir)) { New-Item -ItemType Directory -Path $toolsDir | Out-Null }

    $installerPath = Join-Path $toolsDir "w64devkit.exe"
    Write-Host "[INFO] Downloading w64devkit..." -ForegroundColor Gray
    curl.exe -L -o "$installerPath" "https://github.com/skeeto/w64devkit/releases/download/v2.10.1/w64devkit-x64-2.10.1.7z.exe"

    Write-Host "[INFO] Unpacking w64devkit..." -ForegroundColor Gray
    Start-Process -FilePath "$installerPath" -ArgumentList "-o`"$toolsDir`" -y" -Wait
    Remove-Item "$installerPath" -Force -ErrorAction SilentlyContinue

    if (Test-Path $toolsGxx) {
        $compiler = $toolsGxx
        $windres = $toolsWindres
    } else {
        Write-Error "Failed to bootstrap w64devkit."
        exit 1
    }
}

$resourceRc = Join-Path $scriptDir "resource.rc"
$resourceObj = Join-Path $scriptDir "resource.o"
$outFile = Join-Path $scriptDir "CopilotRemapper.exe"

$sources = @(
    (Join-Path $scriptDir "src\main.cpp"),
    (Join-Path $scriptDir "src\Config.cpp"),
    (Join-Path $scriptDir "src\ActionRunner.cpp"),
    (Join-Path $scriptDir "src\KeyboardHook.cpp"),
    (Join-Path $scriptDir "src\Theme.cpp"),
    (Join-Path $scriptDir "src\IconHelper.cpp"),
    (Join-Path $scriptDir "src\SettingsWindow.cpp"),
    (Join-Path $scriptDir "src\TrayIcon.cpp"),
    (Join-Path $scriptDir "src\SingleInstance.cpp"),
    (Join-Path $scriptDir "src\ProcessManager.cpp")
)

if ($compiler -eq "cl.exe") {
    Write-Host "Using compiler: MSVC cl.exe" -ForegroundColor Gray
    $resFile = Join-Path $scriptDir "resource.res"
    & rc.exe /nologo /fo "$resFile" "$resourceRc"
    
    & cl.exe /nologo /std:c++17 /O2 /GL /Gy /Gw /W3 /DUNICODE /D_UNICODE /DNDEBUG /EHsc `
        "-I$scriptDir" "-I$(Join-Path $scriptDir 'src')" `
        $sources "$resFile" `
        /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /OUT:"$outFile" `
        user32.lib gdi32.lib shell32.lib comctl32.lib dwmapi.lib uxtheme.lib advapi32.lib ole32.lib uuid.lib

    Remove-Item "$resFile" -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $scriptDir "*.obj") -Force -ErrorAction SilentlyContinue
} else {
    Write-Host "Using compiler: $compiler" -ForegroundColor Gray
    Write-Host "Compiling resources..." -ForegroundColor Gray
    & $windres -i "$resourceRc" -o "$resourceObj"

    Write-Host "Compiling C++ sources..." -ForegroundColor Gray
    & $compiler -std=c++17 -O3 -ffunction-sections -fdata-sections -mwindows -municode -static -s -DUNICODE -D_UNICODE -DNDEBUG `
        "-I$scriptDir" "-I$(Join-Path $scriptDir 'src')" `
        $sources "$resourceObj" `
        "-Wl,--gc-sections" -lcomctl32 -ldwmapi -luxtheme -lole32 -luuid `
        -o "$outFile"

    $compileErr = $LASTEXITCODE
    Remove-Item "$resourceObj" -Force -ErrorAction SilentlyContinue
    if ($compileErr -ne 0) {
        Write-Error "Compilation failed with exit code $compileErr"
        exit $compileErr
    }
}

if (Test-Path $outFile) {
    Write-Host "`n[SUCCESS] CopilotRemapper.exe built successfully!" -ForegroundColor Green
    Write-Host "Location: $outFile" -ForegroundColor Gray
} else {
    Write-Error "Output executable was not created."
    exit 1
}
