# Toyium OS — wsl-build.ps1
# Helper to build Toyium OS from Windows PowerShell via WSL2
# Usage:  powershell -ExecutionPolicy Bypass -File wsl-build.ps1
#         powershell -ExecutionPolicy Bypass -File wsl-build.ps1 -Target run
param(
    [string]$Target = "all"
)

$ErrorActionPreference = "Stop"

# Resolve project root (this script's directory)
$ProjectRoot = $PSScriptRoot
if (-not $ProjectRoot) { $ProjectRoot = (Get-Location).Path }
$WslPath = wsl wslpath -a "$ProjectRoot" 2>$null
if (-not $WslPath) {
    Write-Host "[toyium] WSL not found. Please install WSL2: wsl --install" -ForegroundColor Red
    exit 1
}
$WslPath = $WslPath.Trim()

Write-Host "[toyium] Project: $ProjectRoot" -ForegroundColor Cyan
Write-Host "[toyium] WSL path: $WslPath" -ForegroundColor Cyan
Write-Host "[toyium] Target: $Target" -ForegroundColor Cyan
Write-Host ""

# Ensure WSL has deps (first run)
$CheckCmd = "cd '$WslPath' && bash -c 'command -v gcc >/dev/null || echo MISSING'"
$missing = wsl bash -c "cd '$WslPath' && bash -c 'command -v gcc >/dev/null || echo MISSING'"
if ($missing -match "MISSING") {
    Write-Host "[toyium] Installing build deps in WSL (requires sudo) ..." -ForegroundColor Yellow
    wsl bash -c "sudo apt update && sudo apt install -y build-essential bc bison flex libelf-dev libssl-dev libncurses-dev cpio qemu-system-x86 xorriso grub-pc-bin grub-efi-amd64-bin mtools wget curl git"
}

# Fix CRLF -> LF for scripts (Windows checkout)
wsl bash -c "cd '$WslPath' && sed -i 's/\r$//' scripts/*.sh overlay/init grub/grub.cfg Makefile 2>/dev/null; chmod +x scripts/*.sh overlay/init 2>/dev/null; echo '[toyium] line endings fixed'"

Write-Host "[toyium] Building: make $Target" -ForegroundColor Green
wsl bash -c "cd '$WslPath' && make $Target"

if ($LASTEXITCODE -ne 0) {
    Write-Host "[toyium] Build failed (exit $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "[toyium] Done. Outputs in $ProjectRoot\build\" -ForegroundColor Green
if (Test-Path "$ProjectRoot\build") {
    Get-ChildItem "$ProjectRoot\build" | Format-Table Name, Length, LastWriteTime
}
Write-Host "  To run in WSL:  wsl bash -c `"cd '$WslPath' && make run`"" -ForegroundColor Cyan
Write-Host "  Or Docker:      docker build -t toyium-builder .; docker run --rm -it -v ${PWD}:/toyium toyium-builder make run-nographic" -ForegroundColor Cyan
