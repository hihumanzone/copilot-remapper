# Build script for CopilotRemapper using Windows native C# compiler (csc.exe)
$ErrorActionPreference = "Stop"

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "Building CopilotRemapper (Native Windows Executable)" -ForegroundColor Cyan
Write-Host "=======================================================" -ForegroundColor Cyan

$csc64 = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
$csc32 = "C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe"

$csc = if (Test-Path $csc64) { $csc64 } elseif (Test-Path $csc32) { $csc32 } else { $null }

if (-not $csc) {
    Write-Error "C# compiler (csc.exe) was not found in Microsoft.NET Framework directories."
    exit 1
}

# Stop running instance if any
Get-Process CopilotRemapper -ErrorAction SilentlyContinue | Stop-Process -Force

Write-Host "Using compiler: $csc" -ForegroundColor Gray
$currentDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceFile = Join-Path $currentDir "Program.cs"
$manifestFile = Join-Path $currentDir "app.manifest"
$outFile = Join-Path $currentDir "CopilotRemapper.exe"

& $csc /target:winexe /optimize+ /platform:anycpu /win32manifest:"$manifestFile" `
    /r:System.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll `
    /out:"$outFile" "$sourceFile"

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[SUCCESS] CopilotRemapper.exe built successfully!" -ForegroundColor Green
    Write-Host "Location: $outFile" -ForegroundColor Gray
} else {
    Write-Host "`n[ERROR] Compilation failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}
