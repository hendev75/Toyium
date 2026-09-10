/* SPDX-License-Identifier: MIT */
/* toywm - Toyium window manager / compositor
 *
 * Opens /dev/fb0, draws a desktop, runs a window protocol over /run/toywm.sock
 * and manages client processes. Reads /dev/input/mice for the pointer.
 *
 * made by xex & ayham
 */
#include "toy.h"

#define MAXW 16
#define TITLE_H 18
#define BORDER 1

struct window {
    int used;
    int fd;
    int x, y, w, h;
    struct fb canvas;      /* 32bpp client area */
    char title[40];
    int dirty;
};

static struct window wins[MAXW];
static struct fb screen;
static int mouse_fd = -1;
static int mx = 200, my = 150, mbuttons = 0;
static int drag_win = -1, drag_dx = 0, drag_dy = 0;
static int focus = -1;

/* 12x18 arrow cursor (bit0 = leftmost) */
static const unsigned short cursor_bits[18] = {
    0x001,0x003,0x007,0x00F,0x01F,0x03F,0x07F,0x0FF,0x1FF,
    0x3FF,0x06F,0x067,0x0C3,0x0C1,0x180,0x180,0x100,0x000
};

static u32 rgb(int r, int g, int b) { return ((u32)r << 16) | ((u32)g << 8) | (u32)b; }

static int win_total_h(struct window *w) { return TITLE_H + w->h; }

static void draw_cursor(void) {
    for (int row = 0; row < 18; row++) {
        unsigned short bits = cursor_bits[row];
        for (int col = 0; col < 12; col++) {
            if ((bits >> col) & 1) {
                int x = mx + col, y = my + row;
                if (x >= 0 && y >= 0 && x < screen.w && y < screen.h) {
                    /* outline for visibility */
                    u32 c = (col == 0 || row == 0) ? rgb(0,0,0) : rgb(255,255,255);
                    fb_px(&screen, x, y, fb_pack(&screen, c));
                }
            }
        }
    }
}

static void blit_window(struct window *w) {
    for (int yy = 0; yy < w->h; yy++) {
        for (int xx = 0; xx < w->w; xx++) {
            u32 c = w->canvas.px[yy * w->w + xx];
            fb_px(&screen, w->x + xx, w->y + TITLE_H + yy, fb_pack(&screen, c));
        }
    }
}

static void composite(void) {
    /* desktop background */
    gfx_fill(&screen, 0, 0, screen.w, screen.h, rgb(18, 32, 52));
    for (int y = 0; y < screen.h; y += 4)
        gfx_fill(&screen, 0, y, screen.w, 1, rgb(22, 38, 60));

    /* windows bottom -> top */
    for (int i = 0; i < MAXW; i++) {
        struct window *w = &wins[i];
        if (!w->used) continue;
        gfx_fill(&screen, w->x - BORDER, w->y - BORDER, w->w + 2 * BORDER,
                 win_total_h(w) + 2 * BORDER, rgb(0, 0, 0));
        gfx_fill(&screen, w->x, w->y, w->w, TITLE_H,
                 (int)i == focus ? rgb(60, 120, 200) : rgb(70, 70, 90));
        gfx_text(&screen, w->x + 4, w->y + 5, rgb(255, 255, 255), -1, w->title);
        blit_window(w);
    }

    /* taskbar */
    int ty = screen.h - 22;
    gfx_fill(&screen, 0, ty, screen.w, 22, rgb(30, 30, 40));
    gfx_fill(&screen, 0, ty, screen.w, 1, rgb(90, 90, 120));
    gfx_text(&screen, 6, ty + 7, rgb(200, 220, 255), -1, "Toyium WM");
    gfx_text(&screen, screen.w - 150, ty + 7, rgb(160, 160, 180), -1,
             "click+drag title bar");

    draw_cursor();
}

/* bring index i to the top of the z-order */
static void raise_win(int i) {
    if (i < 0 || i >= MAXW || !wins[i].used) return;
    struct window tmp = wins[i];
    for (int j = i; j < MAXW - 1; j++) wins[j] = wins[j + 1];
    wins[MAXW - 1] = tmp;
}

static int top_index(void) {
    for (int i = MAXW - 1; i >= 0; i--) if (wins[i].used) return i;
    return -1;
}

static int hit_test(int x, int y) {
    for (int i = MAXW - 1; i >= 0; i--) {
        struct window *w = &wins[i];
        if (!w->used) continue;
        if (x >= w->x - BORDER && x < w->x + w->w + BORDER &&
            y >= w->y - BORDER && y < w->y + win_total_h(w) + BORDER)
            return i;
    }
    return -1;
}

static void send_msg(int fd, struct win_msg *m) { write_full(fd, m, sizeof *m); }

static void close_win(int i) {
    if (i < 0 || i >= MAXW) return;
    struct window *w = &wins[i];
    if (w->fd >= 0) { t_close(w->fd); }
    w->used = 0;
    w->fd = -1;
    if (focus == i) focus = top_index();
}

static void handle_client(int i) {
    struct window *w = &wins[i];
    struct win_msg m;
    long n = read_full(w->fd, &m, sizeof m);
    if (n <= 0) { close_win(i); return; }

    switch (m.type) {
    case WIN_CLEAR:
        gfx_fill(&w->canvas, 0, 0, w->w, w->h, m.color);
        break;
    case WIN_RECT:
        gfx_fill(&w->canvas, m.x, m.y, m.w, m.h, m.color);
        break;
    case WIN_TEXT: {
        int len = m.len; if (len < 0) len = 0; if (len > WIN_MSG_LEN) len = WIN_MSG_LEN;
        char tmp[WIN_MSG_LEN + 1];
        for (int k = 0; k < len; k++) tmp[k] = m.data[k];
        tmp[len] = 0;
        gfx_text_clip(&w->canvas, m.x, m.y, w->w, w->h, m.color, -1, tmp);
        break;
    }
    case WIN_FLUSH:
        w->dirty = 1;
        break;
    case WIN_DESTROY:
        close_win(i);
        break;
    }
}

static void spawn_client(void) {
    long pid = t_fork();
    if (pid == 0) {
        char *argv[2]; argv[0] = (char *)"/bin/toyterm"; argv[1] = 0;
        char *envp[1]; envp[0] = 0;
        t_execve("/bin/toyterm", argv, envp);
        t_exit_group(1); /* exec failed */
    }
}

static int make_listener(void) {
    int fd = (int)t_socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    t_unlink(WM_SOCK);
    struct sockaddr_un sa;
    for (u64 i = 0; i < sizeof sa; i++) ((u8 *)&sa)[i] = 0;
    sa.sun_family = AF_UNIX;
    const char *p = WM_SOCK;
    for (int i = 0; p[i] && i < 107; i++) sa.sun_path[i] = p[i];
    if (t_bind(fd, &sa, sizeof sa) != 0) return -1;
    if (t_listen(fd, 8) != 0) return -1;
    return fd;
}

static int find_free_win(void) {
    for (int i = 0; i < MAXW; i++) if (!wins[i].used) return i;
    return -1;
}

int _start(void) {
    if (fb_open(&screen, "/dev/fb0") != 0) {
        puts_("toywm: no /dev/fb0 (no graphics device)\n");
        return 1;
    }
    puts_("toywm: fb "); putu_((u64)screen.w); putc_('x'); putu_((u64)screen.h);
    puts_(" bpp "); putu_((u64)screen.bpp); putc_('\n');

    mouse_fd = (int)t_open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
    puts_("toywm: mouse "); puts_(mouse_fd >= 0 ? "ok\n" : "MISSING\n");

    int listen_fd = make_listener();
    if (listen_fd < 0) {
        puts_("toywm: cannot create " WM_SOCK "\n");
        return 1;
    }
    puts_("toywm: listening\n");

    for (int i = 0; i < MAXW; i++) { wins[i].used = 0; wins[i].fd = -1; }

    /* start two client processes (each window is a client process) */
    spawn_client();
    spawn_client();
    puts_("toywm: clients spawned\n");

    struct pollfd {
        int fd; short events; short revents;
    } fds[MAXW + 2];
    int frame = 0;

    for (;;) {
        int n = 0;
        fds[n].fd = listen_fd; fds[n].events = POLLIN; fds[n].revents = 0; n++;
        if (mouse_fd >= 0) { fds[n].fd = mouse_fd; fds[n].events = POLLIN; fds[n].revents = 0; n++; }
        for (int i = 0; i < MAXW; i++) {
            if (wins[i].used && wins[i].fd >= 0) {
                fds[n].fd = wins[i].fd; fds[n].events = POLLIN; fds[n].revents = 0; n++;
            }
        }

        long pr = t_poll(fds, n, 30);
        if (pr > 0) {
            if (fds[0].revents & POLLIN) {
                int cfd = (int)t_accept(listen_fd, 0, 0);
                if (cfd >= 0) {
                    int idx = find_free_win();
                    if (idx < 0) { t_close(cfd); }
                    else {
                        struct win_msg m;
                        long r = read_full(cfd, &m, sizeof m); /* WIN_CREATE */
                        int ww = (r > 0 && m.type == WIN_CREATE) ? m.w : 320;
                        int wh = (r > 0 && m.type == WIN_CREATE) ? m.h : 200;
                        if (ww < 64) ww = 64; if (ww > screen.w - 8) ww = screen.w - 8;
                        if (wh < 48) wh = 48; if (wh > screen.h - 60) wh = screen.h - 60;
                        wins[idx].used = 1;
                        wins[idx].fd = cfd;
                        wins[idx].w = ww; wins[idx].h = wh;
                        wins[idx].x = 40 + idx * 60;
                        wins[idx].y = 40 + idx * 50;
                        for (int k = 0; k < 39; k++) wins[idx].title[k] = 0;
                        const char *t = (r > 0 && m.data[0]) ? m.data : "client";
                        for (int k = 0; k < 39 && t[k]; k++) wins[idx].title[k] = t[k];
                        wins[idx].canvas.w = ww; wins[idx].canvas.h = wh;
                        wins[idx].canvas.bpp = 32;
                        wins[idx].canvas.stride = ww * 4;
                        wins[idx].canvas.mem = (u8 *)t_mmap(0, (u64)ww * wh * 4,
                                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                        wins[idx].canvas.px = (u32 *)wins[idx].canvas.mem;
                        for (int k = 0; k < ww * wh; k++) wins[idx].canvas.px[k] = rgb(20, 20, 30);
                        wins[idx].dirty = 1;
                        focus = idx;
                        puts_("toywm: window created for client\n");
                    }
                }
            }
            for (int k = 1; k < n; k++) {
                if (fds[k].fd == mouse_fd) {
                    if (fds[k].revents & POLLIN) {
                        u8 pkt[64];
                        long r = t_read(mouse_fd, pkt, sizeof pkt);
                        int oldbtn = mbuttons;
                        for (long o = 0; o + 2 < r; o += 3) {
                            mbuttons = pkt[o] & 0x7;
                            int dx = (signed char)pkt[o + 1];
                            int dy = (signed char)pkt[o + 2];
                            mx += dx; my -= dy;
                            if (mx < 0) mx = 0; if (mx >= screen.w) mx = screen.w - 1;
                            if (my < 0) my = 0; if (my >= screen.h) my = screen.h - 1;
                        }
                        int lbtn = mbuttons & 1, old = oldbtn & 1;
                        if (lbtn && !old) {
                            int hi = hit_test(mx, my);
                            if (hi >= 0) {
                                focus = hi;
                                raise_win(hi);
                                focus = top_index();
                                struct window *w = &wins[focus];
                                if (my < w->y + TITLE_H) { drag_win = focus; drag_dx = mx - w->x; drag_dy = my - w->y; }
                            }
                        } else if (!lbtn && old) {
                            drag_win = -1;
                        }
                        if (drag_win >= 0) {
                            struct window *w = &wins[drag_win];
                            w->x = mx - drag_dx; w->y = my - drag_dy;
                            if (w->x < 0) w->x = 0;
                            if (w->y < 0) w->y = 0;
                            if (w->x + w->w > screen.w) w->x = screen.w - w->w;
                            if (w->y + win_total_h(w) > screen.h - 22) w->y = screen.h - 22 - win_total_h(w);
                        }
                        if (focus >= 0 && wins[focus].used) {
                            struct win_msg ev;
                            ev.type = WIN_EV_MOUSE; ev.x = mx - wins[focus].x;
                            ev.y = my - wins[focus].y - TITLE_H; ev.w = mbuttons; ev.len = 0;
                            send_msg(wins[focus].fd, &ev);
                        }
                    }
                } else {
                    int wi = -1;
                    for (int q = 0; q < MAXW; q++) if (wins[q].used && wins[q].fd == fds[k].fd) { wi = q; break; }
                    if (wi >= 0 && (fds[k].revents & POLLIN)) handle_client(wi);
                    else if (wi >= 0 && (fds[k].revents & 0x10)) close_win(wi); /* POLLHUP */
                }
            }
        }
        composite();
        if (frame == 0) { puts_("toywm: first frame\n"); frame = 1; }
    }
    return 0;
}
