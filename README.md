# Toyium OS

```
  _____           _
 |_   _|__  _   _(_)_   _ _ __ ___
   | |/ _ \| | | | | | | | | '_ ` _ \
   | | (_) | |_| | | |_| | | | | | |
   |_|\___/ \__, |_|\__,_|_| |_| |_|
            |___/
```
<img width="721" height="456" alt="image" src="https://github.com/user-attachments/assets/3fe99642-b673-492b-b037-a5b48729745f" />

**Toyium OS** — made by **xex & ayham**

A minimal 64-bit command-line operating system built on the Linux kernel
(7.3.0-rc2 mainline tree, trimmed to the essentials). Toyium boots straight
into its own tiny shell with only `toy*` commands — no busybox, no glibc, no
bloat. The whole userland is **one freestanding C binary (~12 KB)** that runs
as PID 1. It ships with **toyfs**, a custom in-memory filesystem written for
this project.

---

## What Toyium is

| Item | Detail |
|------|--------|
| **Architecture** | x86_64 |
| **Kernel** | Linux 7.3.0-rc2 (`linux-master` tree), minimal config |
| **Kernel tag** | `7.3.0-rc2-toyium` |
| **Userland** | custom `/init` (PID 1), freestanding C, no libc |
| **Filesystem** | **toyfs** — the Toyium in-memory filesystem (`fs/toyfs/`) |
| **Root FS** | initramfs (cpio.gz, ~10 KB) — the OS runs in RAM |
| **Display** | VGA text console (windowed QEMU) or serial console |
| **Cursor** | blinking block cursor |
| **Input** | full keyboard: letters, digits, symbols, SHIFT, CAPS LOCK |
| **Size** | ~9.3 MB kernel (with networking) + ~12 KB initramfs |

## Commands

| Command | Description |
|---------|-------------|
| `toyls [dir]` | list directory contents (dirs get a trailing `/`) |
| `toycd <dir>` | change current directory (shown in the prompt) |
| `toypwd` | print the working directory |
| `toycat <file>` | print a file |
| `toyfetch <host> [path]` | fetch a web page over HTTP (needs QEMU net) |
| `toydns <host>` | resolve a hostname to IPv4 |
| `toyip` | show interface IPv4 addresses |
| `toyping <host> [count]` | ICMP ping a website or IP (TCP/80 fallback) |
| `toymount <dev> <dir> [fstype]` | mount a filesystem |
| `toydf [path]` | show filesystem space |
| `toync <host> <port>` | TCP client (Ctrl-D quits) |
| `toyntp [server]` | sync clock via NTP |
| `toyserve [port] [dir] [n]` | serve files over HTTP |
| `toynano <file>` | full-screen text editor |
| `echo <text>` | print text |
| `clear` | clear the screen |
| `help` | list commands and keys |
| `poweroff` | power off the machine |
| `reboot` | reboot the machine |

The prompt is `toyium:<dir>#` and the window title follows the current
directory on serial terminals.

## toyfs — the Toyium filesystem

`toyfs` is a small read-write in-memory filesystem implemented in the kernel
tree at `linux-master/fs/toyfs/` (registered as the `toyfs` filesystem type,
built into the kernel). `/init` mounts it at `/toy` and starts you there, so
your first `toyls` shows your own filesystem:

```
toyium:/toy# toyls
./
../
welcome.txt
```

Files created with `toynano` live on toyfs and survive as long as the machine
is running.

## toynano — the editor

`toynano <file>` is a full-screen editor (not just line input):

- full cursor movement and on-screen status bar (file, line, column)
- insert anywhere, Enter splits lines, Backspace/Delete (joins lines)
- horizontal display, scrolling, `[modified]` indicator

| Key | Action |
|-----|--------|
| arrows, `Home`/`End`, `PgUp`/`PgDn` | move |
| `Enter` | new line |
| `Tab` | insert spaces to next tab stop |
| `Backspace` / `Delete` | delete backward / forward |
| `Ctrl-O` / `Ctrl-S` | save |
| `Ctrl-X` | exit (prompts to save if modified) |
| `Ctrl-K` / `Ctrl-U` | cut line / paste line |

## Keyboard support

Full typing works everywhere — letters, digits, symbols, **SHIFT** and
**CAPS LOCK** (handled by the kernel PS/2 keymap on the VGA window; the serial
console passes through whatever your terminal sends).

| Key (shell) | Action |
|-------------|--------|
| `TAB` | auto-complete the command or path you are typing |
| `Up` / `Down` | command history |
| `Left`/`Right`/`Home`/`End`/`Delete`, `Backspace` | edit the line |
| `Ctrl-A`/`Ctrl-E`/`Ctrl-U` | start / end of line, kill line |
| `Ctrl-C` | cancel line |
| `Ctrl-D` | power off (on an empty line) |
| `Ctrl-L` | clear screen |

TAB completion behaves like a mini-bash: unique matches complete (commands
get a trailing space, directories a trailing `/`), ambiguous matches extend to
the common prefix, then list the candidates.

## Building

The kernel must be compiled on Linux. On Windows, WSL works (the project was
built with a WSL1 Ubuntu 22.04 distro).

```bash
# inside WSL, project root = this folder
# 1. build the kernel (from the linux-master tree, includes fs/toyfs)
bash scripts/build-kernel-toyium.sh <path-to-kernel-source>

# 2. build the initramfs (compiles src/toyium.c -> /init)
bash scripts/build-initramfs-toyium.sh .

# outputs: build/bzImage, build/initramfs.cpio.gz, build/kernel.config
```

Requirements: `gcc make cpio gzip flex bison bc perl`.

## Running

```powershell
# windowed (GTK, VGA console, PS/2 keyboard, block cursor)
powershell -ExecutionPolicy Bypass -File scripts/run-toyium.ps1

# serial console in your terminal
powershell -ExecutionPolicy Bypass -File scripts/run-toyium.ps1 -Serial
```

Or from the project root: `run-toyium` (wrapper `.bat`).

Raw QEMU:

```bash
qemu-system-x86_64 -m 256 -no-reboot \
  -netdev user,id=u0 -device virtio-net-pci,netdev=u0 \
  -kernel build/bzImage -initrd build/initramfs.cpio.gz \
  -append "console=tty0 rdinit=/init ip=dhcp quiet loglevel=3"
```

## Networking

The kernel ships with a minimal network stack (`NET`, `INET`, `virtio-net`,
`IP_PNP_DHCP`) and QEMU provides user-mode NAT. The guest gets `10.0.2.15`
via DHCP (`ip=dhcp` on the kernel command line), DNS is at `10.0.2.3`.
`/init` speaks raw syscalls — no libc — with its own tiny DNS client:

```
toyium:/toy# toyip
eth0: 10.0.2.15
lo: 127.0.0.1
toyium:/toy# toydns example.com
example.com -> 172.66.147.243
toyium:/toy# toyfetch example.com
<!doctype html>...Example Domain...
[toyfetch: 559 body bytes]
toyium:/toy# toyping example.com
PING example.com (172.66.147.243)
reply seq=0 time=17 ms
reply seq=1 time=15 ms
--- example.com ping statistics ---
2 sent, 2 received, 0% loss
rtt min/avg/max = 15/16/17 ms
```

## Persistent disk

`build/disk.img` (256 MB ext4, gitignored) is attached as a virtio-blk drive
when present and auto-mounted at `/disk` — files there survive reboots:

```
toyium:/toy# toydf /disk
/disk: 223M total, 223M free
toyium:/toy# toynano /disk/note.txt   # edit, Ctrl-O, Ctrl-X
toyium:/toy# toycat /disk/note.txt
hello persistent world
```

Create it with `qemu-img create -f raw build/disk.img 256M` and format with
`mkfs.ext4 -F build/disk.img` (in WSL).

## More net tools

- `toync <host> <port>` — interactive TCP client; blank line sends CRLF,
  Ctrl-D quits. Handy for hand-rolled HTTP.
- `toyntp [server]` — SNTP client (default `pool.ntp.org`); sets the system
  clock and prints UTC.
- `toyserve [port] [dir] [n]` — tiny iterative HTTP server (default
  `toyserve 80 /toy 8`). QEMU forwards host `:8080` to guest `:80`, so from
  Windows: `curl http://localhost:8080/welcome.txt`.

## Self-test

Boot with the extra kernel arg `toyium=test` and `/init` runs an automated
smoke test (commands, toyfs round-trip, completion, history, toynano editing)
and powers off:

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
│   └── toyium.c                  # the whole userland (freestanding /init)
├── scripts/
│   ├── build-kernel-toyium.sh    # kernel config + build
│   ├── build-initramfs-toyium.sh # compile init + pack cpio.gz
│   └── run-toyium.ps1            # QEMU launcher (Windows)
├── configs/
│   └── kernel-toyium.config      # kernel config used for the build
├── linux-master/                 # Linux 7.3.0-rc2 source tree
│   └── fs/toyfs/                 # the Toyium filesystem
└── build/                        # build outputs (gitignored)
```

## Credits

- OS concept, kernel build, toyfs and userland: **xex & ayham**
- Linux kernel: https://kernel.org (GPL-2.0)
- Toyium userland: MIT — do what you want
