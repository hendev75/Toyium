/* SPDX-License-Identifier: MIT */
/* toywm - Toyium desktop: wallpaper, desktop icons, panel + start menu,
 *         window manager and compositor. Client windows are separate
 *         processes that talk to the WM over /run/toywm.sock.
 *
 * made by xex & ayham
 */
#include "toy.h"

#define MAXW 16
#define TITLE_H 18
#define BORDER 1
#define CLOSE_W 18
#define PANEL_H 30
#define MENU_W 170
#define MENU_ITEM_H 24

struct window {
    int used;
    int fd;
    int x, y, w, h;
    struct fb canvas;
    char title[40];
    int dirty;
    int minimized;
};

struct desktop_icon {
    int x, y;
    const char *label;
    const char *cmd;
    u32 color;
};

static struct window wins[MAXW];
static struct fb screen;
static int mouse_fd = -1;
static int mx = 300, my = 200, mbuttons = 0;
static int drag_win = -1, drag_dx = 0, drag_dy = 0;
static int focus = -1;
static int menu_open = 0;
static char clock_str[16] = "--:--:--";

/* --- app catalogue --------------------------------------------------- */
static const struct desktop_icon icons[] = {
    { 30,  30, "Terminal", "toyterm", 0x40A060 },
    { 30, 120, "Files",    "toyfiles", 0xE0A040 },
    { 30, 210, "About",    "toyinfo", 0x4090E0 },
};
#define NICONS ((int)(sizeof(icons) / sizeof(icons[0])))

static const char *menu_labels[NICONS] = { "Terminal", "Files", "About Toyium" };
static const char *menu_cmds[NICONS]   = { "toyterm",  "toyfiles", "toyinfo" };

/* 12x18 arrow cursor (bit0 = leftmost) */
static const unsigned short cursor_bits[18] = {
    0x001,0x003,0x007,0x00F,0x01F,0x03F,0x07F,0x0FF,0x1FF,
    0x3FF,0x06F,0x067,0x0C3,0x0C1,0x180,0x180,0x100,0x000
};

static u32 rgb(int r, int g, int b) { return ((u32)r << 16) | ((u32)g << 8) | (u32)b; }

static int win_total_h(struct window *w) { return TITLE_H + w->h; }

/* detach/attach the framebuffer console so the kernel stops drawing terminal
 * text over our desktop while the WM owns the screen */
static void fbcon_set(int on) {
    for (int i = 0; i < 2; i++) {
        char p[48]; int n = 0;
        const char *a = "/sys/class/vtconsole/vtcon";
        while (a[n]) { p[n] = a[n]; n++; }
        p[n++] = '0' + i;
        const char *b = "/bind"; int j = 0; while (b[j]) p[n++] = b[j++];
        p[n] = 0;
        int fd = (int)t_open(p, O_WRONLY);
        if (fd >= 0) { t_write(fd, on ? "1" : "0", 1); t_close(fd); }
    }
}

static void launch(const char *name) {
    char path[64]; int i = 0;
    const char *a = "/bin/"; while (a[i]) { path[i] = a[i]; i++; }
    for (int j = 0; name[j] && i < 62; j++) path[i++] = name[j];
    path[i] = 0;
    long pid = t_fork();
    if (pid == 0) {
        char *argv[2]; argv[0] = path; argv[1] = 0;
        t_execve(path, argv, (char **)0);
        t_exit_group(127);
    }
}

/* --- drawing helpers ------------------------------------------------- */
static void draw_cursor(void) {
    for (int row = 0; row < 18; row++) {
        unsigned short bits = cursor_bits[row];
        for (int col = 0; col < 12; col++) {
            if ((bits >> col) & 1) {
                int x = mx + col, y = my + row;
                if (x >= 0 && y >= 0 && x < screen.w && y < screen.h) {
                    u32 c = (col == 0 || row == 0) ? rgb(0,0,0) : rgb(255,255,255);
                    fb_px(&screen, x, y, fb_pack(&screen, c));
                }
            }
        }
    }
}

static void blit_window(struct window *w) {
    for (int yy = 0; yy < w->h; yy++)
        for (int xx = 0; xx < w->w; xx++) {
            u32 c = w->canvas.px[yy * w->w + xx];
            fb_px(&screen, w->x + xx, w->y + TITLE_H + yy, fb_pack(&screen, c));
        }
}

static void draw_wallpaper(void) {
    for (int y = 0; y < screen.h; y++) {
        int t = y * 255 / screen.h;
        int r = 16 + t * 18 / 255;
        int g = 28 + t * 26 / 255;
        int b = 48 + t * 40 / 255;
        gfx_fill(&screen, 0, y, screen.w, 1, rgb(r, g, b));
    }
    /* subtle diagonal sheen */
    for (int i = -screen.h; i < screen.w; i += 48)
        for (int y = 0; y < screen.h; y += 2) {
            int x = i + y;
            if (x >= 0 && x < screen.w) {
                u32 c = screen.px[y * (screen.stride / 4) + x];
                int r = ((c >> 16) & 0xFF) + 6, g = ((c >> 8) & 0xFF) + 6, b = (c & 0xFF) + 6;
                fb_px(&screen, x, y, fb_pack(&screen, rgb(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b)));
            }
        }
    /* watermarked logo */
    gfx_text(&screen, screen.w - 220, 40, rgb(40, 70, 110), -1, "T O Y I U M");
    gfx_text(&screen, screen.w - 218, 54, rgb(30, 55, 90), -1, "made by xex & ayham");
}

static void draw_icon_box(int x, int y, u32 color) {
    gfx_fill(&screen, x, y, 34, 34, rgb(20, 24, 34));
    gfx_rect(&screen, x, y, 34, 34, rgb(90, 100, 130));
    gfx_fill(&screen, x + 4, y + 4, 26, 26, color);
    gfx_fill(&screen, x + 4, y + 4, 26, 4, rgb(255, 255, 255));
}

static void draw_icons(void) {
    for (int i = 0; i < NICONS; i++) {
        draw_icon_box(icons[i].x, icons[i].y, icons[i].color);
        gfx_text(&screen, icons[i].x - 8, icons[i].y + 38, rgb(220, 230, 255), -1, icons[i].label);
    }
}

static void panel_win_rect(int i, int *bx, int *bw) {
    int x = 84;
    for (int k = 0; k < MAXW; k++) {
        if (!wins[k].used) continue;
        if (k == i) { *bx = x; *bw = 130; return; }
        x += 134;
    }
    *bx = 84; *bw = 130;
}

static void draw_panel(void) {
    int py = screen.h - PANEL_H;
    gfx_fill(&screen, 0, py, screen.w, PANEL_H, rgb(24, 28, 40));
    gfx_fill(&screen, 0, py, screen.w, 1, rgb(80, 90, 120));

    /* start button */
    gfx_fill(&screen, 4, py + 4, 72, PANEL_H - 8, menu_open ? rgb(70, 110, 180) : rgb(40, 60, 100));
    gfx_text(&screen, 12, py + 10, rgb(220, 235, 255), -1, "Toyium");

    /* window buttons */
    for (int i = 0; i < MAXW; i++) {
        if (!wins[i].used) continue;
        int bx, bw; panel_win_rect(i, &bx, &bw);
        gfx_fill(&screen, bx, py + 4, bw, PANEL_H - 8, (i == focus) ? rgb(60, 80, 130) : rgb(36, 42, 60));
        char t[14]; int n = 0;
        for (int j = 0; wins[i].title[j] && j < 13; j++) t[n++] = wins[i].title[j];
        t[n] = 0;
        gfx_text(&screen, bx + 6, py + 10, rgb(210, 220, 240), -1, t);
    }

    /* clock */
    gfx_text(&screen, screen.w - 86, py + 10, rgb(180, 220, 255), -1, clock_str);
}

static void draw_start_menu(void) {
    if (!menu_open) return;
    int py = screen.h - PANEL_H;
    int mh = NICONS * MENU_ITEM_H + 8;
    int my0 = py - mh;
    gfx_fill(&screen, 4, my0, MENU_W, mh, rgb(34, 40, 56));
    gfx_rect(&screen, 4, my0, MENU_W, mh, rgb(90, 110, 150));
    for (int i = 0; i < NICONS; i++) {
        int iy = my0 + 4 + i * MENU_ITEM_H;
        gfx_fill(&screen, 8, iy + 2, 10, 10, icons[i].color);
        gfx_text(&screen, 26, iy + 6, rgb(225, 235, 255), -1, menu_labels[i]);
    }
}

static void composite(void) {
    draw_wallpaper();
    draw_icons();

    for (int i = 0; i < MAXW; i++) {
        struct window *w = &wins[i];
        if (!w->used || w->minimized) continue;
        gfx_fill(&screen, w->x - BORDER, w->y - BORDER, w->w + 2 * BORDER,
                 win_total_h(w) + 2 * BORDER, rgb(0, 0, 0));
        gfx_fill(&screen, w->x, w->y, w->w, TITLE_H,
                 (int)i == focus ? rgb(60, 120, 200) : rgb(70, 70, 90));
        gfx_text(&screen, w->x + 4, w->y + 5, rgb(255, 255, 255), -1, w->title);
        gfx_fill(&screen, w->x + w->w - CLOSE_W, w->y, CLOSE_W, TITLE_H,
                 (int)i == focus ? rgb(200, 60, 60) : rgb(90, 60, 60));
        gfx_text(&screen, w->x + w->w - CLOSE_W + 5, w->y + 5, rgb(255, 255, 255), -1, "x");
        blit_window(w);
    }

    draw_panel();
    draw_start_menu();
    draw_cursor();
}

/* --- window z-order -------------------------------------------------- */
static void raise_win(int i) {
    if (i < 0 || i >= MAXW || !wins[i].used) return;
    struct window tmp = wins[i];
    for (int j = i; j < MAXW - 1; j++) wins[j] = wins[j + 1];
    wins[MAXW - 1] = tmp;
}
static int top_index(void) {
    for (int i = MAXW - 1; i >= 0; i--) if (wins[i].used && !wins[i].minimized) return i;
    return -1;
}
static int hit_test(int x, int y) {
    for (int i = MAXW - 1; i >= 0; i--) {
        struct window *w = &wins[i];
        if (!w->used || w->minimized) continue;
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
    if (w->fd >= 0) {
        struct win_msg ev; ev.type = WIN_EV_CLOSE; ev.x = ev.y = ev.w = 0; ev.len = 0;
        send_msg(w->fd, &ev);
        t_close(w->fd);
    }
    w->used = 0; w->fd = -1;
    if (focus == i) focus = top_index();
}

static void handle_client(int i) {
    struct window *w = &wins[i];
    struct win_msg m;
    long n = read_full(w->fd, &m, sizeof m);
    if (n <= 0) { close_win(i); return; }
    switch (m.type) {
    case WIN_CLEAR: gfx_fill(&w->canvas, 0, 0, w->w, w->h, m.color); break;
    case WIN_RECT:  gfx_fill(&w->canvas, m.x, m.y, m.w, m.h, m.color); break;
    case WIN_TEXT: {
        int len = m.len; if (len < 0) len = 0; if (len > WIN_MSG_LEN) len = WIN_MSG_LEN;
        char tmp[WIN_MSG_LEN + 1];
        for (int k = 0; k < len; k++) tmp[k] = m.data[k];
        tmp[len] = 0;
        gfx_text_clip(&w->canvas, m.x, m.y, w->w, w->h, m.color, -1, tmp);
        break;
    }
    case WIN_FLUSH: w->dirty = 1; break;
    case WIN_DESTROY: close_win(i); break;
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

static void update_clock(void) {
    struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 0;
    sc2(SYS_clock_gettime, CLOCK_REALTIME, (long)&ts);
    long s = ts.tv_sec;
    int hh = (int)((s / 3600) % 24), mm = (int)((s / 60) % 60), ss = (int)(s % 60);
    clock_str[0] = '0' + hh / 10; clock_str[1] = '0' + hh % 10; clock_str[2] = ':';
    clock_str[3] = '0' + mm / 10; clock_str[4] = '0' + mm % 10; clock_str[5] = ':';
    clock_str[6] = '0' + ss / 10; clock_str[7] = '0' + ss % 10; clock_str[8] = 0;
}

static void do_click(void) {
    int py = screen.h - PANEL_H;

    /* start menu open? */
    if (menu_open) {
        int mh = NICONS * MENU_ITEM_H + 8;
        int my0 = py - mh;
        if (mx >= 4 && mx < 4 + MENU_W && my >= my0 && my < py) {
            int idx = (my - (my0 + 4)) / MENU_ITEM_H;
            if (idx >= 0 && idx < NICONS) launch(menu_cmds[idx]);
        }
        menu_open = 0;
        return;
    }

    /* panel clicks */
    if (my >= py) {
        if (mx >= 4 && mx < 76) { menu_open = 1; return; }
        for (int i = 0; i < MAXW; i++) {
            if (!wins[i].used) continue;
            int bx, bw; panel_win_rect(i, &bx, &bw);
            if (mx >= bx && mx < bx + bw) {
                wins[i].minimized = 0;
                focus = i; raise_win(i); focus = top_index();
                return;
            }
        }
        return;
    }

    /* desktop icons */
    for (int i = 0; i < NICONS; i++) {
        if (mx >= icons[i].x && mx < icons[i].x + 34 &&
            my >= icons[i].y && my < icons[i].y + 34) {
            launch(icons[i].cmd);
            return;
        }
    }

    /* windows */
    int hi = hit_test(mx, my);
    if (hi >= 0) {
        struct window *w = &wins[hi];
        if (mx >= w->x + w->w - CLOSE_W && my < w->y + TITLE_H) { close_win(hi); return; }
        focus = hi; raise_win(hi); focus = top_index();
        w = &wins[focus];
        if (my < w->y + TITLE_H) { drag_win = focus; drag_dx = mx - w->x; drag_dy = my - w->y; }
    }
}

int _start(void) {
    if (fb_open(&screen, "/dev/fb0") != 0) { puts_("toywm: no /dev/fb0\n"); return 1; }
    puts_("toywm: fb "); putu_((u64)screen.w); putc_('x'); putu_((u64)screen.h); putc_('\n');

    fbcon_set(0);   /* take the screen away from the kernel console */
    t_raw(0);       /* read raw keys from the console */

    mouse_fd = (int)t_open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
    puts_("toywm: mouse "); puts_(mouse_fd >= 0 ? "ok\n" : "MISSING\n");

    int listen_fd = make_listener();
    if (listen_fd < 0) { puts_("toywm: cannot create " WM_SOCK "\n"); return 1; }

    for (int i = 0; i < MAXW; i++) { wins[i].used = 0; wins[i].fd = -1; wins[i].minimized = 0; }
    update_clock();

    /* welcome window */
    launch("toyterm");
    puts_("toywm: desktop up\n");

    struct pollfd { int fd; short events; short revents; } fds[MAXW + 3];
    int frame = 0, kbd_fd = 0, running = 1;

    for (;;) {
        update_clock();

        int n = 0;
        fds[n].fd = listen_fd; fds[n].events = POLLIN; fds[n].revents = 0; n++;
        if (mouse_fd >= 0) { fds[n].fd = mouse_fd; fds[n].events = POLLIN; fds[n].revents = 0; n++; }
        if (kbd_fd >= 0)   { fds[n].fd = kbd_fd;   fds[n].events = POLLIN; fds[n].revents = 0; n++; }
        for (int i = 0; i < MAXW; i++)
            if (wins[i].used && wins[i].fd >= 0) { fds[n].fd = wins[i].fd; fds[n].events = POLLIN; fds[n].revents = 0; n++; }

        long pr = t_poll(fds, n, 200);
        if (pr > 0) {
            if (fds[0].revents & POLLIN) {
                int cfd = (int)t_accept(listen_fd, 0, 0);
                if (cfd >= 0) {
                    int idx = find_free_win();
                    if (idx < 0) t_close(cfd);
                    else {
                        struct win_msg m;
                        long r = read_full(cfd, &m, sizeof m);
                        int ww = (r > 0 && m.type == WIN_CREATE) ? m.w : 320;
                        int wh = (r > 0 && m.type == WIN_CREATE) ? m.h : 200;
                        if (ww < 64) ww = 64; if (ww > screen.w - 8) ww = screen.w - 8;
                        if (wh < 48) wh = 48; if (wh > screen.h - PANEL_H - 40) wh = screen.h - PANEL_H - 40;
                        wins[idx].used = 1; wins[idx].fd = cfd;
                        wins[idx].w = ww; wins[idx].h = wh;
                        wins[idx].x = 120 + (idx % 5) * 40; wins[idx].y = 60 + (idx % 5) * 30;
                        wins[idx].minimized = 0;
                        for (int k = 0; k < 39; k++) wins[idx].title[k] = 0;
                        const char *t = (r > 0 && m.data[0]) ? m.data : "client";
                        for (int k = 0; k < 39 && t[k]; k++) wins[idx].title[k] = t[k];
                        wins[idx].canvas.w = ww; wins[idx].canvas.h = wh;
                        wins[idx].canvas.bpp = 32; wins[idx].canvas.stride = ww * 4;
                        wins[idx].canvas.mem = (u8 *)t_mmap(0, (u64)ww * wh * 4,
                                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                        wins[idx].canvas.px = (u32 *)wins[idx].canvas.mem;
                        for (int k = 0; k < ww * wh; k++) wins[idx].canvas.px[k] = rgb(20, 20, 30);
                        wins[idx].dirty = 1; focus = idx;
                        puts_("toywm: window '"); puts_(wins[idx].title); puts_("'\n");
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
                            mx += (signed char)pkt[o + 1];
                            my -= (signed char)pkt[o + 2];
                            if (mx < 0) mx = 0; if (mx >= screen.w) mx = screen.w - 1;
                            if (my < 0) my = 0; if (my >= screen.h) my = screen.h - 1;
                        }
                        int lbtn = mbuttons & 1, old = oldbtn & 1;
                        if (lbtn && !old && drag_win < 0) do_click();
                        else if (!lbtn && old) drag_win = -1;
                        if (drag_win >= 0) {
                            struct window *w = &wins[drag_win];
                            w->x = mx - drag_dx; w->y = my - drag_dy;
                            if (w->x < 0) w->x = 0;
                            if (w->y < 0) w->y = 0;
                            if (w->x + w->w > screen.w) w->x = screen.w - w->w;
                            if (w->y + win_total_h(w) > screen.h - PANEL_H) w->y = screen.h - PANEL_H - win_total_h(w);
                        }
                        if (focus >= 0 && wins[focus].used) {
                            struct win_msg ev;
                            ev.type = WIN_EV_MOUSE; ev.x = mx - wins[focus].x;
                            ev.y = my - wins[focus].y - TITLE_H; ev.w = mbuttons; ev.len = 0;
                            send_msg(wins[focus].fd, &ev);
                        }
                    }
                } else if (fds[k].fd == kbd_fd) {
                    if (fds[k].revents & POLLIN) {
                        char kb[64];
                        long r = t_read(kbd_fd, kb, sizeof kb);
                        for (long o = 0; o < r; o++) {
                            unsigned char c = (unsigned char)kb[o];
                            if (c == 0x03) { running = 0; break; }
                            if (focus >= 0 && wins[focus].used) {
                                struct win_msg ev;
                                ev.type = WIN_EV_KEY; ev.x = ev.y = ev.w = 0;
                                ev.len = 1; ev.data[0] = (char)c;
                                send_msg(wins[focus].fd, &ev);
                            }
                        }
                    }
                } else {
                    int wi = -1;
                    for (int q = 0; q < MAXW; q++) if (wins[q].used && wins[q].fd == fds[k].fd) { wi = q; break; }
                    if (wi >= 0 && (fds[k].revents & POLLIN)) handle_client(wi);
                    else if (wi >= 0 && (fds[k].revents & 0x10)) close_win(wi);
                }
            }
        }
        composite();
        if (frame == 0) { puts_("toywm: first frame\n"); frame = 1; }
        while (t_wait4(-1, (int *)0, 1, 0) > 0) { }
        if (!running) break;
    }

    for (int i = 0; i < MAXW; i++) if (wins[i].used) close_win(i);
    t_close(listen_fd);
    t_unlink(WM_SOCK);
    fbcon_set(1);   /* give the screen back to the console */
    puts_("toywm: exit\n");
    return 0;
}
