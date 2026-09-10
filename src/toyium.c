/*
 * toyium.c - Toyium OS /init
 *
 * Freestanding x86_64 PID1 for an initramfs-only toy kernel.
 * No libc, no busybox. Minimal kernel setup, mounts the toyfs filesystem,
 * then a REPL exposing:
 *
 *   toyls [dir]    list directory entries
 *   toycd <dir>    change directory
 *   toypwd         print working directory
 *   toycat <file>  print a file
 *   toynano <file> full-screen text editor
 *   echo <text>    print text
 *   clear          clear the screen
 *   help           command help
 *   poweroff       power off the machine
 *   reboot         reboot the machine
 *
 * made by xex & ayham
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

#define O_RDONLY        0
#define O_WRONLY        1
#define O_CREAT         0100
#define O_TRUNC         01000
#define O_DIRECTORY     00200000

#define TCGETS          0x5401
#define TCSETS          0x5402
#define TIOCGWINSZ      0x5413

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

static long k_write(int fd, const void *buf, u64 len) { return sc3(SYS_write, fd, (long)buf, (long)len); }
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
#define DT_REG     8

struct termios {
    u32 c_iflag, c_oflag, c_cflag, c_lflag;
    u8  c_line;
    u8  c_cc[19];
};

struct winsize { u16 ws_row, ws_col, ws_xpixel, ws_ypixel; };

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

static int contains(const char *hay, const char *needle) {
    u64 i, j;
    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j] && hay[i + j] == needle[j]; j++);
        if (needle[j] == 0) return 1;
    }
    return 0;
}

static void clrscr(void) { puts("\x1b[2J\x1b[H"); }

/* ---- terminal ---- */
static int raw_mode(int fd) {
    struct termios t;
    if (sc3(SYS_ioctl, fd, TCGETS, (long)&t) != 0) return 0;
    t.c_iflag = 0;                /* no ICRNL/IXON/ISTRIP/... : true raw input */
    t.c_lflag &= ~(u32)(0x000B);  /* ICANON | ECHO | ISIG */
    t.c_oflag &= ~(u32)0x0001;    /* OPOST */
    t.c_cc[5] = 0;                /* VTIME */
    t.c_cc[6] = 1;                /* VMIN */
    if (sc3(SYS_ioctl, fd, TCSETS, (long)&t) != 0) return 0;
    return 1;
}

/* ---- boot cmdline / console type ---- */
static char g_cmdline[512];
static int  g_serial = 0;
static int  g_selftest = 0;

static void probe_cmdline(void) {
    int fd = (int)k_open("/proc/cmdline", O_RDONLY);
    g_cmdline[0] = 0;
    if (fd >= 0) {
        long n = k_read(fd, g_cmdline, sizeof g_cmdline - 1);
        k_close(fd);
        if (n > 0) g_cmdline[n] = 0; else g_cmdline[0] = 0;
    }
    g_serial = contains(g_cmdline, "console=ttyS");
    g_selftest = contains(g_cmdline, "toyium=test");
}

static void console_setup(void) {
    if (g_serial) {
        puts("\x1b[1 q");     /* DECSCUSR: blinking block (host terminal) */
    } else {
        puts("\x1b[?6c");     /* Linux VT: full block cursor */
    }
    puts("\x1b[?25h");        /* show cursor */
}

static void set_title(const char *cwd) {
    if (!g_serial) return;    /* Linux VT ignores OSC; don't emit garbage */
    puts("\x1b]0;toyium:");
    puts(cwd);
    putc('\x07');
}

/* ---- command history ---- */
#define HIST_MAX 32
#define HIST_LEN 256
static char hist[HIST_MAX][HIST_LEN];
static int  hist_n = 0;
static int  hist_pos = 0;
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

/* ---- commands ---- */
#define NCMD 10
static const char *cmd_names[NCMD] = {
    "toyls", "toycd", "toypwd", "toycat", "toynano",
    "echo", "clear", "help", "poweroff", "reboot"
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
static char tc_names[64][132];

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
        int cut = -1;
        for (int i = 0; i < toklen; i++) if (tok[i] == '/') cut = i;
        char dir[160];
        int dl = 0, skip_dir = 0;
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

/* ---- line reader with editing, history, completion ---- */
static void render_line(const char *prompt, const char *buf, int pos) {
    puts("\r\x1b[K");
    puts(prompt);
    puts(buf);
    putc('\r');
    int n = (int)slen(prompt) + pos;
    if (n > 0) { puts("\x1b["); putu((u64)n); putc('C'); }
}

static int readline(const char *prompt, char *buf, int cap) {
    int len = 0, pos = 0;
    buf[0] = 0;
    hist_pos = hist_n;
    (void)raw_mode(0);
    render_line(prompt, buf, pos);

    for (;;) {
        char c = 0;
        long r = k_read(0, &c, 1);
        if (r <= 0) return -1;

        if (c == '\n' || c == '\r') {
            buf[len] = 0;
            puts("\r\n");
            hist_add(buf);
            return len;
        }
        if (c == '\t') {
            (void)tab_complete(buf, &len, &pos, cap);
            render_line(prompt, buf, pos);
            continue;
        }
        if (c == 0x1b) {
            char c2 = 0, c3 = 0;
            if (k_read(0, &c2, 1) <= 0) return -1;
            if (c2 != '[') continue;
            if (k_read(0, &c3, 1) <= 0) return -1;
            if (c3 == 'A') {
                const char *h = hist_up(buf);
                if (h) { scpy(buf, h); len = pos = (int)slen(h); }
            } else if (c3 == 'B') {
                const char *h = hist_down();
                if (h) { scpy(buf, h); len = pos = (int)slen(h); }
            } else if (c3 == 'C') {
                if (pos < len) pos++;
            } else if (c3 == 'D') {
                if (pos > 0) pos--;
            } else if (c3 == 'H' || c3 == '1') {
                pos = 0;
            } else if (c3 == 'F' || c3 == '4') {
                pos = len;
            } else if (c3 == '3') {
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
        if (c == 0x03) {
            len = 0; pos = 0; buf[0] = 0;
            puts("^C\r\n");
            render_line(prompt, buf, pos);
            continue;
        }
        if (c == 0x04) {
            if (len == 0) { puts("^D\r\n"); return -1; }
            continue;
        }
        if (c == 0x0c) {
            clrscr();
            console_setup();
            render_line(prompt, buf, pos);
            continue;
        }
        if (c == 0x01) { pos = 0;      render_line(prompt, buf, pos); continue; }
        if (c == 0x05) { pos = len;    render_line(prompt, buf, pos); continue; }
        if (c == 0x15) { len = 0; pos = 0; buf[0] = 0; render_line(prompt, buf, pos); continue; }
        if (c == 0x7f || c == 0x08) {
            if (pos > 0) {
                for (int i = pos - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; pos--; buf[len] = 0;
                render_line(prompt, buf, pos);
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7f) {
            if (len < cap - 1) {
                for (int i = len; i > pos; i--) buf[i] = buf[i - 1];
                buf[pos++] = c;
                len++;
                buf[len] = 0;
                render_line(prompt, buf, pos);
            }
            continue;
        }
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

/* ---- toycat ---- */
static void toy_cat(const char *path) {
    static char buf[1024];
    int fd = (int)k_open(path, O_RDONLY);
    if (fd < 0) { puts("toycat: "); puts(path); puts(": no such file\r\n"); return; }
    for (;;) {
        long n = k_read(fd, buf, sizeof buf);
        if (n <= 0) break;
        k_write(1, buf, (u64)n);
    }
    k_close(fd);
}

/* ---- toypwd ---- */
static void toy_pwd(void) {
    static char cwd[512];
    if (k_getcwd(cwd, sizeof cwd) > 0) puts(cwd);
    else puts("/");
    puts("\r\n");
}

/* =====================================================================
 * toynano - full screen editor
 * ===================================================================== */
#define ED_MAXL  1024
#define ED_LMAX  240

static char ed[ED_MAXL][ED_LMAX];
static int  edlen[ED_MAXL];
static int  edn = 1;
static int  ecx = 0, ecy = 0, etop = 0, egoalx = 0;
static int  emodified = 0;
static char ed_file[256];
static int  ed_rows = 24, ed_cols = 80;
static char ed_msg[80];
static char ed_cutbuf[ED_LMAX];
static int  ed_cutlen = 0, ed_have_cut = 0;

/* key codes */
#define K_UP    (-1)
#define K_DOWN  (-2)
#define K_LEFT  (-3)
#define K_RIGHT (-4)
#define K_HOME  (-5)
#define K_END   (-6)
#define K_DEL   (-7)
#define K_PGUP  (-8)
#define K_PGDN  (-9)
#define K_EOF   (-10)

static int ed_getkey(void) {
    char c = 0;
    long r = k_read(0, &c, 1);
    if (r <= 0) return K_EOF;
    if (c != 0x1b) return (unsigned char)c;
    char c2 = 0, c3 = 0;
    if (k_read(0, &c2, 1) <= 0) return K_EOF;
    if (c2 != '[') return 0;
    if (k_read(0, &c3, 1) <= 0) return K_EOF;
    char c4 = 0;
    switch (c3) {
    case 'A': return K_UP;
    case 'B': return K_DOWN;
    case 'C': return K_RIGHT;
    case 'D': return K_LEFT;
    case 'H': return K_HOME;
    case 'F': return K_END;
    case '1': case '7': if (k_read(0, &c4, 1) > 0 && c4 == '~') return K_HOME; return 0;
    case '4': case '8': if (k_read(0, &c4, 1) > 0 && c4 == '~') return K_END;  return 0;
    case '3': if (k_read(0, &c4, 1) > 0 && c4 == '~') return K_DEL;   return 0;
    case '5': if (k_read(0, &c4, 1) > 0 && c4 == '~') return K_PGUP;  return 0;
    case '6': if (k_read(0, &c4, 1) > 0 && c4 == '~') return K_PGDN;  return 0;
    }
    return 0;
}

static void move_to(int row, int col) {
    puts("\x1b["); putu((u64)row); putc(';'); putu((u64)col); putc('H');
}

static int ed_text_h(void) {
    int h = ed_rows - 2;
    return h < 1 ? 1 : h;
}

static void ed_ensure_visible(void) {
    int h = ed_text_h();
    if (ecy < etop) etop = ecy;
    if (ecy > etop + h - 1) etop = ecy - (h - 1);
    if (etop < 0) etop = 0;
}

static void ed_render(void) {
    puts("\x1b[?25l");
    /* title bar */
    move_to(1, 1);
    puts("\x1b[7m");
    puts("  toynano   ");
    puts(ed_file);
    if (emodified) puts("   [modified]");
    puts("\x1b[K\x1b[0m");

    /* text area */
    int h = ed_text_h();
    for (int i = 0; i < h; i++) {
        int ln = etop + i;
        move_to(2 + i, 1);
        if (ln < edn) {
            int l = edlen[ln];
            if (l > ed_cols) l = ed_cols;
            k_write(1, ed[ln], (u64)l);
        }
        puts("\x1b[K");
    }

    /* status bar */
    move_to(ed_rows, 1);
    puts("\x1b[7m");
    if (ed_msg[0]) {
        puts(ed_msg);
    } else {
        puts(" ^O Save  ^X Exit  ^K Cut  ^U Paste  ^S Save");
    }
    puts("\x1b[K");
    puts("  Ln ");
    putu((u64)(ecy + 1));
    puts(", Col ");
    putu((u64)(ecx + 1));
    puts("\x1b[0m");
    ed_msg[0] = 0;

    /* cursor */
    move_to(2 + (ecy - etop), ecx + 1);
    puts("\x1b[?25h");
}

static void ed_insert(char c) {
    if (edlen[ecy] < ED_LMAX - 1) {
        for (int i = edlen[ecy]; i > ecx; i--) ed[ecy][i] = ed[ecy][i - 1];
        ed[ecy][ecx] = c;
        edlen[ecy]++;
        ed[ecy][edlen[ecy]] = 0;
        ecx++;
        emodified = 1;
    }
}

static void ed_newline(void) {
    if (edn >= ED_MAXL) return;
    int taillen = edlen[ecy] - ecx;
    char tail[ED_LMAX];
    for (int i = 0; i < taillen; i++) tail[i] = ed[ecy][ecx + i];
    edlen[ecy] = ecx;
    ed[ecy][ecx] = 0;
    for (int i = edn; i > ecy + 1; i--) {
        for (int j = 0; j <= edlen[i - 1]; j++) ed[i][j] = ed[i - 1][j];
        edlen[i] = edlen[i - 1];
    }
    for (int i = 0; i < taillen; i++) ed[ecy + 1][i] = tail[i];
    ed[ecy + 1][taillen] = 0;
    edlen[ecy + 1] = taillen;
    edn++;
    ecy++;
    ecx = 0;
    emodified = 1;
}

static void ed_backspace(void) {
    if (ecx > 0) {
        for (int i = ecx - 1; i < edlen[ecy] - 1; i++) ed[ecy][i] = ed[ecy][i + 1];
        edlen[ecy]--; ecx--; ed[ecy][edlen[ecy]] = 0;
        emodified = 1;
    } else if (ecy > 0) {
        int prev = edlen[ecy - 1];
        if (prev + edlen[ecy] < ED_LMAX - 1) {
            for (int i = 0; i < edlen[ecy]; i++) ed[ecy - 1][prev + i] = ed[ecy][i];
            edlen[ecy - 1] = prev + edlen[ecy];
            ed[ecy - 1][edlen[ecy - 1]] = 0;
            for (int i = ecy; i < edn - 1; i++) {
                for (int j = 0; j <= edlen[i + 1]; j++) ed[i][j] = ed[i + 1][j];
                edlen[i] = edlen[i + 1];
            }
            edn--;
            ecy--;
            ecx = prev;
            emodified = 1;
        }
    }
}

static void ed_delete(void) {
    if (ecx < edlen[ecy]) {
        for (int i = ecx; i < edlen[ecy] - 1; i++) ed[ecy][i] = ed[ecy][i + 1];
        edlen[ecy]--; ed[ecy][edlen[ecy]] = 0;
        emodified = 1;
    } else if (ecy < edn - 1) {
        int cur = edlen[ecy];
        if (cur + edlen[ecy + 1] < ED_LMAX - 1) {
            for (int i = 0; i < edlen[ecy + 1]; i++) ed[ecy][cur + i] = ed[ecy + 1][i];
            edlen[ecy] = cur + edlen[ecy + 1];
            ed[ecy][edlen[ecy]] = 0;
            for (int i = ecy + 1; i < edn - 1; i++) {
                for (int j = 0; j <= edlen[i + 1]; j++) ed[i][j] = ed[i + 1][j];
                edlen[i] = edlen[i + 1];
            }
            edn--;
            emodified = 1;
        }
    }
}

static long ed_save(void) {
    int fd = (int)k_open3(ed_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    long total = 0;
    for (int i = 0; i < edn; i++) {
        total += k_write(fd, ed[i], (u64)edlen[i]);
        total += k_write(fd, "\n", 1);
    }
    k_close(fd);
    emodified = 0;
    return total;
}

static void ed_load(const char *path) {
    edn = 1; edlen[0] = 0; ed[0][0] = 0;
    ecx = ecy = etop = egoalx = 0;
    emodified = 0;
    int fd = (int)k_open(path, O_RDONLY);
    if (fd < 0) return;
    char c;
    int ended_nl = 0;
    for (;;) {
        long r = k_read(fd, &c, 1);
        if (r <= 0) break;
        if (c == '\n') { ed_newline(); ended_nl = 1; }
        else if (c != '\r') { ed_insert(c); ended_nl = 0; }
    }
    k_close(fd);
    /* a trailing newline must not create an extra empty line */
    if (ended_nl && edn > 1 && edlen[edn - 1] == 0) edn--;
    emodified = 0;
    ecy = 0; ecx = 0; etop = 0;
}

static void ed_cut(void) {
    int l = edlen[ecy];
    if (l >= ED_LMAX) l = ED_LMAX - 1;
    for (int i = 0; i < l; i++) ed_cutbuf[i] = ed[ecy][i];
    ed_cutbuf[l] = 0;
    ed_cutlen = l;
    ed_have_cut = 1;
    if (edn > 1) {
        for (int i = ecy; i < edn - 1; i++) {
            for (int j = 0; j <= edlen[i + 1]; j++) ed[i][j] = ed[i + 1][j];
            edlen[i] = edlen[i + 1];
        }
        edn--;
    } else {
        edlen[ecy] = 0; ed[ecy][0] = 0;
    }
    if (ecy >= edn) ecy = edn - 1;
    ecx = 0;
    emodified = 1;
}

static void ed_paste(void) {
    if (!ed_have_cut) return;
    if (edn < ED_MAXL) {
        for (int i = edn; i > ecy; i--) {
            for (int j = 0; j <= edlen[i - 1]; j++) ed[i][j] = ed[i - 1][j];
            edlen[i] = edlen[i - 1];
        }
        edn++;
    }
    for (int i = 0; i < ed_cutlen; i++) ed[ecy][i] = ed_cutbuf[i];
    ed[ecy][ed_cutlen] = 0;
    edlen[ecy] = ed_cutlen;
    ecx = ed_cutlen;
    emodified = 1;
}

/* returns 1 if the editor should exit */
static int ed_confirm_exit(void) {
    if (!emodified) return 1;
    move_to(ed_rows, 1);
    puts("\x1b[7m Save modified buffer? (y/n/c) \x1b[K\x1b[0m");
    int k = ed_getkey();
    if (k == 'y' || k == 'Y') { ed_save(); return 1; }
    if (k == 'n' || k == 'N') return 1;
    return 0;
}

static void nano_run(const char *path) {
    struct winsize ws;
    if (sc3(SYS_ioctl, 0, TIOCGWINSZ, (long)&ws) == 0 && ws.ws_row && ws.ws_col) {
        ed_rows = ws.ws_row; ed_cols = ws.ws_col;
    }
    if (ed_rows < 4) ed_rows = 4;
    if (ed_cols < 20) ed_cols = 20;
    if (ed_cols > ED_LMAX - 1) ed_cols = ED_LMAX - 1;

    {
        int i = 0;
        while (path[i] && i < 255) { ed_file[i] = path[i]; i++; }
        ed_file[i] = 0;
    }

    ed_load(path);
    (void)raw_mode(0);
    clrscr();

    for (;;) {
        ed_ensure_visible();
        ed_render();
        int k = ed_getkey();

        if (k == K_EOF) {
            if (ed_confirm_exit()) break; else continue;
        } else if (k == K_UP) {
            egoalx = ecx;
            if (ecy > 0) { ecy--; ecx = (egoalx < edlen[ecy]) ? egoalx : edlen[ecy]; }
        } else if (k == K_DOWN) {
            egoalx = ecx;
            if (ecy < edn - 1) { ecy++; ecx = (egoalx < edlen[ecy]) ? egoalx : edlen[ecy]; }
        } else if (k == K_LEFT) {
            if (ecx > 0) ecx--;
            else if (ecy > 0) { ecy--; ecx = edlen[ecy]; }
        } else if (k == K_RIGHT) {
            if (ecx < edlen[ecy]) ecx++;
            else if (ecy < edn - 1) { ecy++; ecx = 0; }
        } else if (k == K_HOME) {
            ecx = 0;
        } else if (k == K_END) {
            ecx = edlen[ecy];
        } else if (k == K_DEL) {
            ed_delete();
        } else if (k == K_PGUP) {
            int h = ed_text_h();
            ecy -= h; if (ecy < 0) ecy = 0;
            ecx = (ecx < edlen[ecy]) ? ecx : edlen[ecy];
        } else if (k == K_PGDN) {
            int h = ed_text_h();
            ecy += h; if (ecy > edn - 1) ecy = edn - 1;
            ecx = (ecx < edlen[ecy]) ? ecx : edlen[ecy];
        } else if (k == '\n' || k == '\r') {
            ed_newline();
        } else if (k == 0x7f || k == 0x08) {
            ed_backspace();
        } else if (k == '\t') {
            int next = (ecx / 8 + 1) * 8;
            while (ecx < next && ecx < ED_LMAX - 1) ed_insert(' ');
        } else if (k == 0x0f || k == 0x13) {      /* Ctrl-O / Ctrl-S: save */
            long w = ed_save();
            if (w < 0) scpy(ed_msg, " [save failed]");
        } else if (k == 0x18) {                     /* Ctrl-X: exit */
            if (ed_confirm_exit()) break; else continue;
        } else if (k == 0x0b) {                     /* Ctrl-K: cut line */
            ed_cut();
        } else if (k == 0x15) {                     /* Ctrl-U: paste */
            ed_paste();
        } else if (k == 0x03) {                     /* Ctrl-C: position */
            scpy(ed_msg, " line/col shown right");
        } else if (k >= 0x20 && k < 0x7f) {
            ed_insert((char)k);
        }
    }

    clrscr();
    console_setup();
}

/* ---- help / power ---- */
static void show_help(void) {
    puts(
      "toyium - minimal command-line OS  (made by xex & ayham)\r\n"
      "commands:\r\n"
      "  toyls [dir]    list directory contents\r\n"
      "  toycd <dir>    change current directory\r\n"
      "  toypwd         print working directory\r\n"
      "  toycat <file>  print a file\r\n"
      "  toynano <file> full-screen text editor\r\n"
      "  echo <text>    print text\r\n"
      "  clear          clear the screen\r\n"
      "  help           this message\r\n"
      "  poweroff       power off the machine\r\n"
      "  reboot         reboot the machine\r\n"
      "keys (shell):\r\n"
      "  TAB            complete command / path\r\n"
      "  Up / Down      command history\r\n"
      "  Left/Right/Home/End/Delete, Backspace - edit line\r\n"
      "  Ctrl-A/E/U     start/end of line, kill line\r\n"
      "  Ctrl-C         cancel line\r\n"
      "  Ctrl-D         power off (empty line)\r\n"
      "  Ctrl-L         clear screen\r\n"
      "keys (toynano):\r\n"
      "  arrows, Home/End, PgUp/PgDn, Enter, Tab, Backspace, Delete\r\n"
      "  Ctrl-S / Ctrl-O save      Ctrl-X exit\r\n"
      "  Ctrl-K cut line           Ctrl-U paste line\r\n"
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

    if (scmp(cmd, "echo") == 0) { puts(rest); puts("\r\n"); return 0; }
    if (scmp(cmd, "clear") == 0) { clrscr(); console_setup(); return 0; }
    if (scmp(cmd, "help") == 0) { show_help(); return 0; }
    if (scmp(cmd, "toypwd") == 0) { toy_pwd(); return 0; }
    if (scmp(cmd, "poweroff") == 0 || scmp(cmd, "exit") == 0) return do_poweroff();
    if (scmp(cmd, "reboot") == 0) return do_reboot();

    if (scmp(cmd, "toyls") == 0 || scmp(cmd, "toycd") == 0 ||
        scmp(cmd, "toycat") == 0 || scmp(cmd, "toynano") == 0) {
        const char *usage =
            scmp(cmd, "toyls") == 0   ? "usage: toyls [dir]\r\n" :
            scmp(cmd, "toycd") == 0   ? "usage: toycd <dir>\r\n" :
            scmp(cmd, "toycat") == 0  ? "usage: toycat <file>\r\n" :
                                        "usage: toynano <file>\r\n";
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
        if (scmp(cmd, "toyls") == 0)  { toy_ls(a1);  return 0; }
        if (scmp(cmd, "toycat") == 0) { toy_cat(a1); return 0; }
        if (scmp(cmd, "toycd") == 0) {
            if (k_chdir(a1) != 0) {
                puts("toycd: no such directory '"); puts(a1); puts("'\r\n");
            }
            return 0;
        }
        nano_run(a1);
        return 0;
    }

    puts("toyium: "); puts(cmd); puts(": command not found\r\n");
    puts("available: toyls, toycd, toypwd, toycat, toynano, echo, clear, help, poweroff, reboot\r\n");
    return 0;
}

/* ---- boot-time smoke test (kernel arg: toyium=test) ---- */
static int check_str(const char *got, const char *want, const char *what) {
    if (scmp(got, want) == 0) { puts("PASS "); puts(what); puts("\r\n"); return 1; }
    puts("FAIL "); puts(what); puts(" got='"); puts(got); puts("' want='"); puts(want); puts("'\r\n");
    return 0;
}

static void report(int ok, const char *what) {
    puts(ok ? "PASS " : "FAIL ");
    puts(what);
    puts("\r\n");
}

static void run_selftest(void) {
    static const char *demo[] = {
        "toyls", "toycd /proc", "toyls", "toycd /", "toyls /sys",
        "toycd /definitely-not-a-dir", "help", "toycd /toy", "toyls",
        "bogus-cmd", 0
    };
    char buf[256];

    puts("== toyium self-test ==\r\n");

    for (int i = 0; demo[i]; i++) {
        scpy(buf, demo[i]);
        puts("> "); puts(demo[i]); puts("\r\n");
        if (run_line(buf)) return;
    }

    puts("> echo hello from echo\r\n");
    scpy(buf, "echo hello from echo");
    if (run_line(buf)) return;

    puts("> toypwd\r\n");
    scpy(buf, "toypwd");
    if (run_line(buf)) return;

    /* toyfs file round-trip via toycat */
    puts("== toyfs tests ==\r\n");
    {
        int fd = (int)k_open3("/toy/selftest.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            const char *msg = "toyfs says hello\nsecond line\n";
            k_write(fd, msg, slen(msg));
            k_close(fd);
            puts("PASS toyfs create/write\r\n");
        } else {
            puts("FAIL toyfs create/write\r\n");
        }
    }
    puts("> toycat /toy/selftest.txt\r\n");
    toy_cat("/toy/selftest.txt");

    /* completion unit tests */
    puts("== completion tests ==\r\n");
    static char b[64];
    int len, pos;

    scpy(b, "toyl"); len = 4; pos = 4;
    (void)tab_complete(b, &len, &pos, 64); b[len] = 0;
    check_str(b, "toyls ", "complete 'toyl' -> 'toyls '");

    scpy(b, "to"); len = 2; pos = 2;
    (void)tab_complete(b, &len, &pos, 64); b[len] = 0;
    check_str(b, "toy", "ambiguous 'to' -> common prefix 'toy'");

    scpy(b, "/pr"); len = 3; pos = 3;
    (void)tab_complete(b, &len, &pos, 64); b[len] = 0;
    check_str(b, "/proc/", "complete '/pr' -> '/proc/'");

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

    /* toynano buffer unit test (non-interactive) */
    puts("== toynano buffer test ==\r\n");
    ed_load("/toy/selftest.txt");
    if (edn >= 2 && scmp(ed[0], "toyfs says hello") == 0) puts("PASS toynano load\r\n");
    else puts("FAIL toynano load\r\n");

    /* toynano editing unit tests */
    puts("== toynano edit tests ==\r\n");
    edn = 1; edlen[0] = 0; ed[0][0] = 0; ecy = 0; ecx = 0; emodified = 0;
    ed_insert('h'); ed_insert('i');
    ed_newline();
    ed_insert('y'); ed_insert('o'); ed_insert('u');
    report(edn == 2 && scmp(ed[0], "hi") == 0 && scmp(ed[1], "you") == 0,
           "insert + newline");
    ecy = 1; ecx = 0;
    ed_backspace();
    report(edn == 1 && scmp(ed[0], "hiyou") == 0, "backspace joins lines");
    ecx = 2; ed_insert('X');
    ecx = 2; ed_delete();
    report(scmp(ed[0], "hiyou") == 0, "mid-line insert/delete");
    scpy(ed_file, "/toy/edit.txt");
    ed_save();
    ed_load("/toy/edit.txt");
    report(edn == 1 && scmp(ed[0], "hiyou") == 0, "save + reload");

    puts("== self-test done ==\r\n");
    do_poweroff();
}

/* ---- entry point ---- */
int _start(void) {
    static char cwd[512];
    static char line[512];
    static char prompt[600];

    if (k_mkdir("/proc", 0755) != 0) {}
    if (k_mkdir("/sys", 0755) != 0) {}
    if (k_mkdir("/dev", 0755) != 0) {}
    if (k_mkdir("/tmp", 0777) != 0) {}
    if (k_mkdir("/run", 0755) != 0) {}
    if (k_mkdir("/toy", 0755) != 0) {}
    if (k_mount("none", "/proc", "proc", 0, 0) != 0) { }
    if (k_mount("none", "/sys", "sysfs", 0, 0) != 0) { }
    if (k_mount("none", "/dev", "devtmpfs", 0, 0) != 0) {
        k_mount("none", "/dev", "tmpfs", 0, 0);
    }
    k_mount("none", "/tmp", "tmpfs", 0, 0);
    k_mount("none", "/run", "tmpfs", 0, 0);

    /* mount the Toyium filesystem */
    if (k_mount("none", "/toy", "toyfs", 0, 0) == 0) {
        int fd = (int)k_open3("/toy/welcome.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            const char *msg =
                "Welcome to Toyium OS!\n"
                "This file lives on toyfs - the Toyium filesystem.\n"
                "made by xex & ayham\n";
            k_write(fd, msg, slen(msg));
            k_close(fd);
        }
    }

    sc2(SYS_sethostname, (long)"toyium", 6);
    probe_cmdline();
    console_setup();

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
    puts("  (commands: toyls, toycd, toypwd, toycat, toynano, echo, clear, help, poweroff, reboot)\r\n\r\n");
    puts("Type 'help' for usage. TAB completes, Up/Down = history.\r\n\r\n");

    if (g_selftest) {
        run_selftest();
        for (;;) sc0(SYS_exit_group);
    }

    k_chdir("/toy");
    for (;;) {
        long g = k_getcwd(cwd, sizeof cwd);
        scpy(prompt, "toyium:");
        if (g > 0) {
            u64 l = slen(prompt), i = 0;
            while (cwd[i] && l < sizeof prompt - 3) prompt[l++] = cwd[i++];
            prompt[l] = 0;
        } else {
            scpy(prompt, "toyium:/");
        }
        scpy(prompt + slen(prompt), "# ");
        if (g > 0) set_title(cwd);

        int n = readline(prompt, line, sizeof line);
        if (n < 0) {
            do_poweroff();
            for (;;) sc0(SYS_exit_group);
        }
        run_line(line);
    }
}
