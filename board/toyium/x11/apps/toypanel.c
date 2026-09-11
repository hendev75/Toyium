/* toypanel - Toyium top taskbar (Xlib, no toolkit).
 *
 * Full-width bar: Toyium launcher buttons on the left, network status
 * (WLAN / wired) and clock on the right. Registers as a DOCK window
 * with a top strut so Openbox keeps space for it.
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
#include <sys/select.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PANEL_H 30

static Display *dpy;
static Window win, b_logo, b_term, b_calc;
static GC gc;
static XFontStruct *font;
static int scr, sW;
static unsigned long c_bg, c_fg, c_btn, c_acc;

struct btn {
    Window w;
    const char *label;
    const char *cmd;
    int x, width;
};

static struct btn btns[3];

static void net_status(char *out, size_t cap)
{
    DIR *d;
    struct dirent *e;
    char best_name[32] = "";
    char best_ip[64] = "";
    int best_wlan = 0, best_up = 0;
    struct ifaddrs *ifa, *p;

    d = opendir("/sys/class/net");
    if (d) {
        while ((e = readdir(d)) != NULL) {
            char path[128], state[16];
            FILE *f;
            int up = 0, wlan = 0;
            if (e->d_name[0] == '.')
                continue;
            if (strcmp(e->d_name, "lo") == 0)
                continue;
            snprintf(path, sizeof(path), "/sys/class/net/%s/operstate",
                     e->d_name);
            f = fopen(path, "r");
            if (f) {
                if (fgets(state, sizeof(state), f))
                    up = (strncmp(state, "up", 2) == 0);
                fclose(f);
            }
            snprintf(path, sizeof(path), "/sys/class/net/%s/wireless",
                     e->d_name);
            {
                DIR *w = opendir(path);
                if (w) {
                    wlan = 1;
                    closedir(w);
                }
            }
            if (!best_up && up) {
                strncpy(best_name, e->d_name, sizeof(best_name) - 1);
                best_name[sizeof(best_name) - 1] = '\0';
                best_wlan = wlan;
                best_up = 1;
                if (wlan)
                    break;
            } else if (!best_up && best_name[0] == '\0') {
                strncpy(best_name, e->d_name, sizeof(best_name) - 1);
                best_name[sizeof(best_name) - 1] = '\0';
                best_wlan = wlan;
            }
        }
        closedir(d);
    }

    if (best_up && getifaddrs(&ifa) == 0) {
        for (p = ifa; p; p = p->ifa_next) {
            struct sockaddr_in *a;
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
                continue;
            if (strcmp(p->ifa_name, best_name) != 0)
                continue;
            a = (struct sockaddr_in *)p->ifa_addr;
            if ((a->sin_addr.s_addr & 0xFF) == 127)
                continue;
            inet_ntop(AF_INET, &a->sin_addr, best_ip, sizeof(best_ip));
            break;
        }
        freeifaddrs(ifa);
    }

    if (best_up)
        snprintf(out, cap, "%s %s UP %s",
                 best_wlan ? "WLAN" : "ETH", best_name, best_ip);
    else if (best_name[0])
        snprintf(out, cap, "ETH %s DOWN", best_name);
    else
        snprintf(out, cap, "NET DOWN");
}

static void draw_btn(struct btn *b)
{
    int dir, asc, desc, tw;
    XCharStruct overall;
    XSetForeground(dpy, gc, c_btn);
    XFillRectangle(dpy, b->w, gc, 0, 0, b->width, PANEL_H - 6);
    XTextExtents(font, b->label, strlen(b->label), &dir, &asc, &desc,
                 &overall);
    tw = overall.rbearing - overall.lbearing;
    XSetForeground(dpy, gc, c_fg);
    XDrawString(dpy, b->w, gc,
                (b->width - tw) / 2 - overall.lbearing,
                (PANEL_H - 6 + asc - desc) / 2,
                b->label, strlen(b->label));
}

static void draw_all(void)
{
    char net[128], clk[64], right[256];
    time_t t;
    struct tm *tm;
    int dir, asc, desc, tw, i;
    XCharStruct overall;

    XSetForeground(dpy, gc, c_bg);
    XFillRectangle(dpy, win, gc, 0, 0, sW, PANEL_H);
    for (i = 0; i < 3; i++)
        draw_btn(&btns[i]);

    net_status(net, sizeof(net));
    t = time(NULL);
    tm = localtime(&t);
    strftime(clk, sizeof(clk), "%Y-%m-%d %H:%M:%S", tm);
    snprintf(right, sizeof(right), "%s   %s", net, clk);
    XSetForeground(dpy, gc, c_acc);
    XTextExtents(font, right, strlen(right), &dir, &asc, &desc, &overall);
    tw = overall.rbearing - overall.lbearing;
    XDrawString(dpy, win, gc, sW - 8 - tw - overall.lbearing,
                (PANEL_H + asc - desc) / 2, right, strlen(right));
}

static void launch(const char *cmd)
{
    if (fork() == 0) {
        setsid();
        execlp(cmd, cmd, (char *)NULL);
        _exit(127);
    }
}

int main(void)
{
    const char *fonts[] = { "fixed", "9x15", "6x13", NULL };
    const char *labels[] = { "Toyium", "Term", "Calc" };
    const char *cmds[] = { "xterm", "xterm", "toycalc" };
    const int widths[] = { 84, 64, 64 };
    int i, x;
    int xfd;
    Atom wt, dock, st, stp;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "toypanel: cannot open display\n");
        return 1;
    }
    scr = DefaultScreen(dpy);
    sW = DisplayWidth(dpy, scr);
    {
        XColor xc;
        Colormap cm = DefaultColormap(dpy, scr);
        unsigned long *slots[] = { &c_bg, &c_fg, &c_btn, &c_acc };
        unsigned vals[] = { 0x1E3A5F, 0xE0F0FF, 0x2A4A73, 0x9CD0FF };
        for (i = 0; i < 4; i++) {
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

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, sW, PANEL_H,
                              0, BlackPixel(dpy, scr), c_bg);
    XStoreName(dpy, win, "Toyium Panel");
    {
        XClassHint ch;
        ch.res_name = "toypanel";
        ch.res_class = "ToyPanel";
        XSetClassHint(dpy, win, &ch);
    }
    /* dock + top strut */
    wt = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(dpy, win, wt, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&dock, 1);
    {
        long strut[4] = { 0, 0, PANEL_H, 0 };
        long sp[12] = { 0, 0, PANEL_H, 0, 0, 0, 0, 0, 0, 0, sW - 1, 0 };
        st = XInternAtom(dpy, "_NET_WM_STRUT", False);
        stp = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
        XChangeProperty(dpy, win, st, XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *)strut, 4);
        XChangeProperty(dpy, win, stp, XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *)sp, 12);
    }
    gc = XCreateGC(dpy, win, 0, NULL);
    XSetFont(dpy, gc, font->fid);

    x = 6;
    for (i = 0; i < 3; i++) {
        btns[i].label = labels[i];
        btns[i].cmd = cmds[i];
        btns[i].x = x;
        btns[i].width = widths[i];
        btns[i].w = XCreateSimpleWindow(dpy, win, x, 3, widths[i],
                                        PANEL_H - 6, 0,
                                        BlackPixel(dpy, scr), c_btn);
        XSelectInput(dpy, btns[i].w, ExposureMask | ButtonPressMask);
        XMapWindow(dpy, btns[i].w);
        x += widths[i] + 6;
    }
    XSelectInput(dpy, win, ExposureMask);
    XMapWindow(dpy, win);

    xfd = ConnectionNumber(dpy);
    for (;;) {
        fd_set rfds;
        struct timeval tv;
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == Expose) {
                draw_all();
            } else if (ev.type == ButtonPress) {
                for (i = 0; i < 3; i++) {
                    if (ev.xbutton.window == btns[i].w) {
                        launch(btns[i].cmd);
                        break;
                    }
                }
            }
        }
        draw_all();
        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        select(xfd + 1, &rfds, NULL, NULL, &tv);
    }
    return 0;
}
