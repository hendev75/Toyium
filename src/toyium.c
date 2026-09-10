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
 *   toynano <file> full-screen text edito
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
#define SYS_socket      41
#define SYS_connect     42
#define SYS_bind        49
#define SYS_sendto      44
#define SYS_recvfrom    45
#define SYS_setsockopt  54
#define SYS_shutdown    48
#define SYS_clock_gettime 228
#define SYS_nanosleep   35
#define SYS_statfs      137
#define SYS_bind        49
#define SYS_listen      50
#define SYS_accept      43
#define SYS_clock_settime 227

#define AF_INET         2
#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17
#define IPPROTO_ICMP    1
#define SOCK_RAW        3
#define SOL_SOCKET      1
#define SO_RCVTIMEO     20
#define SO_SNDTIMEO     21
#define SIOCGIFADDR     0x8915

struct sockaddr_in {
    u16 sin_family;
    u16 sin_port;
    u32 sin_addr;
    u8  sin_zero[8];
};

struct ifreq_addr {
    char ifr_name[16];
    u16  sa_family;
    u8   sa_data[14];
};

struct k_timeval {
    long tv_sec;
    long tv_usec;
};

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
static inline long sc4(long n, long a, long b, long c, long d) { long r; register long r10 asm("r10")=d; asm volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory"); return r; }
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
static long k_socket(long dom, long type, long proto)   { return sc3(SYS_socket, dom, type, proto); }
static long k_connect(long fd, const void *a, long n)   { return sc3(SYS_connect, fd, (long)a, n); }
static long k_sendto(long fd, const void *b, u64 n, long fl, const void *a, long al)
                                                        { return sc6(SYS_sendto, fd, (long)b, (long)n, fl, (long)a, al); }
static long k_recvfrom(long fd, void *b, u64 n, long fl, void *a, void *al)
                                                        { return sc6(SYS_recvfrom, fd, (long)b, (long)n, fl, (long)a, (long)al); }
static long k_setsockopt(long fd, long lv, long nm, const void *v, long n)
                                                        { return sc5(SYS_setsockopt, fd, lv, nm, (long)v, n); }
static long k_ioctl(long fd, long req, void *arg)       { return sc3(SYS_ioctl, fd, req, (long)arg); }
static long k_shutdown(long fd, long how)               { return sc2(SYS_shutdown, fd, how); }
struct k_timespec { long tv_sec; long tv_nsec; };
static long k_clock_gettime(long clk, void *ts)         { return sc2(SYS_clock_gettime, clk, (long)ts); }
static long k_nanosleep(const void *req, void *rem)     { return sc2(SYS_nanosleep, (long)req, (long)rem); }
struct k_statfs {
    u64 f_type, f_bsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    u64 f_fsid[2];
    u64 f_namelen, f_frsize, f_flags, f_spare[4];
};
static long k_statfs(const char *p, void *b)            { return sc2(SYS_statfs, (long)p, (long)b); }
static long k_bind(long fd, const void *a, long n)      { return sc3(SYS_bind, fd, (long)a, n); }
static long k_listen(long fd, long n)                   { return sc2(SYS_listen, fd, n); }
static long k_accept(long fd, void *a, void *al)        { return sc3(SYS_accept, fd, (long)a, (long)al); }
static long k_clock_settime(long clk, const void *ts)   { return sc2(SYS_clock_settime, clk, (long)ts); }

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
#define NCMD 19
static const char *cmd_names[NCMD] = {
    "toyls", "toycd", "toypwd", "toycat", "toynano",
    "toyfetch", "toydns", "toyip", "toyping",
    "toymount", "toydf", "toync", "toyntp", "toyserve",
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
 * toynano - full screen edito
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
      "  toyfetch <host> [path] fetch a web page (HTTP)\r\n"
      "  toydns <host>  resolve a hostname to IPv4\r\n"
      "  toyip          show interface IPv4 addresses\r\n"
      "  toyping <host> [count] ICMP ping (TCP/80 fallback)\r\n"
      "  toymount <dev> <dir> [fstype] mount a filesystem\r\n"
      "  toydf [path]   show filesystem space\r\n"
      "  toync <host> <port> TCP client (Ctrl-D quits)\r\n"
      "  toyntp [server] sync clock via NTP\r\n"
      "  toyserve [port] [dir] [n] serve files over HTTP\r\n"
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

/* ---- minimal networking (AF_INET, DNS + HTTP over raw syscalls) ---- */
static u16 n_htons(u16 v) { return (u16)(((v >> 8) & 0xff) | ((v & 0xff) << 8)); }
static u32 n_htonl(u32 v) {
    return ((v >> 24) & 0xff) | ((v >> 8) & 0xff00) |
           ((v << 8) & 0xff0000) | ((v << 24) & 0xff000000);
}

/* dotted decimal -> network-order u32. returns 1 ok, 0 bad */
static int parse_ip(const char *s, u32 *out) {
    u32 parts[4]; int pi = 0; u32 cur = 0; int digits = 0;
    for (;;) {
        char c = *s;
        if (c >= '0' && c <= '9') { cur = cur * 10 + (u32)(c - '0'); digits++; if (cur > 255) return 0; s++; continue; }
        if ((c == '.' || c == 0) && digits > 0) {
            if (pi >= 4) return 0;
            parts[pi++] = cur; cur = 0; digits = 0;
            if (c == 0) break;
            s++; continue;
        }
        return 0;
    }
    if (pi != 4) return 0;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 1;
}

static void print_ip(u32 ip) {
    putu((ip >> 24) & 0xff); putc('.');
    putu((ip >> 16) & 0xff); putc('.');
    putu((ip >> 8) & 0xff); putc('.');
    putu(ip & 0xff);
}

static void set_timeout(long fd) {
    struct k_timeval tv;
    tv.tv_sec = 4; tv.tv_usec = 0;
    k_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    k_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

/* QEMU user-mode networking serves DNS here */
#define TOY_DNS_SERVER_IP 0x0a000203u /* 10.0.2.3 */

static u8 dnsbuf[512];

/* encode "a.b.c" as DNS labels. returns length, 0 on overflow */
static int dns_encode(const char *host, u8 *out, int cap) {
    int len = 0;
    for (;;) {
        int lab = 0;
        while (host[lab] && host[lab] != '.' && lab < 63) lab++;
        if (len + 1 + lab + 1 > cap) return 0;
        out[len++] = (u8)lab;
        for (int i = 0; i < lab; i++) out[len++] = (u8)host[i];
        host += lab;
        if (*host == '.') { host++; continue; }
        break;
    }
    out[len++] = 0;
    return len;
}

/* skip a possibly-compressed name. returns offset after it, -1 on error */
static int dns_skip(const u8 *p, int off, int end) {
    while (off < end) {
        u8 c = p[off];
        if (c == 0) return off + 1;
        if ((c & 0xc0) == 0xc0) return off + 2;
        off += 1 + c;
    }
    return -1;
}

static u16 rd16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }

/* resolve A record. returns 0 ok (ip set, network order), -1 fail */
static int dns_resolve(const char *host, u32 *out_ip) {
    static u16 qid = 0x1234;
    long fd; int qlen, rlen, off, i;
    struct sockaddr_in sa;
    u8 *q = dnsbuf;

    if (parse_ip(host, out_ip)) return 0;

    q[0] = (u8)(qid >> 8); q[1] = (u8)qid; qid++;
    q[2] = 0x01; q[3] = 0x00; /* RD */
    q[4] = 0; q[5] = 1;       /* QDCOUNT */
    q[6] = 0; q[7] = 0; q[8] = 0; q[9] = 0; q[10] = 0; q[11] = 0;
    qlen = dns_encode(host, q + 12, (int)sizeof dnsbuf - 12 - 4);
    if (!qlen) return -1;
    qlen += 12;
    q[qlen++] = 0; q[qlen++] = 1; /* QTYPE A */
    q[qlen++] = 0; q[qlen++] = 1; /* QCLASS IN */

    fd = k_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;
    set_timeout(fd);
    sa.sin_family = AF_INET;
    sa.sin_port = n_htons(53);
    sa.sin_addr = n_htonl(TOY_DNS_SERVER_IP);
    for (i = 0; i < 8; i++) sa.sin_zero[i] = 0;
    if (k_sendto(fd, q, (u64)qlen, 0, &sa, 16) != qlen) { k_close(fd); return -1; }
    rlen = (int)k_recvfrom(fd, q, sizeof dnsbuf, 0, 0, 0);
    k_close(fd);
    if (rlen < 12) return -1;
    if ((q[2] & 0x80) == 0) return -1; /* not a response */
    off = dns_skip(q, 12, rlen);
    if (off < 0 || off + 4 > rlen) return -1;
    off += 4; /* question */
    for (i = 0; i < 16; i++) { /* up to 16 answers */
        int rdlen, type;
        off = dns_skip(q, off, rlen);
        if (off < 0 || off + 10 > rlen) return -1;
        type = rd16(q + off); rdlen = rd16(q + off + 8);
        if (type == 1 && rd16(q + off + 2) == 1 && rdlen == 4 && off + 10 + 4 <= rlen) {
            *out_ip = ((u32)q[off+10] << 24) | ((u32)q[off+11] << 16) |
                      ((u32)q[off+12] << 8) | (u32)q[off+13];
            return 0;
        }
        off += 10 + rdlen;
    }
    return -1;
}

static u8 respbuf[32768];
static char reqbuf[1024];

/* GET http://host[:80]/path (path may be "") and print the body */
static void toy_fetch(const char *host, const char *path) {
    u32 ip; long fd, n; u64 total = 0; int rl, bi, hs;
    struct sockaddr_in sa;
    const char *p1a = "GET ", *p1b = " HTTP/1.0\r\nHost: ", *p1c = "\r\nConnection: close\r\n\r\n";

    if (dns_resolve(host, &ip) != 0) {
        puts("toyfetch: cannot resolve '"); puts(host); puts("'\r\n");
        return;
    }
    fd = k_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { puts("toyfetch: no network (socket failed)\r\n"); return; }
    set_timeout(fd);
    sa.sin_family = AF_INET;
    sa.sin_port = n_htons(80);
    sa.sin_addr = n_htonl(ip);
    for (rl = 0; rl < 8; rl++) sa.sin_zero[rl] = 0;
    if (k_connect(fd, &sa, 16) != 0) {
        puts("toyfetch: connect to "); print_ip(ip); puts(" failed\r\n");
        k_close(fd); return;
    }
    /* build request */
    rl = 0;
    for (bi = 0; p1a[bi] && rl < (int)sizeof reqbuf - 1; bi++) reqbuf[rl++] = p1a[bi];
    if (!*path || *path != '/') { if (rl < (int)sizeof reqbuf - 1) reqbuf[rl++] = '/'; }
    for (bi = 0; path[bi] && rl < (int)sizeof reqbuf - 1; bi++) reqbuf[rl++] = path[bi];
    for (bi = 0; p1b[bi] && rl < (int)sizeof reqbuf - 1; bi++) reqbuf[rl++] = p1b[bi];
    for (bi = 0; host[bi] && rl < (int)sizeof reqbuf - 1; bi++) reqbuf[rl++] = host[bi];
    for (bi = 0; p1c[bi] && rl < (int)sizeof reqbuf - 1; bi++) reqbuf[rl++] = p1c[bi];
    reqbuf[rl] = 0;
    if (k_write((int)fd, reqbuf, (u64)rl) != rl) {
        puts("toyfetch: send failed\r\n"); k_close(fd); return;
    }
    k_shutdown(fd, 1); /* SHUT_WR: we are done sending */
    total = 0;
    for (;;) {
        n = k_read(fd, respbuf + total, sizeof respbuf - total - 1);
        if (n <= 0) break;
        total += (u64)n;
        if (total >= sizeof respbuf - 1) break;
    }
    k_close(fd);
    if (!total) { puts("toyfetch: empty reply\r\n"); return; }
    respbuf[total] = 0;
    /* strip HTTP headers */
    hs = -1;
    for (bi = 0; bi + 3 < (int)total; bi++) {
        if (respbuf[bi] == '\r' && respbuf[bi+1] == '\n' &&
            respbuf[bi+2] == '\r' && respbuf[bi+3] == '\n') { hs = bi + 4; break; }
    }
    if (hs < 0) hs = 0;
    k_write(1, respbuf + hs, total - (u64)hs);
    puts("\r\n[toyfetch: "); putu(total - (u64)hs); puts(" body bytes]\r\n");
}

static void toy_ip(void) {
    static const char *ifs[] = { "eth0", "lo" };
    long fd = k_socket(AF_INET, SOCK_DGRAM, 0);
    int i, found = 0;
    if (fd < 0) { puts("toyip: no network support in this kernel\r\n"); return; }
    for (i = 0; i < 2; i++) {
        struct ifreq_addr rq;
        int k;
        for (k = 0; k < 16; k++) rq.ifr_name[k] = 0;
        for (k = 0; ifs[i][k] && k < 15; k++) rq.ifr_name[k] = ifs[i][k];
        if (k_ioctl((int)fd, SIOCGIFADDR, &rq) == 0) {
            u32 ip = ((u32)(u8)rq.sa_data[2] << 24) | ((u32)(u8)rq.sa_data[3] << 16) |
                     ((u32)(u8)rq.sa_data[4] << 8) | (u32)(u8)rq.sa_data[5];
            puts(ifs[i]); puts(": "); print_ip(ip); puts("\r\n");
            found = 1;
        }
    }
    k_close(fd);
    if (!found) puts("toyip: no IPv4 address (is QEMU net attached? try: ip=dhcp)\r\n");
}

static void toy_dns(const char *host) {
    u32 ip;
    if (dns_resolve(host, &ip) != 0) {
        puts("toydns: cannot resolve '"); puts(host); puts("'\r\n");
        return;
    }
    puts(host); puts(" -> "); print_ip(ip); puts("\r\n");
}

/* internet checksum (RFC 1071) */
static u16 icmp_cksum(const u8 *p, int n) {
    u32 sum = 0;
    while (n > 1) { sum += ((u32)p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) sum += (u32)p[0] << 8;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += sum >> 16;
    return (u16)~sum;
}

static long now_ms(void) {
    struct k_timespec ts;
    if (k_clock_gettime(1, &ts) != 0) return -1;
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int parse_uint(const char *s, int *out) {
    int v = 0, d = 0;
    if (!*s) return 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); d = 1; s++; }
    if (*s || !d) return 0;
    *out = v; return 1;
}

/* ICMP echo ping. QEMU user-net drops ICMP, so with zero replies we fall
 * back to a TCP/80 open check to tell "filtered" from "down". */
static void toy_ping(const char *host, int count) {
    u32 ip; long fd, i;
    long sent = 0, got = 0, tmin = -1, tmax = 0, tsum = 0;
    static u8 pkt[128];
    if (count < 1) count = 1;
    if (count > 20) count = 20;
    if (dns_resolve(host, &ip) != 0) {
        puts("toyping: cannot resolve '"); puts(host); puts("'\r\n");
        return;
    }
    puts("PING "); puts(host); puts(" ("); print_ip(ip); puts(")\r\n");
    fd = k_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) { puts("toyping: raw socket failed\r\n"); return; }
    {
        struct k_timeval tv;
        tv.tv_sec = 1; tv.tv_usec = 0;
        k_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    for (i = 0; i < count; i++) {
        int k, n, hl; long t0, t1;
        struct sockaddr_in sa;
        pkt[0] = 8; pkt[1] = 0; pkt[2] = 0; pkt[3] = 0; /* echo request */
        pkt[4] = 0x54; pkt[5] = 0x59;                   /* id 'TY' */
        pkt[6] = (u8)(i >> 8); pkt[7] = (u8)i;           /* seq */
        for (k = 0; k < 32; k++) pkt[8 + k] = (u8)(k + i);
        {
            u16 c = icmp_cksum(pkt, 40);
            pkt[2] = (u8)(c >> 8); pkt[3] = (u8)c;
        }
        sa.sin_family = AF_INET; sa.sin_port = 0; sa.sin_addr = n_htonl(ip);
        for (k = 0; k < 8; k++) sa.sin_zero[k] = 0;
        t0 = now_ms();
        if (k_sendto(fd, pkt, 40, 0, &sa, 16) != 40) { puts("send failed\r\n"); continue; }
        sent++;
        {
            long deadline = t0 + 1000, ms = 0;
            int ok = 0;
            /* keep receiving until the deadline: raw sockets also get a
             * copy of our own outgoing request, which must be skipped */
            while (now_ms() < deadline) {
                n = (int)k_recvfrom(fd, pkt, sizeof pkt, 0, 0, 0);
                t1 = now_ms();
                hl = (n > 0) ? (pkt[0] & 0x0f) * 4 : 0;
                if (n >= hl + 8 && hl >= 20 && pkt[hl] == 0 && pkt[hl + 1] == 0 &&
                    pkt[hl + 4] == 0x54 && pkt[hl + 5] == 0x59 &&
                    pkt[hl + 6] == (u8)(i >> 8) && pkt[hl + 7] == (u8)i) {
                    ms = t1 - t0;
                    ok = 1;
                    break;
                }
            }
            if (ok) {
                got++; tsum += ms;
                if (tmin < 0 || ms < tmin) tmin = ms;
                if (ms > tmax) tmax = ms;
                puts("reply seq="); putu((u64)i);
                puts(" time="); putu((u64)ms); puts(" ms\r\n");
            } else {
                puts("timeout seq="); putu((u64)i); puts("\r\n");
            }
        }
        if (i + 1 < count) {
            struct k_timespec rq;
            rq.tv_sec = 1; rq.tv_nsec = 0;
            k_nanosleep(&rq, 0);
        }
    }
    k_close(fd);
    puts("--- "); puts(host); puts(" ping statistics ---\r\n");
    putu((u64)sent); puts(" sent, "); putu((u64)got); puts(" received, ");
    putu(sent ? (u64)(100 * (sent - got) / sent) : 100); puts("% loss\r\n");
    if (got) {
        puts("rtt min/avg/max = "); putu((u64)tmin); putc('/');
        putu((u64)(tsum / (u64)got)); putc('/'); putu((u64)tmax); puts(" ms\r\n");
        return;
    }
    puts("no ICMP replies (QEMU user-net blocks ICMP); trying TCP/80 ...\r\n");
    {
        long tf = k_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (tf >= 0) {
            struct sockaddr_in sa2; int k; long t0 = now_ms(), t1;
            set_timeout(tf);
            sa2.sin_family = AF_INET; sa2.sin_port = n_htons(80); sa2.sin_addr = n_htonl(ip);
            for (k = 0; k < 8; k++) sa2.sin_zero[k] = 0;
            if (k_connect(tf, &sa2, 16) == 0) puts("TCP/80 open");
            else puts("TCP/80 closed/filtered");
            t1 = now_ms();
            puts(" in "); putu((u64)(t1 - t0)); puts(" ms\r\n");
            k_close(tf);
        }
    }
}

/* ---- command dispatch ---- */
static void toy_nc(const char *host, int port) {
    u32 ip; long fd, n;
    static char line[512];
    struct sockaddr_in sa;
    int k;
    struct k_timeval tv;
    if (dns_resolve(host, &ip) != 0) {
        puts("toync: cannot resolve '"); puts(host); puts("'\r\n");
        return;
    }
    fd = k_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { puts("toync: no network (socket failed)\r\n"); return; }
    tv.tv_sec = 1; tv.tv_usec = 0;
    k_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    set_timeout(fd);
    sa.sin_family = AF_INET; sa.sin_port = n_htons((u16)port); sa.sin_addr = n_htonl(ip);
    for (k = 0; k < 8; k++) sa.sin_zero[k] = 0;
    if (k_connect(fd, &sa, 16) != 0) {
        puts("toync: connect to "); print_ip(ip);
        puts(" port "); putu((u64)port); puts(" failed\r\n");
        k_close(fd); return;
    }
    puts("connected. empty Ctrl-D quits; blank line sends CRLF.\r\n");
    for (;;) {
        static u8 chunk[1024];
        n = readline("nc> ", line, sizeof line);
        if (n < 0) break;
        line[n] = 0;
        k_write((int)fd, line, (u64)n);
        k_write((int)fd, "\n", 1);
        for (;;) {
            n = k_read(fd, chunk, sizeof chunk);
            if (n <= 0) break;
            k_write(1, chunk, (u64)n);
        }
    }
    k_close(fd);
    puts("\r\ndisconnected\r\n");
}

/* unix seconds -> "YYYY-MM-DD HH:MM:SS" (UTC) */
static void fmt_date(u64 secs, char *out) {
    long days = (long)(secs / 86400);
    long rem = (long)(secs % 86400);
    long z = days + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp + (mp < 10 ? 3 : -9);
    unsigned hh, mm, ss;
    y += (m <= 2);
    hh = (unsigned)(rem / 3600); mm = (unsigned)((rem % 3600) / 60); ss = (unsigned)(rem % 60);
    out[0] = (char)('0' + (y / 1000) % 10); out[1] = (char)('0' + (y / 100) % 10);
    out[2] = (char)('0' + (y / 10) % 10); out[3] = (char)('0' + y % 10);
    out[4] = '-'; out[5] = (char)('0' + m / 10); out[6] = (char)('0' + m % 10);
    out[7] = '-'; out[8] = (char)('0' + d / 10); out[9] = (char)('0' + d % 10);
    out[10] = ' '; out[11] = (char)('0' + hh / 10); out[12] = (char)('0' + hh % 10);
    out[13] = ':'; out[14] = (char)('0' + mm / 10); out[15] = (char)('0' + mm % 10);
    out[16] = ':'; out[17] = (char)('0' + ss / 10); out[18] = (char)('0' + ss % 10);
    out[19] = 0;
}

static void toy_ntp(const char *server) {
    u32 ip; long fd, n, i;
    static u8 pkt[48];
    struct sockaddr_in sa;
    int k;
    u32 tx; u64 usecs;
    static char datebuf[24];
    struct k_timespec ts;
    if (dns_resolve(server, &ip) != 0) {
        puts("toyntp: cannot resolve '"); puts(server); puts("'\r\n");
        return;
    }
    fd = k_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) { puts("toyntp: no network (socket failed)\r\n"); return; }
    set_timeout(fd);
    for (i = 0; i < 48; i++) pkt[i] = 0;
    pkt[0] = 0x23; /* LI=0 VN=4 Mode=3 (client) */
    sa.sin_family = AF_INET; sa.sin_port = n_htons(123); sa.sin_addr = n_htonl(ip);
    for (k = 0; k < 8; k++) sa.sin_zero[k] = 0;
    if (k_sendto(fd, pkt, 48, 0, &sa, 16) != 48) {
        puts("toyntp: send failed\r\n"); k_close(fd); return;
    }
    n = k_recvfrom(fd, pkt, sizeof pkt, 0, 0, 0);
    k_close(fd);
    if (n < 48) { puts("toyntp: no reply\r\n"); return; }
    tx = ((u32)pkt[40] << 24) | ((u32)pkt[41] << 16) | ((u32)pkt[42] << 8) | (u32)pkt[43];
    if (tx < 2208988800u) { puts("toyntp: bad timestamp\r\n"); return; }
    usecs = (u64)(tx - 2208988800u);
    ts.tv_sec = (long)usecs; ts.tv_nsec = 0;
    if (k_clock_settime(0, &ts) != 0) puts("toyntp: clock not set (continuing)\r\n");
    fmt_date(usecs, datebuf);
    puts(datebuf); puts(" UTC\r\n");
}

static void toy_serve(int port, const char *dir, int maxreq) {
    long sfd, cfd, n;
    struct sockaddr_in sa;
    int k, served = 0;
    static u8 req[2048];
    sfd = k_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sfd < 0) { puts("toyserve: no network (socket failed)\r\n"); return; }
    sa.sin_family = AF_INET; sa.sin_port = n_htons((u16)port); sa.sin_addr = 0;
    for (k = 0; k < 8; k++) sa.sin_zero[k] = 0;
    if (k_bind(sfd, &sa, 16) != 0 || k_listen(sfd, 4) != 0) {
        puts("toyserve: cannot bind port "); putu((u64)port); puts("\r\n");
        k_close(sfd); return;
    }
    puts("serving "); puts(dir); puts(" on port "); putu((u64)port);
    puts(" (host: http://localhost:8080/ , Ctrl-C hits are just requests)\r\n");
    while (served < maxreq) {
        cfd = k_accept(sfd, 0, 0);
        if (cfd < 0) continue;
        served++;
        n = k_read(cfd, req, sizeof req - 1);
        if (n > 0) {
            char path[256];
            int pi = 0, qi = 0;
            req[n] = 0;
            /* expect "GET /path ..." */
            if (n > 4 && req[0] == 'G' && req[1] == 'E' && req[2] == 'T' && req[3] == ' ') {
                int j = 4;
                while (req[j] && req[j] != ' ' && req[j] != '\r' && req[j] != '\n' && req[j] != '?'
                       && pi < (int)sizeof path - 1) {
                    path[pi++] = (char)req[j++];
                }
                path[pi] = 0;
                /* serve dir + path (index.html for directories) */
                {
                    char full[320];
                    int fi = 0, bad = 0;
                    for (qi = 0; dir[qi] && fi < (int)sizeof full - 1; qi++) full[fi++] = dir[qi];
                    for (qi = 0; qi < pi && fi < (int)sizeof full - 1; qi++) full[fi++] = path[qi];
                    if (fi > 0 && full[fi - 1] == '/') {
                        static const char *idx = "index.html";
                        for (qi = 0; idx[qi] && fi < (int)sizeof full - 1; qi++) full[fi++] = idx[qi];
                    }
                    full[fi] = 0;
                    for (qi = 0; full[qi]; qi++) {
                        if (full[qi] == '.' && full[qi + 1] == '.') bad = 1;
                    }
                    if (!bad) {
                        long ffd = k_open(full, O_RDONLY);
                        if (ffd >= 0) {
                            static const char *hd = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n";
                            k_write((int)cfd, hd, slen(hd));
                            for (;;) {
                                long r = k_read((int)ffd, respbuf, sizeof respbuf);
                                if (r <= 0) break;
                                k_write((int)cfd, respbuf, (u64)r);
                            }
                            k_close(ffd);
                            puts("200 "); puts(full); puts("\r\n");
                        } else {
                            static const char *nf = "HTTP/1.0 404 Not Found\r\nConnection: close\r\n\r\nnot found\n";
                            k_write((int)cfd, nf, slen(nf));
                            puts("404 "); puts(full); puts("\r\n");
                        }
                    }
                }
            }
        }
        k_close((int)cfd);
    }
    k_close((int)sfd);
    puts("toyserve: done\r\n");
}
static int toy_df(const char *path) {
    struct k_statfs st;
    u64 total, freeb;
    if (k_statfs(path, &st) != 0) {
        puts("toydf: cannot statfs '"); puts(path); puts("'\r\n");
        return -1;
    }
    total = st.f_blocks * st.f_bsize / 1048576;
    freeb = st.f_bfree * st.f_bsize / 1048576;
    puts(path); puts(": ");
    putu(total); puts("M total, ");
    putu(freeb); puts("M free\r\n");
    return 0;
}

static int toy_mount(const char *dev, const char *dir, const char *fstype) {
    k_mkdir(dir, 0755);
    if (k_mount(dev, dir, fstype, 0, 0) != 0) {
        puts("toymount: cannot mount '"); puts(dev);
        puts("' on '"); puts(dir); puts("'\r\n");
        return -1;
    }
    puts("mounted "); puts(dev); puts(" on "); puts(dir); puts("\r\n");
    return 0;
}
/* ---- external program execution (process model) ---- */
#define SYS_fork    57
#define SYS_execve  59
#define SYS_wait4   61

static int path_exists(const char *p) {
    int fd = (int)sc2(SYS_open, (long)p, O_RDONLY);
    if (fd >= 0) { sc1(SYS_close, fd); return 1; }
    return 0;
}

/* fork + exec /bin/<cmd>; returns 1 if it ran, 0 if not found */
static int try_exec(const char *cmd) {
    char path[64];
    int i = 0;
    const char *a = "/bin/";
    while (a[i]) { path[i] = a[i]; i++; }
    int j = 0;
    while (cmd[j] && i < 62) path[i++] = cmd[j++];
    path[i] = 0;
    if (!path_exists(path)) return 0;
    long pid = sc0(SYS_fork);
    if (pid == 0) {
        char *argv[2]; argv[0] = path; argv[1] = 0;
        sc3(SYS_execve, (long)path, (long)argv, 0);
        sc1(SYS_exit_group, 127);
    } else if (pid > 0) {
        int st = 0;
        sc4(SYS_wait4, pid, (long)&st, 0, 0);
    }
    return 1;
}

static int run_line(char *line) {
    char *rest = line;
    while (*rest && *rest != ' ' && *rest != '\t') rest++;
    if (*rest) *rest++ = 0;
    while (*rest == ' ' || *rest == '\t') rest++;
    char *cmd = line;
    if (!*cmd) return 0;

    if (scmp(cmd, "echo") == 0) { puts(rest); puts("\r\n"); return 0; }
    if (scmp(cmd, "clear") == 0) { clrscr(); console_setup(); return 0; }
    if (scmp(cmd, "help") == 0) { show_help(); return 0; }
    if (scmp(cmd, "toypwd") == 0) { toy_pwd(); return 0; }
    if (scmp(cmd, "toyip") == 0) { toy_ip(); return 0; }
    if (scmp(cmd, "toydns") == 0 || scmp(cmd, "toyfetch") == 0) {
        char *a1 = rest, *a2 = 0, *e = rest;
        while (*e && *e != ' ' && *e != '\t') e++;
        if (*e) { *e = 0; e++; while (*e == ' ' || *e == '\t') e++; if (*e) a2 = e; }
        if (!*a1 || (scmp(cmd, "toydns") == 0 && a2)) {
            puts(scmp(cmd, "toydns") == 0 ? "usage: toydns <host>\r\n"
                                          : "usage: toyfetch <host> [path]\r\n");
            return 0;
        }
        if (scmp(cmd, "toydns") == 0) { toy_dns(a1); return 0; }
        /* toyfetch: allow http://host/path or host + separate path */
        {
            char *host = a1, *path = "/";
            if (slen(host) > 7 && host[0]=='h' && host[1]=='t' && host[2]=='t' &&
                host[3]=='p' && host[4]==':' && host[5]=='/' && host[6]=='/') host += 7;
            {
                char *s = host;
                while (*s && *s != '/') s++;
                if (*s) { *s = 0; path = s + 1; }
            }
            if (a2) path = a2;
            toy_fetch(host, path);
        }
        return 0;
    }
    if (scmp(cmd, "toyping") == 0) {
        char *a1 = rest, *a2 = 0, *e = rest;
        int count = 4, c2;
        while (*e && *e != ' ' && *e != '\t') e++;
        if (*e) { *e = 0; e++; while (*e == ' ' || *e == '\t') e++; if (*e) a2 = e; }
        if (!*a1) { puts("usage: toyping <host> [count]\r\n"); return 0; }
        if (a2) {
            if (!parse_uint(a2, &c2) || c2 < 1 || c2 > 20) {
                puts("usage: toyping <host> [count 1..20]\r\n"); return 0;
            }
            count = c2;
        }
        toy_ping(a1, count);
        return 0;
    }
    if (scmp(cmd, "toymount") == 0) {
        char *a1 = rest, *a2 = 0, *a3 = 0, *e = rest;
        while (*e && *e != ' ' && *e != '\t') e++;
        if (*e) { *e = 0; e++; while (*e == ' ' || *e == '\t') e++; if (*e) a2 = e; }
        if (a2) {
            e = a2;
            while (*e && *e != ' ' && *e != '\t') e++;
            if (*e) { *e = 0; e++; while (*e == ' ' || *e == '\t') e++; if (*e) a3 = e; }
        }
        if (!*a1 || !a2 || !*a2) { puts("usage: toymount <dev> <dir> [fstype]\r\n"); return 0; }
        toy_mount(a1, a2, (a3 && *a3) ? a3 : "ext4");
        return 0;
    }
    if (scmp(cmd, "toydf") == 0) {
        char *a1 = rest, *e = rest;
        while (*e && *e != ' ' && *e != '\t') e++;
        if (*e) *e = 0;
        toy_df(*a1 ? a1 : "/");
        return 0;
    }
    if (scmp(cmd, "toync") == 0) {
        char *a1 = rest, *a2 = 0, *e = rest;
        int port;
        while (*e && *e != ' ' && *e != '\t') e++;
        if (*e) { *e = 0; e++; while (*e == ' ' || *e == '\t') e++; if (*e) a2 = e; }
        if (!*a1 || !a2 || !parse_uint(a2, &port) || port < 1 || port > 65535) {
            puts("usage: toync <host> <port>\r\n"); return 0;
        }
        toy_nc(a1, port);
        return 0;
    }
    if (scmp(cmd, "toyntp") == 0) {
        char *a1 = rest, *e = rest;
        while (*e && *e != ' ' && *e != '\t') e++;
        if (*e) *e = 0;
        toy_ntp(*a1 ? a1 : "pool.ntp.org");
        return 0;
    }
    if (scmp(cmd, "toyserve") == 0) {
        char *a[3] = { 0, 0, 0 };
        int na = 0, port = 80, maxreq = 8, tmp;
        char *e = rest;
        const char *dir = "/toy";
        while (na < 3) {
            while (*e == ' ' || *e == '\t') e++;
            if (!*e) break;
            a[na++] = e;
            while (*e && *e != ' ' && *e != '\t') e++;
            if (*e) { *e = 0; e++; }
        }
        if (na > 0) {
            if (!parse_uint(a[0], &tmp) || tmp < 1 || tmp > 65535) {
                puts("usage: toyserve [port] [dir] [count]\r\n"); return 0;
            }
            port = tmp;
        }
        if (na > 1) dir = a[1];
        if (na > 2) {
            if (!parse_uint(a[2], &tmp) || tmp < 1 || tmp > 64) {
                puts("usage: toyserve [port] [dir] [count 1..64]\r\n"); return 0;
            }
            maxreq = tmp;
        }
        toy_serve(port, dir, maxreq);
        return 0;
    }
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

    /* not a builtin: try to exec /bin/<cmd> as a separate process */
    if (try_exec(cmd)) return 0;

    puts("toyium: "); puts(cmd); puts(": command not found\r\n");
    puts("available: toyls, toycd, toypwd, toycat, toynano, toyfetch, toydns, toyip, toyping, toymount, toydf, toync, toyntp, toyserve, echo, clear, help, poweroff, reboot\r\n");
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

    /* networking unit tests (offline: pure functions only) */
    puts("== net tests ==\r\n");
    {
        u32 ip = 0;
        report(parse_ip("10.0.2.3", &ip) && ip == 0x0a000203u, "parse 10.0.2.3");
        report(!parse_ip("999.1.1.1", &ip), "reject bad octet");
        report(!parse_ip("1.2.3", &ip), "reject short addr");
        report(n_htons(80) == 0x5000u, "htons(80)");
        report(n_htonl(0x01020304u) == 0x04030201u, "htonl");
        {
            static const u8 t[8] = { 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
            report(icmp_cksum(t, 8) == 0xf7ffu, "icmp checksum");
        }
        report(toy_df("/") == 0, "statfs /");
        {
            static char d[24];
            fmt_date(0, d);
            report(scmp(d, "1970-01-01 00:00:00") == 0, "date epoch");
            fmt_date(86400u, d);
            report(scmp(d, "1970-01-02 00:00:00") == 0, "date rollover");
        }
    }

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

    /* persistent disk (QEMU virtio drive), if attached */
    k_mkdir("/disk", 0755);
    k_mount("/dev/vda", "/disk", "ext4", 0, 0);

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
    puts("  (commands: toyls, toycd, toypwd, toycat, toynano, toyfetch, toydns, toyip, toyping, toymount, toydf, toync, toyntp, toyserve, echo, clear, help, poweroff, reboot)\r\n\r\n");
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
