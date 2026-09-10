/* SPDX-License-Identifier: MIT */
/* toyterm - a toywm client process: owns one window drawn via the protocol */
#include "toy.h"

static struct timespec { long tv_sec; long tv_nsec; } ts = { 0, 200000000 };
static void msleep(long ms) { ts.tv_sec = ms / 1000; ts.tv_nsec = (ms % 1000) * 1000000L; sc2(SYS_nanosleep, (long)&ts, 0); }

static int connect_wm(void) {
    for (int tries = 0; tries < 50; tries++) {
        int fd = (int)t_socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0) {
            struct sockaddr_un sa;
            for (u64 i = 0; i < sizeof sa; i++) ((u8 *)&sa)[i] = 0;
            sa.sun_family = AF_UNIX;
            const char *p = WM_SOCK;
            for (int i = 0; p[i] && i < 107; i++) sa.sun_path[i] = p[i];
            if (t_connect(fd, &sa, sizeof sa) == 0) return fd;
            t_close(fd);
        }
        msleep(100);
    }
    return -1;
}

static void send(int fd, struct win_msg *m) { write_full(fd, m, sizeof *m); }

static void txt(int fd, int x, int y, u32 color, const char *s) {
    struct win_msg m;
    m.type = WIN_TEXT; m.x = x; m.y = y; m.w = 0; m.h = 0; m.color = color;
    int i = 0; for (; s[i] && i < WIN_MSG_LEN - 1; i++) m.data[i] = s[i];
    m.data[i] = 0; m.len = i;
    send(fd, &m);
}

int _start(void) {
    int fd = connect_wm();
    if (fd < 0) { puts_("toyterm: no window manager\n"); return 1; }
    puts_("toyterm: connected to toywm\n");

    long pid = t_getpid();

    struct win_msg m;
    /* create window: 360 x 200, title */
    m.type = WIN_CREATE; m.x = 0; m.y = 0; m.w = 360; m.h = 200; m.color = 0; m.len = 0;
    {
        const char *t = "toyterm (client process)";
        int i = 0; for (; t[i] && i < WIN_MSG_LEN - 1; i++) m.data[i] = t[i];
        m.data[i] = 0; m.len = i;
    }
    send(fd, &m);

    int mx = -1, my = -1, mb = 0;
    int counter = 0;
    char line[80];
    char typed[80];
    int tn = 0;
    typed[0] = 0;

    for (;;) {
        /* redraw our content */
        m.type = WIN_CLEAR; m.color = 0x102030; send(fd, &m);
        m.type = WIN_RECT; m.x = 0; m.y = 0; m.w = 360; m.h = 20; m.color = 0x1E3A5F; send(fd, &m);
        txt(fd, 8, 6, 0x88CCFF, "hello from a separate process!");

        /* "pid: N" */
        {
            char *p = line; const char *a = "pid: ";
            while (*a) *p++ = *a++;
            char nb[16]; int i = 0; long v = pid;
            if (!v) nb[i++] = '0'; while (v) { nb[i++] = '0' + (v % 10); v /= 10; }
            while (i) *p++ = nb[--i];
            *p = 0;
            txt(fd, 8, 36, 0xFFFFFF, line);
        }

        /* counter */
        {
            char *p = line; const char *a = "redraw #";
            while (*a) *p++ = *a++;
            char nb[16]; int i = 0; int v = counter;
            if (!v) nb[i++] = '0'; while (v) { nb[i++] = '0' + (v % 10); v /= 10; }
            while (i) *p++ = nb[--i];
            *p = 0;
            txt(fd, 8, 56, 0xAAFFAA, line);
        }

        txt(fd, 8, 76, 0xFFCC66, "drag my title bar with the mouse");

        if (mx >= 0) {
            char *p = line; const char *a = "last mouse: ";
            while (*a) *p++ = *a++;
            int vals[2]; vals[0] = mx; vals[1] = my;
            for (int k = 0; k < 2; k++) {
                int v = vals[k]; char nb[16]; int i = 0;
                if (!v) nb[i++] = '0'; while (v) { nb[i++] = '0' + (v % 10); v /= 10; }
                while (i) *p++ = nb[--i];
                *p++ = (k == 0) ? ',' : ' ';
            }
            const char *bb = "btn ";
            while (*bb) *p++ = *bb++;
            *p++ = '0' + (mb & 7);
            *p = 0;
            txt(fd, 8, 100, 0xFF88AA, line);
        }

        /* a small animated bar */
        {
            int bw = (counter * 7) % 300;
            m.type = WIN_RECT; m.x = 8; m.y = 130; m.w = 344; m.h = 14; m.color = 0x203040; send(fd, &m);
            m.type = WIN_RECT; m.x = 8; m.y = 130; m.w = bw + 4; m.h = 14; m.color = 0x40A060; send(fd, &m);
        }

        /* typed text (from keyboard events forwarded by toywm) */
        {
            char *p = line; const char *a = "type: ";
            while (*a) *p++ = *a++;
            for (int i = 0; typed[i] && i < 60; i++) *p++ = typed[i];
            *p = 0;
            txt(fd, 8, 156, 0xFFFF66, line);
        }

        m.type = WIN_FLUSH; send(fd, &m);
        counter++;

        /* wait for events for a bit */
        struct pollfd { int fd; short events; short revents; } pf;
        pf.fd = fd; pf.events = POLLIN; pf.revents = 0;
        long pr = t_poll(&pf, 1, 500);
        if (pr > 0 && (pf.revents & POLLIN)) {
            struct win_msg ev;
            long r = read_full(fd, &ev, sizeof ev);
            if (r <= 0) break;
            if (ev.type == WIN_EV_MOUSE) { mx = ev.x; my = ev.y; mb = ev.w; }
            else if (ev.type == WIN_EV_KEY) {
                unsigned char c = (unsigned char)ev.data[0];
                puts_("toyterm: got key\n");
                if (c == 0x7f || c == 0x08) { if (tn > 0) typed[--tn] = 0; }
                else if (c == '\r' || c == '\n') { tn = 0; typed[0] = 0; }
                else if (c >= 0x20 && c < 0x7f) { if (tn < 70) { typed[tn++] = (char)c; typed[tn] = 0; } }
            }
            else if (ev.type == WIN_EV_CLOSE) break;
        }
    }
    t_close(fd);
    return 0;
}
