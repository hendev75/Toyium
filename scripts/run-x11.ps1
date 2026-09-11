# Run the Buildroot X11 userland with the existing Toyium kernel.
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$KernelStock = Join-Path $Root 'build\toyium-x11-bzImage'
$KernelToyium = Join-Path $Root 'build\bzImage'
$Kernel = if (Test-Path $KernelStock) { $KernelStock } else { $KernelToyium }
$Rootfs = Join-Path $Root 'build\toyium-x11-rootfs.ext4'

if (-not (Test-Path $Qemu)) { throw "QEMU not found: $Qemu" }
if (-not (Test-Path $Kernel)) { throw "Kernel missing: $Kernel" }
if (-not (Test-Path $Rootfs)) { throw "X11 rootfs missing: $Rootfs. Run scripts/build-x11.sh in WSL first." }

& $Qemu `
  -m 512 `
  -no-reboot `
  -kernel $Kernel `
  -drive "file=$Rootfs,if=virtio,format=raw" `
  -vga std `
  -netdev 'user,id=u0,hostfwd=tcp::8080-:80' `
  -device 'virtio-net-pci,netdev=u0' `
  -append 'root=/dev/vda rootfstype=ext4 rw console=ttyS0 console=tty1 quiet loglevel=3' `
  -display gtk
