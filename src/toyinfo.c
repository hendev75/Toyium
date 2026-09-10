/* SPDX-License-Identifier: MIT */
/* toyinfo - "About Toyium" window (toywm client) */
#include "toy.h"

struct utsname { char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65]; };

static int wmfd = -1;
static void msleep(long ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; sc2(SYS_nanosleep, (long)&t, 0); }
static void send(struct win_msg *m) { write_full(wmfd, m, sizeof *m); }
static void txt(int x, int y, u32 c, const char *s) {
    struct win_msg m; m.type = WIN_TEXT; m.x = x; m.y = y; m.w = m.h = 0; m.color = c;
    int i = 0; for (; s[i] && i < WIN_MSG_LEN - 1; i++) m.data[i] = s[i];
    m.data[i] = 0; m.len = i; send(&m);
}
static void rect(int x, int y, int w, int h, u32 c) {
    struct win_msg m; m.type = WIN_RECT; m.x = x; m.y = y; m.w = w; m.h = h; m.color = c; send(&m);
}

/* read a whole small file into buf, return length */
static long slurp(const char *path, char *buf, long cap) {
    int fd = (int)t_open(path, O_RDONLY);
    if (fd < 0) return -1;
    long n = t_read(fd, buf, cap - 1);
    t_close(fd);
    if (n < 0) n = 0;
    buf[n] = 0;
    return n;
}

int _start(void) {
    for (int t = 0; t < 50; t++) {
        wmfd = (int)t_socket(AF_UNIX, SOCK_STREAM, 0);
        if (wmfd >= 0) {
            struct sockaddr_un sa; for (u64 i = 0; i < sizeof sa; i++) ((u8 *)&sa)[i] = 0;
            sa.sun_family = AF_UNIX; const char *p = WM_SOCK;
            for (int i = 0; p[i] && i < 107; i++) sa.sun_path[i] = p[i];
            if (t_connect(wmfd, &sa, sizeof sa) == 0) break;
            t_close(wmfd); wmfd = -1;
        }
        msleep(100);
    }
    if (wmfd < 0) { puts_("toyinfo: no toywm\n"); return 1; }

    struct win_msg m; m.type = WIN_CREATE; m.x = m.y = 0; m.w = 360; m.h = 220; m.color = 0; m.len = 0;
    const char *t = "About Toyium"; int i = 0; for (; t[i]; i++) m.data[i] = t[i]; m.data[i] = 0; m.len = i;
    send(&m);

    char buf[1024];
    for (;;) {
        m.type = WIN_CLEAR; m.color = 0x101828; send(&m);
        rect(0, 0, 360, 40, 0x1E3A5F);
        txt(12, 8, 0x9CD0FF, "Toyium OS");
        txt(12, 22, 0x8898B0, "made by xex & ayham");

        struct utsname u;
        if (sc1(SYS_uname, (long)&u) == 0)
            txt(12, 54, 0xE0F0FF, u.release);

        char line[128];
        /* kernel release string + machine */
        txt(12, 70, 0xB0C0D8, u.sysname);
        txt(90, 70, 0xB0C0D8, u.machine);

        /* uptime */
        long n = slurp("/proc/uptime", buf, sizeof buf);
        if (n > 0) {
            int s = 0; for (int k = 0; buf[k] >= '0' && buf[k] <= '9'; k++) s = s * 10 + (buf[k] - '0');
            char *p = line; const char *a = "uptime: "; while (*a) *p++ = *a++;
            char nb[16]; int c = 0, v = s;
            if (!v) nb[c++] = '0'; while (v) { nb[c++] = '0' + v % 10; v /= 10; }
            while (c) *p++ = nb[--c];
            const char *b = " s"; while (*b) *p++ = *b++;
            *p = 0;
            txt(12, 96, 0xC0D0E8, line);
        }

        /* MemTotal / MemFree */
        n = slurp("/proc/meminfo", buf, sizeof buf);
        if (n > 0) {
            /* find "MemTotal:" then first digits */
            int idx = -1;
            for (int k = 0; k + 8 < n; k++) if (buf[k]=='M'&&buf[k+1]=='e'&&buf[k+2]=='m'&&buf[k+3]=='T'&&buf[k+4]=='o'&&buf[k+5]=='t'&&buf[k+6]=='a'&&buf[k+7]=='l') { idx = k; break; }
            if (idx >= 0) {
                int k = idx; while (k < n && (buf[k] < '0' || buf[k] > '9')) k++;
                int mb = 0; while (k < n && buf[k] >= '0' && buf[k] <= '9') { mb = mb * 10 + (buf[k] - '0'); k++; }
                mb /= 1024;
                char *p = line; const char *a = "memory: "; while (*a) *p++ = *a++;
                char nb[16]; int c = 0, v = mb;
                if (!v) nb[c++] = '0'; while (v) { nb[c++] = '0' + v % 10; v /= 10; }
                while (c) *p++ = nb[--c];
                const char *b = " MB"; while (*b) *p++ = *b++;
                *p = 0;
                txt(12, 120, 0xC0D0E8, line);
            }
        }

        txt(12, 150, 0x90A0C0, "toyfs, TCP/IP, a window manager");
        txt(12, 166, 0x90A0C0, "and this window - all from scratch.");
        txt(12, 192, 0xE0C060, "Ctrl-C in the WM quits to the shell.");

        m.type = WIN_FLUSH; send(&m);

        struct pollfd { int fd; short events; short revents; } pf;
        pf.fd = wmfd; pf.events = POLLIN; pf.revents = 0;
        long pr = t_poll(&pf, 1, 1000);
        if (pr > 0 && (pf.revents & POLLIN)) {
            struct win_msg ev;
            if (read_full(wmfd, &ev, sizeof ev) <= 0) break;
            if (ev.type == WIN_EV_CLOSE) break;
        }
    }
    t_close(wmfd);
    return 0;
}
