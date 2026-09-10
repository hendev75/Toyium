# Toyium OS

```
  _____           _
 |_   _|__  _   _(_)_   _ _ __ ___
   | |/ _ \| | | | | | | | | '_ ` _ \
   | | (_) | |_| | | |_| | | | | | |
   |_|\___/ \__, |_|\__,_|_| |_| |_|
            |___/
```

**Toyium OS** — made by **xex & ayham**

A minimal 64-bit command-line operating system built on the Linux kernel
(7.3.0-rc2 mainline tree, trimmed to the essentials). Toyium boots straight
into its own tiny shell with only `toy*` commands — no busybox, no glibc, no
bloat. The entire userland is **one freestanding C binary (~10 KB)** that runs
as PID 1.

---

## What Toyium is

| Item | Detail |
|------|--------|
| **Architecture** | x86_64 |
| **Kernel** | Linux 7.3.0-rc2 (`linux-master` tree), minimal config |
| **Kernel tag** | `7.3.0-rc2-toyium` |
| **Userland** | custom `/init` (PID 1), freestanding C, no libc |
| **Root FS** | initramfs (cpio.gz, ~3 KB) — entire OS runs in RAM |
| **Display** | VGA text console (windowed QEMU) or serial console |
| **Input** | full keyboard: letters, digits, symbols, SHIFT, CAPS LOCK |
| **Size** | ~6.7 MB kernel + ~3 KB initramfs |

## Commands

| Command | Description |
|---------|-------------|
| `toyls [dir]` | list directory contents (dirs get a trailing `/`) |
| `toycd <dir>` | change current directory |
| `toynano <file>` | tiny text editor (see below) |
| `echo <text>` | print text |
| `clear` | clear the screen |
| `help` | list commands and keys |
| `poweroff` | power off the machine |
| `reboot` | reboot the machine |

## Keyboard support

Full typing works everywhere — letters, digits, symbols, **SHIFT** and
**CAPS LOCK** (handled by the kernel PS/2 keymap on the VGA window; the serial
console passes through whatever your terminal sends).

| Key | Action |
|-----|--------|
| `TAB` | auto-complete the command or path you are typing |
| `Up` / `Down` | walk command history |
| `Left` / `Right` | move cursor inside the line |
| `Home` / `End` | jump to start / end of line |
| `Delete` / `Backspace` | delete forward / backward |
| `Ctrl-A` / `Ctrl-E` | start / end of line |
| `Ctrl-U` | kill the whole line |
| `Ctrl-C` | cancel the current line |
| `Ctrl-D` | power off (on an empty line) |
| `Ctrl-L` | clear the screen |

TAB completion behaves like a mini-bash: unique matches complete (commands
get a trailing space, directories get a trailing `/`), ambiguous matches
extend to the common prefix, and repeated TAB lists the candidates.

## toynano — the text editor

`toynano <file>` opens (or creates) a file in a line-based editor:

- typed lines are appended to the buffer
- `:w` — save the file
- `:q` — quit without saving
- `:x` — save and quit
- `:d N` — delete line N
- `:p` — print the buffer with line numbers
- `:c` — clear the buffer
- `Ctrl-D` — save and quit

## Building

The kernel must be compiled on Linux. On Windows, WSL works (the project was
built with a WSL1 Ubuntu 22.04 distro).

```bash
# inside WSL, project root = this folder
# 1. build the kernel (from the linux-master tree)
bash scripts/build-kernel-toyium.sh <path-to-kernel-source>

# 2. build the initramfs (compiles src/toyium.c -> /init)
bash scripts/build-initramfs-toyium.sh .

# outputs:
#   build/bzImage
#   build/initramfs.cpio.gz
#   build/kernel.config
```

Requirements: `gcc make cpio gzip flex bison bc perl`.

Note: the build copies the kernel source to a Linux filesystem (building on
`/mnt/c` is slow) — the scripts expect the tree at a native path.

## Running

```powershell
# windowed (GTK, VGA console, PS/2 keyboard)
powershell -ExecutionPolicy Bypass -File scripts/run-toyium.ps1

# serial console in your terminal
powershell -ExecutionPolicy Bypass -File scripts/run-toyium.ps1 -Serial
```

Raw QEMU:

```bash
qemu-system-x86_64 -m 256 -no-reboot \
  -kernel build/bzImage -initrd build/initramfs.cpio.gz \
  -append "console=tty0 rdinit=/init quiet loglevel=3"
```

## Self-test

Boot with the extra kernel arg `toyium=test` and `/init` runs an automated
smoke test (commands, completion, history, toynano round-trip) and powers off:

```bash
qemu-system-x86_64 -m 256 -no-reboot -display none -serial stdio \
  -kernel build/bzImage -initrd build/initramfs.cpio.gz \
  -append "console=ttyS0 rdinit=/init toyium=test"
```

## Project layout

```
os/
├── README.md
├── VERSION
├── src/
│   └── toyium.c              # the whole userland (freestanding /init)
├── scripts/
│   ├── build-kernel-toyium.sh    # kernel config + build
│   ├── build-initramfs-toyium.sh # compile init + pack cpio.gz
│   └── run-toyium.ps1            # QEMU launcher (Windows)
├── configs/
│   └── kernel-toyium.config  # kernel config used for the build
├── linux-master/             # Linux 7.3.0-rc2 source tree (not edited)
└── build/                    # build outputs (gitignored)
```

## Credits

- OS concept, kernel build and userland: **xex & ayham**
- Linux kernel: https://kernel.org (GPL-2.0)
- Toyium userland: MIT — do what you want
