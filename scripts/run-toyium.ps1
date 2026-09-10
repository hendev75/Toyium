# Toyium OS - QEMU launcher (Windows)
# Usage:
#   powershell -ExecutionPolicy Bypass -File run-toyium.ps1          # windowed (GTK)
#   powershell -ExecutionPolicy Bypass -File run-toyium.ps1 -Serial  # terminal serial console
param(
    [ValidateSet('Window', 'Serial')]
    [string]$Mode = 'Window'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$Kernel = Join-Path $Root 'build\bzImage'
$Initrd = Join-Path $Root 'build\initramfs.cpio.gz'

if (-not (Test-Path $Qemu))  { Write-Error "qemu not found at $Qemu"; exit 1 }
if (-not (Test-Path $Kernel)) { Write-Error "missing $Kernel - build first"; exit 1 }
if (-not (Test-Path $Initrd)) { Write-Error "missing $Initrd - build first"; exit 1 }

$base = @('-m', '256', '-no-reboot', '-kernel', $Kernel, '-initrd', $Initrd)

if ($Mode -eq 'Window') {
    Write-Host '[toyium] windowed boot (GTK). Type help / toyls / toycd. poweroff to quit.' -ForegroundColor Cyan
    $append = 'console=tty0 rdinit=/init quiet loglevel=3'
    & $Qemu @base -append $append -display gtk
} else {
    Write-Host '[toyium] serial boot. poweroff to quit; Ctrl-A X to kill QEMU.' -ForegroundColor Cyan
    $append = 'console=ttyS0 rdinit=/init quiet loglevel=3'
    & $Qemu @base -append $append -nographic
}
