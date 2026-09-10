/*
 * toyium.c - Toyium OS /init
 *
 * Freestanding x86_64 PID1 for an initramfs-only toy kernel.
 * No libc, no busybox. Minimal kernel setup, then a REPL exposing:
 *
 *   toyls [dir]    list directory entries
 *   toycd <dir>    change directory
 *   toynano <file> tiny line editor
 *   echo <text>    print text
 *   clear          clear the screen
 *   help           command help
 *   poweroff       power off the machine
 *   reboot         reboot the machine
 *
 * Line editing: full cursor movement (Left/Right/Home/End/Delete),
 * Backspace, Ctrl-A/E/C/D/L/U, TAB completion (commands + paths),
 * history with Up/Down arrows.
 *
 * Build (Linux, x86_64):
 *   gcc -Os -ffreestanding -nostdlib -static -fno-stack-protector \
 *       -fno-pie -no-pie -mno-red-zone -o init toyium.c
 */

typedef unsigned long u64;
typedef unsigned int  u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long s64;
typedef long ssize_t;

/* ---- x86_64 syscall numbers ---- */
#define SYS_read        0
#define SYS_write       1
#define SYS_open        2
#define SYS_close       3
#define SYS_ioctl       16
#define SYS_mkdir       83
#define SYS_chdir       80
#define SYS_getcwd      79
#define SYS_mount       165
#define SYS_reboot      169
#define SYS_uname       63
#define SYS_exit_group  231
#define SYS_getdents64  217
#define SYS_sync        162
#define SYS_sethostname 170

#define AT_FDCWD        (-100)
#define O_RDONLY        0
#define O_WRONLY        1
#define O_CREAT         0100
#define O_TRUNC         01000
#define O_DIRECTORY     00200000

#define TCGETS          0x5401
#define TCSETS          0x5402

/* ---- tiny syscall wrappers ---- */
static inline long sc0(long n) { long r; asm volatile("syscall" : "=a"(r) : "a"(n) : "rcx", "r11", "memory"); return r; }
static inline long sc1(long n, long a) { long r; asm volatile("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx", "r11", "memory"); return r; }
static inline long sc2(long n, long a, long b) { long r; asm volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b) : "rcx", "r11", "memory"); return r; }
static inline long sc3(long n, long a, long b, long c) { long r; asm volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory"); return r; }
static inline long sc5(long n, long a, long b, long c, long d, long e) {
    long r; register long r10 asm("r10") = d; register long r8 asm("r8") = e;
    asm volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8) : "rcx", "r11", "memory"); return r;
}
static inline long sc6(long n, long a, long b, long c, long d, long e, long f) {
    long r; register long r10 asm("r10") = d; register long r8 asm("r8") = e; register long r9 asm("r9") = f;
    asm volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory"); return r;
}

static long k_write(int fd, const char *buf, u64 len) { return sc3(SYS_write, fd, (long)buf, (long)len); }
static long k_open(const char *p, long flags)         { return sc2(SYS_open, (long)p, flags); }
static long k_open3(const char *p, long flags, long mode) { return sc3(SYS_open, (long)p, flags, mode); }
static long k_close(long fd)                          { return sc1(SYS_close, fd); }
static long k_read(long fd, void *b, u64 n)           { return sc3(SYS_read, fd, (long)b, (long)n); }
static long k_chdir(const char *p)                    { return sc1(SYS_chdir, (long)p); }
static long k_getcwd(char *b, u64 n)                  { return sc2(SYS_getcwd, (long)b, (long)n); }
static long k_mkdir(const char *p, long m)            { return sc2(SYS_mkdir, (long)p, m); }
static long k_mount(const char *s, const char *t, const char *f, u64 fl, const void *d)
                                                        { return sc5(SYS_mount, (long)s, (long)t, (long)f, (long)fl, (long)d); }

struct linux_dirent64 {
    u64  d_ino;
    s64  d_off;
    u16  d_reclen;
    u8   d_type;
    char d_name[];
};
#define DT_UNKNOWN 0
#define DT_DIR     4

struct termios {
    u32 c_iflag, c_oflag, c_cflag, c_lflag;
    u8  c_line;
    u8  c_cc[19];
};

struct utsname {
    char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
};

/* ---- string / io helpers ---- */
static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static int scmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }
static void scpy(char *dst, const char *src) { while ((*dst++ = *src++)) {} }
static void puts(const char *s) { k_write(1, s, slen(s)); }
static void putc(char c) { k_write(1, &c, 1); }

static void putu(u64 v) {
    char b[24]; int i = 23; b[i] = 0;
    if (v == 0) b[--i] = '0';
    while (v) { b[--i] = '0' + (v % 10); v /= 10; }
    puts(&b[i]);
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}

static void clrscr(void) { puts("\x1b[2J\x1b[H"); }

static int contains(const char *hay, const char *needle) {
    u64 i, j;
    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j] && hay[i + j] == needle[j]; j++);
        if (needle[j] == 0) return 1;
    }
    return 0;
}

/* ---- terminal ---- */
static int raw_mode(int fd) {
    struct termios t;
    if (sc3(SYS_ioctl, fd, TCGETS, (long)&t) != 0) return 0; /* not a tty */
    t.c_lflag &= ~(u32)(0x000B);  /* ICANON | ECHO | ISIG */
    t.c_oflag &= ~(u32)0x0001;    /* OPOST */
    t.c_cc[5] = 0;                /* VTIME */
    t.c_cc[6] = 1;                /* VMIN: block for 1 char */
    if (sc3(SYS_ioctl, fd, TCSETS, (long)&t) != 0) return 0;
    return 1;
}

static void render_line(const char *prompt, const char *buf, int pos) {
    puts("\r\x1b[K");
    puts(prompt);
    puts(buf);
    putc('\r');
    int n = (int)slen(prompt) + pos;
    if (n > 0) { puts("\x1b["); putu((u64)n); putc('C'); }
}

/* ---- command history ---- */
#define HIST_MAX 32
#define HIST_LEN 256
static char hist[HIST_MAX][HIST_LEN];
static int  hist_n = 0;
static int  hist_pos = 0;      /* hist_n == draft position */
static char hist_draft[HIST_LEN];

static void hist_add(const char *s) {
    u64 l = slen(s);
    if (l == 0 || l >= HIST_LEN) return;
    if (hist_n > 0 && scmp(hist[hist_n - 1], s) == 0) { hist_pos = hist_n; return; }
    if (hist_n == HIST_MAX) {
        for (int i = 0; i < HIST_MAX - 1; i++) scpy(hist[i], hist[i + 1]);
        hist_n--;
    }
    scpy(hist[hist_n], s);
    hist_n++;
    hist_pos = hist_n;
}

static const char *hist_up(const char *cur) {
    if (hist_pos == hist_n) scpy(hist_draft, cur);
    if (hist_pos > 0) { hist_pos--; return hist[hist_pos]; }
    return 0;
}

static const char *hist_down(void) {
    if (hist_pos < hist_n) {
        hist_pos++;
        if (hist_pos == hist_n) return hist_draft;
        return hist[hist_pos];
    }
    return 0;
}

/* ---- commands (for completion + dispatch) ---- */
#define NCMD 8
static const char *cmd_names[NCMD] = {
    "toyls", "toycd", "toynano", "echo", "clear", "help", "poweroff", "reboot"
};

static int common_prefix_cmds(const char **ms, int n) {
    int cp = 0;
    if (n <= 0) return 0;
    for (;;) {
        char c = ms[0][cp];
        if (!c) break;
        for (int i = 1; i < n; i++) if (ms[i][cp] != c) return cp;
        cp++;
    }
    return cp;
}

/* ---- TAB completion ---- */
static char tc_dbuf[4096];
static char tc_names[64][132];   /* name + NUL, [128] = d_type stash */

static int tab_complete(char *buf, int *len, int *pos, int cap) {
    int ts = *pos;
    while (ts > 0 && buf[ts - 1] != ' ' && buf[ts - 1] != '\t') ts--;
    int toklen = *pos - ts;
    if (toklen >= 128) return 0;
    char tok[128];
    for (int i = 0; i < toklen; i++) tok[i] = buf[ts + i];
    tok[toklen] = 0;

    int is_first = (ts == 0);
    int has_slash = 0;
    for (int i = 0; i < toklen; i++) if (tok[i] == '/') { has_slash = 1; break; }

    char ins[192];
    ins[0] = 0;

    if (is_first && !has_slash) {
        /* ---- command name completion ---- */
        const char *ms[NCMD];
        int n = 0;
        for (int i = 0; i < NCMD; i++) if (starts_with(cmd_names[i], tok)) ms[n++] = cmd_names[i];
        if (n == 0) return 0;
        if (n == 1) {
            scpy(ins, ms[0]);
            u64 l = slen(ins);
            ins[l] = ' ';
            ins[l + 1] = 0;
        } else {
            int cp = common_prefix_cmds(ms, n);
            if (cp > toklen) {
                for (int i = 0; i < cp; i++) ins[i] = ms[0][i];
                ins[cp] = 0;
            } else {
                putc('\r'); putc('\n');
                for (int i = 0; i < n; i++) { puts(ms[i]); puts("\r\n"); }
                return 0;
            }
        }
    } else {
        /* ---- path completion ---- */
        int cut = -1;
        for (int i = 0; i < toklen; i++) if (tok[i] == '/') cut = i;
        char dir[160];
        int dl = 0;
        int skip_dir = 0;
        if (cut < 0) { dir[0] = '.'; dir[1] = 0; dl = 1; skip_dir = 1; }
        else if (cut == 0) { dir[0] = '/'; dir[1] = 0; dl = 1; }
        else { for (int i = 0; i < cut; i++) dir[i] = tok[i]; dir[cut] = 0; dl = cut; }

        char prefix[128];
        int pl = toklen - (cut + 1);
        if (cut < 0) pl = toklen;
        if (pl >= 128) return 0;
        for (int i = 0; i < pl; i++) prefix[i] = tok[cut + 1 + i];
        prefix[pl] = 0;

        int fd = (int)k_open(dir, O_RDONLY | O_DIRECTORY);
        if (fd < 0) return 0;
        int n = 0;
        for (;;) {
            long gn = sc3(SYS_getdents64, fd, (long)tc_dbuf, (long)sizeof tc_dbuf);
            if (gn <= 0) break;
            u64 off = 0;
            while (off < (u64)gn) {
                struct linux_dirent64 *de = (struct linux_dirent64 *)(tc_dbuf + off);
                u64 nl = slen(de->d_name);
                if (n < 64 && nl < 126 && starts_with(de->d_name, prefix)) {
                    for (u64 i = 0; i <= nl; i++) tc_names[n][i] = de->d_name[i];
                    tc_names[n][128] = de->d_type;
                    n++;
                }
                off += de->d_reclen;
            }
        }
        k_close(fd);
        if (n == 0) return 0;

        if (n == 1) {
            int p = 0;
            if (!skip_dir) for (int i = 0; i < dl; i++) ins[p++] = dir[i];
            for (int i = 0; tc_names[0][i]; i++) ins[p++] = tc_names[0][i];
            if (tc_names[0][128] == DT_DIR) ins[p++] = '/';
            else ins[p++] = ' ';
            ins[p] = 0;
        } else {
            /* common prefix across names */
            int cp = 0;
            for (;;) {
                char c = tc_names[0][cp];
                if (!c) break;
                int all = 1;
                for (int i = 1; i < n; i++) if (tc_names[i][cp] != c) { all = 0; break; }
                if (!all) break;
                cp++;
            }
            if (cp > pl) {
                int p = 0;
                if (!skip_dir) for (int i = 0; i < dl; i++) ins[p++] = dir[i];
                for (int i = 0; i < cp; i++) ins[p++] = tc_names[0][i];
                ins[p] = 0;
            } else {
                putc('\r'); putc('\n');
                for (int i = 0; i < n; i++) {
                    if (!skip_dir) { puts(dir); }
                    puts(tc_names[i]);
                    if (tc_names[i][128] == DT_DIR) putc('/');
                    puts("\r\n");
                }
                return 0;
            }
        }
    }

    /* splice: replace token [ts, *pos) with ins */
    int il = (int)slen(ins);
    int taillen = *len - *pos;
    if (*len - toklen + il >= cap) return 0;
    for (int i = taillen - 1; i >= 0; i--) buf[ts + il + i] = buf[*pos + i];
    for (int i = 0; i < il; i++) buf[ts + i] = ins[i];
    *len = ts + il + taillen;
    *pos = ts + il;
    buf[*len] = 0;
    return 1;
}

/* ---- line reader with editing, history and completion ---- */
static int readline(const char *prompt, char *buf, int cap) {
    int len = 0, pos = 0;
    buf[0] = 0;
    hist_pos = hist_n;
    (void)raw_mode(0);
    render_line(prompt, buf, pos);

    for (;;) {
        char c = 0;
        long r = k_read(0, &c, 1);
        if (r <= 0) return -1;                     /* EOF */

        if (c == '\n' || c == '\r') {              /* Enter */
            buf[len] = 0;
            puts("\r\n");
            hist_add(buf);
            return len;
        }
        if (c == '\t') {                           /* TAB */
            (void)tab_complete(buf, &len, &pos, cap);
            render_line(prompt, buf, pos);
            continue;
        }
        if (c == 0x1b) {                           /* ESC sequence */
            char c2 = 0, c3 = 0;
            if (k_read(0, &c2, 1) <= 0) return -1;
            if (c2 != '[') continue;
            if (k_read(0, &c3, 1) <= 0) return -1;
            if (c3 == 'A') {                       /* up: history */
                const char *h = hist_up(buf);
                if (h) { scpy(buf, h); len = pos = (int)slen(h); }
            } else if (c3 == 'B') {                /* down: history */
                const char *h = hist_down();
                if (h) { scpy(buf, h); len = pos = (int)slen(h); }
            } else if (c3 == 'C') {                /* right */
                if (pos < len) pos++;
            } else if (c3 == 'D') {                /* left */
                if (pos > 0) pos--;
            } else if (c3 == 'H' || c3 == '1') {   /* home */
                pos = 0;
            } else if (c3 == 'F' || c3 == '4') {   /* end */
                pos = len;
            } else if (c3 == '3') {                /* delete */
                char c4 = 0;
                if (k_read(0, &c4, 1) <= 0) return -1;
                if (c4 == '~' && pos < len) {
                    for (int i = pos; i < len - 1; i++) buf[i] = buf[i + 1];
                    len--; buf[len] = 0;
                } else continue;
            } else continue;
            render_line(prompt, buf, pos);
            continue;
        }
        if (c == 0x03) {                           /* Ctrl-C: cancel line */
            len = 0; pos = 0; buf[0] = 0;
            puts("^C\r\n");
            render_line(prompt, buf, pos);
            continue;
        }
        if (c == 0x04) {                           /* Ctrl-D: EOF if empty */
            if (len == 0) { puts("^D\r\n"); return -1; }
            continue;
        }
        if (c == 0x0c) {                           /* Ctrl-L: clear screen */
            clrscr();
            render_line(prompt, buf, pos);
            continue;
        }
        if (c == 0x01) { pos = 0;      render_line(prompt, buf, pos); continue; } /* Ctrl-A */
        if (c == 0x05) { pos = len;   render_line(prompt, buf, pos); continue; } /* Ctrl-E */
        if (c == 0x15) {                       /* Ctrl-U: kill line */
            len = 0; pos = 0; buf[0] = 0;
            render_line(prompt, buf, pos);
            continue;
        }
        if (c == 0x7f || c == 0x08) {          /* backspace */
            if (pos > 0) {
                for (int i = pos - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; pos--; buf[len] = 0;
                render_line(prompt, buf, pos);
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7f) {           /* printable */
            if (len < cap - 1) {
                for (int i = len; i > pos; i--) buf[i] = buf[i - 1];
                buf[pos++] = c;
                len++;
                buf[len] = 0;
                render_line(prompt, buf, pos);
            }
            continue;
        }
        /* other control bytes ignored */
    }
}

/* ---- toyls ---- */
static void toy_ls(const char *path) {
    static char lsbuf[4096];
    int fd = (int)k_open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) { puts("toyls: cannot open '"); puts(path); puts("'\r\n"); return; }
    for (;;) {
        long n = sc3(SYS_getdents64, fd, (long)lsbuf, (long)sizeof lsbuf);
        if (n <= 0) break;
        u64 off = 0;
        while (off < (u64)n) {
            struct linux_dirent64 *de = (struct linux_dirent64 *)(lsbuf + off);
            puts(de->d_name);
            if (de->d_type == DT_DIR) putc('/');
            puts("\r\n");
            off += de->d_reclen;
        }
    }
    k_close(fd);
}

/* ---- toynano ---- */
#define NANO_MAX_LINES 512
#define NANO_LINE_LEN 240
static char nano_buf[NANO_MAX_LINES][NANO_LINE_LEN];
static int  nano_n = 0;

static void nano_append(const char *s) {
    if (nano_n >= NANO_MAX_LINES) { puts("[toynano] buffer full\r\n"); return; }
    u64 l = slen(s);
    if (l >= NANO_LINE_LEN) l = NANO_LINE_LEN - 1;
    for (u64 i = 0; i < l; i++) nano_buf[nano_n][i] = s[i];
    nano_buf[nano_n][l] = 0;
    nano_n++;
}

static int nano_load(const char *path) {
    nano_n = 0;
    int fd = (int)k_open(path, O_RDONLY);
    if (fd < 0) return -1;
    char tmp[NANO_LINE_LEN];
    int c = 0;
    for (;;) {
        char ch;
        long r = k_read(fd, &ch, 1);
        if (r <= 0) break;
        if (ch == '\n') {
            tmp[c] = 0;
            nano_append(tmp);
            c = 0;
        } else if (ch != '\r') {
            if (c < NANO_LINE_LEN - 1) tmp[c++] = ch;
        }
    }
    if (c > 0) { tmp[c] = 0; nano_append(tmp); }
    k_close(fd);
    return nano_n;
}

static long nano_save(const char *path) {
    int fd = (int)k_open3(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    long total = 0;
    for (int i = 0; i < nano_n; i++) {
        total += k_write(fd, nano_buf[i], slen(nano_buf[i]));
        total += k_write(fd, "\n", 1);
    }
    k_close(fd);
    return total;
}

static void nano_print(void) {
    for (int i = 0; i < nano_n; i++) {
        putu((u64)(i + 1)); puts(" | "); puts(nano_buf[i]); puts("\r\n");
    }
}

static long nano_atoi(const char *s) {
    while (*s == ' ') s++;
    long v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

static void nano_run(const char *path) {
    int loaded = nano_load(path);
    clrscr();
    puts("toynano - "); puts(path);
    if (loaded < 0) puts("  [new file]");
    else            puts("  [loaded]");
    puts("  lines: "); putu((u64)nano_n); puts("\r\n\r\n");
    puts("type lines to append. commands:\r\n");
    puts("  :w        save file\r\n");
    puts("  :q        quit (no save)\r\n");
    puts("  :x        save + quit\r\n");
    puts("  :d N      delete line N\r\n");
    puts("  :p        print buffer\r\n");
    puts("  :c        clear buffer\r\n");
    puts("  Ctrl-D    save + quit\r\n\r\n");
    nano_print();

    static char line[NANO_LINE_LEN];
    for (;;) {
        int n = readline("> ", line, sizeof line);
        if (n < 0) {
            long w = nano_save(path);
            puts("\r\n[toynano] wrote "); putu((u64)(w < 0 ? 0 : w)); puts(" bytes\r\n");
            return;
        }
        if (line[0] == ':') {
            if (scmp(line, ":w") == 0) {
                long w = nano_save(path);
                if (w < 0) puts("[toynano] save failed\r\n");
                else { puts("[toynano] wrote "); putu((u64)w); puts(" bytes\r\n"); }
            } else if (scmp(line, ":q") == 0) {
                return;
            } else if (scmp(line, ":x") == 0) {
                nano_save(path);
                return;
            } else if (scmp(line, ":p") == 0) {
                nano_print();
            } else if (scmp(line, ":c") == 0) {
                nano_n = 0;
                puts("[toynano] buffer cleared\r\n");
            } else if (starts_with(line, ":d ")) {
                long num = nano_atoi(line + 3);
                if (num >= 1 && num <= nano_n) {
                    for (int i = (int)num - 1; i < nano_n - 1; i++)
                        scpy(nano_buf[i], nano_buf[i + 1]);
                    nano_n--;
                    puts("[toynano] deleted line "); putu((u64)num); puts("\r\n");
                } else {
                    puts("[toynano] bad line number\r\n");
                }
            } else {
                puts("[toynano] unknown : command (w q x p c d N)\r\n");
            }
        } else {
            nano_append(line);
        }
    }
}

/* ---- help / power ---- */
static void show_help(void) {
    puts(
      "toyium - minimal command-line OS\r\n"
      "commands:\r\n"
      "  toyls [dir]    list directory contents\r\n"
      "  toycd <dir>    change current directory\r\n"
      "  toynano <file> tiny text editor\r\n"
      "  echo <text>    print text\r\n"
      "  clear          clear the screen\r\n"
      "  help           this message\r\n"
      "  poweroff       power off the machine\r\n"
      "  reboot         reboot the machine\r\n"
      "keys:\r\n"
      "  TAB            complete command / path\r\n"
      "  Up / Down      command history\r\n"
      "  Left/Right/Home/End/Delete, Backspace - edit line\r\n"
      "  Ctrl-A/E/U     start/end of line, kill line\r\n"
      "  Ctrl-C         cancel line\r\n"
      "  Ctrl-D         power off (empty line)\r\n"
      "  Ctrl-L         clear screen\r\n"
      "shift / caps lock work everywhere (kernel keymap)\r\n");
}

static int do_poweroff(void) {
    puts("\r\npowering off\r\n");
    sc1(SYS_sync, 0);
    sc6(SYS_reboot, 0xfee1dead, 672274793L, 0x4321fedcL, 0, 0, 0);
    return 1;
}

static int do_reboot(void) {
    puts("\r\nrebooting\r\n");
    sc6(SYS_reboot, 0xfee1dead, 672274793L, 0x1234567L, 0, 0, 0);
    return 1;
}

/* ---- command dispatch ---- */
static int run_line(char *line) {
    char *rest = line;
    while (*rest && *rest != ' ' && *rest != '\t') rest++;
    if (*rest) *rest++ = 0;
    while (*rest == ' ' || *rest == '\t') rest++;
    char *cmd = line;

    if (scmp(cmd, "echo") == 0) {
        puts(rest); puts("\r\n");
        return 0;
    }
    if (scmp(cmd, "clear") == 0) {
        clrscr();
        return 0;
    }
    if (scmp(cmd, "help") == 0) {
        show_help();
        return 0;
    }
    if (scmp(cmd, "poweroff") == 0 || scmp(cmd, "exit") == 0) {
        return do_poweroff();
    }
    if (scmp(cmd, "reboot") == 0) {
        return do_reboot();
    }

    /* commands that take exactly one path argument */
    if (scmp(cmd, "toyls") == 0 || scmp(cmd, "toycd") == 0 || scmp(cmd, "toynano") == 0) {
        const char *usage = (scmp(cmd, "toyls") == 0) ? "usage: toyls [dir]\r\n"
                          : (scmp(cmd, "toycd") == 0) ? "usage: toycd <dir>\r\n"
                          : "usage: toynano <file>\r\n";
        char *a1 = rest;
        int extra = 0;
        char *e = a1;
        while (*e && *e != ' ' && *e != '\t') e++;
        if (*e) {
            *e = 0;
            e++;
            while (*e == ' ' || *e == '\t') e++;
            if (*e) extra = 1;
        }
        if (extra) { puts(usage); return 0; }
        if (!*a1) {
            if (scmp(cmd, "toyls") == 0) { toy_ls("."); return 0; }
            puts(usage);
            return 0;
        }
        if (scmp(cmd, "toyls") == 0)   { toy_ls(a1);   return 0; }
        if (scmp(cmd, "toycd") == 0)   {
            if (k_chdir(a1) != 0) {
                puts("toycd: no such directory '"); puts(a1); puts("'\r\n");
            }
            return 0;
        }
        nano_run(a1);
        return 0;
    }

    puts("toyium: "); puts(cmd); puts(": command not found\r\n");
    puts("available: toyls, toycd, toynano, echo, clear, help, poweroff, reboot\r\n");
    return 0;
}

/* ---- boot-time smoke test (kernel arg: toyium=test) ---- */
static int selftest_enabled(void) {
    static char buf[512];
    int fd = (int)k_open("/proc/cmdline", O_RDONLY);
    if (fd < 0) return 0;
    long n = k_read(fd, buf, sizeof buf - 1);
    k_close(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    return contains(buf, "toyium=test");
}

static int check_str(const char *got, const char *want, const char *what) {
    if (scmp(got, want) == 0) { puts("PASS "); puts(what); puts("\r\n"); return 1; }
    puts("FAIL "); puts(what); puts(" got='"); puts(got); puts("' want='"); puts(want); puts("'\r\n");
    return 0;
}

static void run_selftest(void) {
    static const char *demo[] = {
        "toyls",
        "toycd /proc",
        "toyls",
        "toycd /",
        "toyls /sys",
        "toycd /definitely-not-a-dir",
        "help",
        "toycd /home",
        "toyls",
        "bogus-cmd",
        0
    };
    char buf[256];

    puts("== toyium self-test ==\r\n");

    /* command demo */
    for (int i = 0; demo[i]; i++) {
        scpy(buf, demo[i]);
        puts("> "); puts(demo[i]); puts("\r\n");
        if (run_line(buf)) return;
    }

    /* echo */
    puts("> echo hello from echo\r\n");
    scpy(buf, "echo hello from echo");
    if (run_line(buf)) return;

    /* TAB completion unit tests */
    puts("== completion tests ==\r\n");
    static char b[64];
    int len, pos;

    scpy(b, "toyl"); len = 4; pos = 4;
    (void)tab_complete(b, &len, &pos, 64);
    b[len] = 0;
    check_str(b, "toyls ", "complete 'toyl' -> 'toyls '");

    scpy(b, "to"); len = 2; pos = 2;
    (void)tab_complete(b, &len, &pos, 64);
    b[len] = 0;
    check_str(b, "toy", "ambiguous 'to' -> common prefix 'toy'");

    k_chdir("/");
    scpy(b, "/pr"); len = 3; pos = 3;
    (void)tab_complete(b, &len, &pos, 64);
    b[len] = 0;
    check_str(b, "/proc/", "complete '/pr' -> '/proc/'");

    scpy(b, "toycd /ru"); len = 9; pos = 9;
    (void)tab_complete(b, &len, &pos, 64);
    b[len] = 0;
    check_str(b, "toycd /run/", "complete 'toycd /ru' -> 'toycd /run/'");

    /* history tests */
    puts("== history tests ==\r\n");
    hist_add("alpha");
    hist_add("beta");
    const char *h = hist_up("cur");
    check_str(h ? h : "(null)", "beta", "history up #1");
    h = hist_up("");
    check_str(h ? h : "(null)", "alpha", "history up #2");
    h = hist_down();
    check_str(h ? h : "(null)", "beta", "history down");

    /* toynano round-trip */
    puts("== toynano tests ==\r\n");
    nano_n = 0;
    nano_append("hello from toynano");
    nano_append("second line");
    long w = nano_save("/tmp/note.txt");
    puts("saved "); putu((u64)(w < 0 ? 0 : w)); puts(" bytes\r\n");
    int lines = nano_load("/tmp/note.txt");
    puts("reloaded lines: "); putu((u64)(lines < 0 ? 0 : lines)); puts("\r\n");
    nano_print();
    if (lines == 2
        && scmp(nano_buf[0], "hello from toynano") == 0
        && scmp(nano_buf[1], "second line") == 0)
        puts("PASS toynano round-trip\r\n");
    else
        puts("FAIL toynano round-trip\r\n");

    puts("> toyls /tmp\r\n");
    scpy(buf, "toyls /tmp");
    if (run_line(buf)) return;

    puts("== self-test done ==\r\n");
    do_poweroff();
}

/* ---- entry point ---- */
int _start(void) {
    static char cwd[512];
    static char line[512];
    static char prompt[600];

    /* bring up the virtual filesystems the REPL needs */
    if (k_mkdir("/proc", 0755) != 0) {}
    if (k_mkdir("/sys", 0755) != 0) {}
    if (k_mkdir("/dev", 0755) != 0) {}
    if (k_mkdir("/tmp", 0777) != 0) {}
    if (k_mkdir("/run", 0755) != 0) {}
    if (k_mount("none", "/proc", "proc", 0, 0) != 0) { /* non-fatal */ }
    if (k_mount("none", "/sys", "sysfs", 0, 0) != 0) { }
    if (k_mount("none", "/dev", "devtmpfs", 0, 0) != 0) {
        k_mount("none", "/dev", "tmpfs", 0, 0);
    }
    k_mount("none", "/tmp", "tmpfs", 0, 0);
    k_mount("none", "/run", "tmpfs", 0, 0);

    sc2(SYS_sethostname, (long)"toyium", 6);

    puts("\r\n");
    puts("   _____           _\r\n");
    puts("  |_   _|__  _   _(_)_   _ _ __ ___\r\n");
    puts("    | |/ _ \\| | | | | | | | | '_ ` _ \\\r\n");
    puts("    | | (_) | |_| | | |_| | | | | | |\r\n");
    puts("    |_|\\___/ \\__, |_|\\__,_|_| |_| |_|\r\n");
    puts("             |___/   Toyium OS\r\n");
    puts("kernel: ");
    {
        struct utsname u;
        if (sc1(SYS_uname, (long)&u) == 0) { puts(u.release); }
    }
    puts("  made by xex & ayham\r\n");
    puts("  (commands: toyls, toycd, toynano, echo, clear, help, poweroff, reboot)\r\n\r\n");
    puts("Type 'help' for usage. TAB completes, Up/Down = history.\r\n\r\n");

    if (selftest_enabled()) {
        run_selftest();
        for (;;) sc0(SYS_exit_group);
    }

    for (;;) {
        long g = k_getcwd(cwd, sizeof cwd);
        scpy(prompt, "toyium:");
        if (g > 0) {
            u64 l = slen(prompt);
            u64 i = 0;
            while (cwd[i] && l < sizeof prompt - 3) prompt[l++] = cwd[i++];
            prompt[l] = 0;
        } else {
            scpy(prompt, "toyium:/");
        }
        scpy(prompt + slen(prompt), "# ");

        int n = readline(prompt, line, sizeof line);
        if (n < 0) {
            do_poweroff();
            for (;;) sc0(SYS_exit_group);
        }
        run_line(line);
    }
}
