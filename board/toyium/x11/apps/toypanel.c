/* toypanel - Toyium dock (Xlib, no toolkit).
 *
 * Vertical taskbar on the left, Ubuntu-style: launcher buttons with
 * hover / press / launch-ring animations, network status dot and a
 * live clock at the bottom. Registers as DOCK with a left strut.
 *
 * made by xex & ayham
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DOCK_W 64
#define BTN 56

static Display *dpy;
static Window win;
static GC gc;
static XFontStruct *font;
static int scr, sH;
static unsigned long c_bg, c_fg, c_btn, c_hover, c_press, c_acc, c_green,
    c_red;

struct btn {
    Window w;
    const char *label;
    const char *icon;
    const char *cmd;
    int y;
    int hover;
    int pressed;
};

static struct btn btns[3];
static int anim_btn = -1;
static long anim_until = 0;

static long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

static void net_status(char *out, size_t cap, int *up, int *wlan)
{
    DIR *d;
    struct dirent *e;
    char best[32] = "";
    int bup = 0, bwlan = 0;
    struct ifaddrs *ifa, *p;
    char ip[64] = "";

    d = opendir("/sys/class/net");
    if (d) {
        while ((e = readdir(d)) != NULL) {
            char path[128], state[16];
            FILE *f;
            int is_up = 0, is_wlan = 0;
            if (e->d_name[0] == '.')
                continue;
            if (strcmp(e->d_name, "lo") == 0)
                continue;
            snprintf(path, sizeof(path), "/sys/class/net/%s/operstate",
                     e->d_name);
            f = fopen(path, "r");
            if (f) {
                if (fgets(state, sizeof(state), f))
                    is_up = (strncmp(state, "up", 2) == 0);
                fclose(f);
            }
            snprintf(path, sizeof(path), "/sys/class/net/%s/wireless",
                     e->d_name);
            {
                DIR *w = opendir(path);
                if (w) {
                    is_wlan = 1;
                    closedir(w);
                }
            }
            if (!bup && is_up) {
                strncpy(best, e->d_name, sizeof(best) - 1);
                best[sizeof(best) - 1] = '\0';
                bwlan = is_wlan;
                bup = 1;
                if (is_wlan)
                    break;
            } else if (!bup && best[0] == '\0') {
                strncpy(best, e->d_name, sizeof(best) - 1);
                best[sizeof(best) - 1] = '\0';
                bwlan = is_wlan;
            }
        }
        closedir(d);
    }
    if (bup && getifaddrs(&ifa) == 0) {
        for (p = ifa; p; p = p->ifa_next) {
            struct sockaddr_in *a;
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
                continue;
            if (strcmp(p->ifa_name, best) != 0)
                continue;
            a = (struct sockaddr_in *)p->ifa_addr;
            if ((a->sin_addr.s_addr & 0xFF) == 127)
                continue;
            inet_ntop(AF_INET, &a->sin_addr, ip, sizeof(ip));
            break;
        }
        freeifaddrs(ifa);
    }
    *up = bup;
    *wlan = bwlan;
    if (bup)
        snprintf(out, cap, "%s %s", best, ip);
    else if (best[0])
        snprintf(out, cap, "%s down", best);
    else
        snprintf(out, cap, "no net");
}

static int text_w(const char *s)
{
    int dir, asc, desc;
    XCharStruct o;
    XTextExtents(font, s, strlen(s), &dir, &asc, &desc, &o);
    return o.rbearing - o.lbearing;
}

static void text_c(Window w, int cx, int y, const char *s, unsigned long fg)
{
    int dir, asc, desc;
    XCharStruct o;
    XTextExtents(font, s, strlen(s), &dir, &asc, &desc, &o);
    XSetForeground(dpy, gc, fg);
    XDrawString(dpy, w, gc, cx - (o.rbearing + o.lbearing) / 2, y, s,
                strlen(s));
}

static void draw_btn(struct btn *b)
{
    unsigned long bg = c_btn;
    int asc = font->ascent;
    if (b->pressed)
        bg = c_press;
    else if (b->hover)
        bg = c_hover;
    XSetForeground(dpy, gc, bg);
    XFillRectangle(dpy, b->w, gc, 0, 0, BTN, BTN);
    XSetForeground(dpy, gc, c_acc);
    XDrawRectangle(dpy, b->w, gc, 0, 0, BTN - 1, BTN - 1);
    /* icon */
    if (strcmp(b->icon, "T") == 0) {
        XSetForeground(dpy, gc, c_acc);
        XFillRectangle(dpy, b->w, gc, 14, 6, 28, 20);
        text_c(b->w, BTN / 2, 6 + (20 + asc) / 2, "T", c_bg);
    } else if (strcmp(b->icon, ">_") == 0) {
        XSetForeground(dpy, gc, c_fg);
        XDrawRectangle(dpy, b->w, gc, 12, 8, 32, 20);
        text_c(b->w, BTN / 2, 8 + (20 + asc) / 2, ">_", c_fg);
    } else {
        int i;
        XSetForeground(dpy, gc, c_fg);
        XDrawRectangle(dpy, b->w, gc, 16, 6, 24, 24);
        for (i = 1; i < 3; i++)
            XDrawLine(dpy, b->w, gc, 16, 6 + i * 8, 40, 6 + i * 8);
        text_c(b->w, BTN / 2, 6 + 24 + asc + 2, "=", c_acc);
    }
    text_c(b->w, BTN / 2, BTN - 4, b->label, c_fg);
}

static void draw_bottom(void)
{
    char clk[16], dat[16], net[128];
    time_t t;
    struct tm *tm;
    int up = 0, wlan = 0;

    t = time(NULL);
    tm = localtime(&t);
    strftime(clk, sizeof(clk), "%H:%M", tm);
    strftime(dat, sizeof(dat), "%m-%d", tm);
    net_status(net, sizeof(net), &up, &wlan);

    XSetForeground(dpy, gc, c_bg);
    XFillRectangle(dpy, win, gc, 0, sH - 120, DOCK_W, 120);
    /* net dot */
    XSetForeground(dpy, gc, up ? c_green : c_red);
    XFillArc(dpy, win, gc, DOCK_W / 2 - 5, sH - 112, 10, 10, 0, 360 * 64);
    text_c(win, DOCK_W / 2, sH - 88, up ? (wlan ? "W" : "E") : "X", c_fg);
    text_c(win, DOCK_W / 2, sH - 60, clk, c_fg);
    text_c(win, DOCK_W / 2, sH - 44, dat, c_acc);
    {
        char ip[96];
        const char *sp = strchr(net, ' ');
        snprintf(ip, sizeof(ip), "%s", sp ? sp + 1 : net);
        if ((int)strlen(ip) > 10) {
            ip[10] = '\0';
        }
        text_c(win, DOCK_W / 2, sH - 14, ip, c_fg);
    }
}

static void draw_ring(int step)
{
    int cx = DOCK_W / 2, cy = btns[anim_btn].y + BTN / 2;
    int r = 8 + step * 6;
    XSetForeground(dpy, gc, c_acc);
    XDrawArc(dpy, win, gc, cx - r, cy - r, r * 2, r * 2, 0, 360 * 64);
}

static void draw_all(void)
{
    int i;
    XSetForeground(dpy, gc, c_bg);
    XFillRectangle(dpy, win, gc, 0, 0, DOCK_W, sH);
    for (i = 0; i < 3; i++)
        draw_btn(&btns[i]);
    draw_bottom();
}

static void launch(const char *cmd)
{
    if (fork() == 0) {
        setsid();
        execlp(cmd, cmd, (char *)NULL);
        _exit(127);
    }
    anim_btn = -1;
}

int main(void)
{
    const char *fonts[] = { "fixed", "9x15", "6x13", NULL };
    const char *labels[] = { "Toyium", "Term", "Calc" };
    const char *icons[] = { "T", ">_", "+-" };
    const char *cmds[] = { "xterm", "xterm", "toycalc" };
    const int ys[] = { 8, 76, 140 };
    int i, xfd;
    Atom wt, dock, st, stp;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "toypanel: cannot open display\n");
        return 1;
    }
    scr = DefaultScreen(dpy);
    sH = DisplayHeight(dpy, scr);
    {
        XColor xc;
        Colormap cm = DefaultColormap(dpy, scr);
        unsigned long *slots[] = { &c_bg, &c_fg, &c_btn, &c_hover,
                                   &c_press, &c_acc, &c_green, &c_red };
        unsigned vals[] = { 0x1E3A5F, 0xE0F0FF, 0x2A4A73, 0x3A5A8C,
                            0x16233C, 0x9CD0FF, 0x40C040, 0xC04040 };
        for (i = 0; i < 8; i++) {
            xc.red = ((vals[i] >> 16) & 0xFF) * 257;
            xc.green = ((vals[i] >> 8) & 0xFF) * 257;
            xc.blue = (vals[i] & 0xFF) * 257;
            if (XAllocColor(dpy, cm, &xc))
                *slots[i] = xc.pixel;
            else
                *slots[i] = (i == 0) ? BlackPixel(dpy, scr) : WhitePixel(dpy, scr);
        }
    }
    font = NULL;
    for (i = 0; fonts[i]; i++) {
        font = XLoadQueryFont(dpy, fonts[i]);
        if (font)
            break;
    }
    if (!font) {
        fprintf(stderr, "toypanel: no font\n");
        return 1;
    }

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, DOCK_W, sH,
                              0, BlackPixel(dpy, scr), c_bg);
    XStoreName(dpy, win, "Toyium Panel");
    {
        XClassHint ch;
        ch.res_name = "toypanel";
        ch.res_class = "ToyPanel";
        XSetClassHint(dpy, win, &ch);
    }
    wt = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(dpy, win, wt, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&dock, 1);
    {
        long strut[4] = { DOCK_W, 0, 0, 0 };
        long sp[12] = { DOCK_W, 0, 0, 0, 0, sH - 1, 0, 0, 0, 0, 0, 0 };
        st = XInternAtom(dpy, "_NET_WM_STRUT", False);
        stp = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
        XChangeProperty(dpy, win, st, XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *)strut, 4);
        XChangeProperty(dpy, win, stp, XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *)sp, 12);
    }
    gc = XCreateGC(dpy, win, 0, NULL);
    XSetFont(dpy, gc, font->fid);

    for (i = 0; i < 3; i++) {
        btns[i].label = labels[i];
        btns[i].icon = icons[i];
        btns[i].cmd = cmds[i];
        btns[i].y = ys[i];
        btns[i].hover = 0;
        btns[i].pressed = 0;
        btns[i].w = XCreateSimpleWindow(dpy, win, (DOCK_W - BTN) / 2, ys[i],
                                        BTN, BTN, 0,
                                        BlackPixel(dpy, scr), c_btn);
        XSelectInput(dpy, btns[i].w,
                     ExposureMask | ButtonPressMask | ButtonReleaseMask |
                     EnterWindowMask | LeaveWindowMask);
        XMapWindow(dpy, btns[i].w);
    }
    XSelectInput(dpy, win, ExposureMask);
    XMapWindow(dpy, win);

    xfd = ConnectionNumber(dpy);
    for (;;) {
        fd_set rfds;
        struct timeval tv;
        int animating = (anim_btn >= 0);
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == Expose) {
                draw_all();
            } else if (ev.type == EnterNotify) {
                for (i = 0; i < 3; i++)
                    if (ev.xcrossing.window == btns[i].w) {
                        btns[i].hover = 1;
                        draw_btn(&btns[i]);
                    }
            } else if (ev.type == LeaveNotify) {
                for (i = 0; i < 3; i++)
                    if (ev.xcrossing.window == btns[i].w) {
                        btns[i].hover = 0;
                        btns[i].pressed = 0;
                        draw_btn(&btns[i]);
                    }
            } else if (ev.type == ButtonPress) {
                for (i = 0; i < 3; i++)
                    if (ev.xbutton.window == btns[i].w) {
                        btns[i].pressed = 1;
                        draw_btn(&btns[i]);
                        XFlush(dpy);
                    }
            } else if (ev.type == ButtonRelease) {
                for (i = 0; i < 3; i++)
                    if (ev.xbutton.window == btns[i].w && btns[i].pressed) {
                        btns[i].pressed = 0;
                        draw_btn(&btns[i]);
                        launch(btns[i].cmd);
                        anim_btn = i;
                        anim_until = now_ms() + 350;
                    }
            }
        }
        if (anim_btn >= 0) {
            long left = anim_until - now_ms();
            if (left <= 0) {
                anim_btn = -1;
                draw_all();
            } else {
                draw_all();
                draw_ring((int)((350 - left) / 60));
            }
        } else {
            draw_all();
        }
        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        tv.tv_sec = animating || anim_btn >= 0 ? 0 : 1;
        tv.tv_usec = animating || anim_btn >= 0 ? 50000 : 0;
        select(xfd + 1, &rfds, NULL, NULL, &tv);
        (void)text_w;
    }
    return 0;
}
