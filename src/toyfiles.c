/* SPDX-License-Identifier: MIT */
/* toyfiles - a tiny GUI file browser (toywm client) */
#include "toy.h"

struct linux_dirent64 { u64 d_ino; s64 d_off; u16 d_reclen; u8 d_type; char d_name[]; };

#define MAXE 18
static struct { char name[40]; int isdir; } ents[MAXE];
static int nents = 0;
static char cwd[128] = "/toy";
static int wmfd = -1;

static void msleep(long ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; sc2(SYS_nanosleep, (long)&t, 0); }

static void send(struct win_msg *m) { write_full(wmfd, m, sizeof *m); }
static void txt(int x, int y, u32 color, const char *s) {
    struct win_msg m; m.type = WIN_TEXT; m.x = x; m.y = y; m.w = m.h = 0; m.color = color;
    int i = 0; for (; s[i] && i < WIN_MSG_LEN - 1; i++) m.data[i] = s[i];
    m.data[i] = 0; m.len = i; send(&m);
}
static void rect(int x, int y, int w, int h, u32 c) {
    struct win_msg m; m.type = WIN_RECT; m.x = x; m.y = y; m.w = w; m.h = h; m.color = c; send(&m);
}

static void joinpath(char *out, const char *dir, const char *name) {
    int i = 0;
    while (dir[i]) { out[i] = dir[i]; i++; }
    if (i == 0 || out[i - 1] != '/') out[i++] = '/';
    for (int j = 0; name[j] && i < 120; j++) out[i++] = name[j];
    out[i] = 0;
}

static void reload(void) {
    nents = 0;
    int fd = (int)t_open(cwd, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return;
    static char buf[4096];
    for (;;) {
        long n = sc3(SYS_getdents64, fd, (long)buf, (long)sizeof buf);
        if (n <= 0) break;
        u64 off = 0;
        while (off < (u64)n && nents < MAXE) {
            struct linux_dirent64 *de = (struct linux_dirent64 *)(buf + off);
            if (de->d_name[0] != '.') {
                int i = 0;
                for (; de->d_name[i] && i < 39; i++) ents[nents].name[i] = de->d_name[i];
                ents[nents].name[i] = 0;
                char full[160]; joinpath(full, cwd, ents[nents].name);
                int dfd = (int)t_open(full, O_RDONLY | O_DIRECTORY);
                ents[nents].isdir = (dfd >= 0);
                if (dfd >= 0) t_close(dfd);
                nents++;
            }
            off += de->d_reclen;
        }
    }
    t_close(fd);
}

static void draw(void) {
    struct win_msg m; m.type = WIN_CLEAR; m.color = 0x14161E; send(&m);
    rect(0, 0, 380, 22, 0x223A5E);
    txt(8, 7, 0xBFD8FF, "Files");
    txt(70, 7, 0xE0E0E0, cwd);

    for (int i = 0; i < nents; i++) {
        int y = 30 + i * 12;
        if (ents[i].isdir) { rect(6, y, 8, 8, 0xE0A040); txt(20, y, 0xFFD080, ents[i].name); }
        else txt(20, y, 0xC0C8D8, ents[i].name);
    }
    if (nents == 0) txt(20, 30, 0x808898, "(empty)");
    txt(8, 380 - 16, 0x7080A0, "click a folder to open");

    m.type = WIN_FLUSH; send(&m);
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
    if (wmfd < 0) { puts_("toyfiles: no toywm\n"); return 1; }

    struct win_msg m; m.type = WIN_CREATE; m.x = m.y = 0; m.w = 380; m.h = 380; m.color = 0; m.len = 0;
    const char *t = "Files"; int i = 0; for (; t[i]; i++) m.data[i] = t[i]; m.data[i] = 0; m.len = i;
    send(&m);

    reload();
    for (;;) {
        draw();
        struct pollfd { int fd; short events; short revents; } pf;
        pf.fd = wmfd; pf.events = POLLIN; pf.revents = 0;
        long pr = t_poll(&pf, 1, 400);
        if (pr > 0 && (pf.revents & POLLIN)) {
            struct win_msg ev;
            if (read_full(wmfd, &ev, sizeof ev) <= 0) break;
            if (ev.type == WIN_EV_MOUSE && (ev.w & 1)) {
                int idx = (ev.y - 30) / 12;
                if (idx >= 0 && idx < nents && ents[idx].isdir) {
                    char full[160]; joinpath(full, cwd, ents[idx].name);
                    int j = 0; for (; full[j] && j < 127; j++) cwd[j] = full[j];
                    cwd[j] = 0;
                    reload();
                }
            } else if (ev.type == WIN_EV_CLOSE) break;
        }
    }
    t_close(wmfd);
    return 0;
}
