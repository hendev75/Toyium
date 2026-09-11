/* toycalc - Toyium calculator (Xlib, no toolkit).
 *
 * Mouse + keyboard. Buttons: C(clear) B(backspace) % / 7 8 9 * 4 5 6 -
 * 1 2 3 + 0 . =
 * Keyboard: 0-9 . , + - * / % Enter(=) Esc(C) BackSpace.
 *
 * made by xex & ayham
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIN_W 260
#define WIN_H 380
#define MARGIN 10
#define GAP 6
#define DISP_H 60
#define NCOL 4
#define NROW 5

static Display *dpy;
static Window win;
static GC gc;
static XFontStruct *font;
static XFontStruct *bigfont;
static int pressed_btn = -1;
static int scr;
static unsigned long c_bg, c_fg, c_btn, c_op, c_disp, c_dispfg, c_err,
    c_eq, c_clr;

static char entry[64] = "0";
static double acc = 0.0;
static char pending = 0;
static int fresh = 1;
static int err = 0;

struct btn { const char *label; int r, c, cs; const char *key; };
static struct btn buttons[] = {
    { "C", 0, 0, 1, "c" }, { "B", 0, 1, 1, "b" },
    { "%", 0, 2, 1, "%" }, { "/", 0, 3, 1, "/" },
    { "7", 1, 0, 1, "7" }, { "8", 1, 1, 1, "8" },
    { "9", 1, 2, 1, "9" }, { "*", 1, 3, 1, "*" },
    { "4", 2, 0, 1, "4" }, { "5", 2, 1, 1, "5" },
    { "6", 2, 2, 1, "6" }, { "-", 2, 3, 1, "-" },
    { "1", 3, 0, 1, "1" }, { "2", 3, 1, 1, "2" },
    { "3", 3, 2, 1, "3" }, { "+", 3, 3, 1, "+" },
    { "0", 4, 0, 2, "0" }, { ".", 4, 2, 1, "." },
    { "=", 4, 3, 1, "=" },
};
#define NBTN (sizeof(buttons) / sizeof(buttons[0]))

static int bw, bh, gx, gy;

static void show(double v)
{
    snprintf(entry, sizeof(entry), "%.10g", v);
}

static double apply(double a, double b, char op)
{
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b == 0.0 ? 0.0 / 0.0 : a / b;
    case '%': return b == 0.0 ? 0.0 / 0.0 : (double)((long long)a % (long long)b);
    default: return b;
    }
}

static int is_bad(double v)
{
    return v != v;
}

static void press(const char *k)
{
    if (err && k[0] != 'C' && k[0] != 'c') {
        entry[0] = '\0';
        err = 0;
        fresh = 1;
        acc = 0.0;
        pending = 0;
    }
    if ((k[0] >= '0' && k[0] <= '9' && k[1] == '\0') || k[0] == '.') {
        if (fresh) {
            entry[0] = '\0';
            fresh = 0;
        }
        if (k[0] == '.' && strchr(entry, '.'))
            return;
        if (strlen(entry) < sizeof(entry) - 2) {
            if (entry[0] == '\0' && k[0] == '.')
                strcpy(entry, "0");
            strncat(entry, k, 1);
        }
        return;
    }
    if (k[0] == 'C' || k[0] == 'c') {
        strcpy(entry, "0");
        acc = 0.0;
        pending = 0;
        fresh = 1;
        err = 0;
        return;
    }
    if (k[0] == 'B' || k[0] == 'b') {
        size_t n = strlen(entry);
        if (!fresh && n > 0) {
            entry[n - 1] = '\0';
            if (entry[0] == '\0' || (entry[0] == '-' && entry[1] == '\0'))
                strcpy(entry, "0"), fresh = 1;
        }
        return;
    }
    if (k[0] == '+' || k[0] == '-' || k[0] == '*' || k[0] == '/' || k[0] == '%') {
        double v = atof(entry);
        if (pending && !fresh) {
            acc = apply(acc, v, pending);
            if (is_bad(acc)) {
                err = 1;
                strcpy(entry, "Error");
                pending = 0;
                fresh = 1;
                return;
            }
        } else {
            acc = v;
        }
        pending = k[0];
        fresh = 1;
        return;
    }
    if (k[0] == '=') {
        if (pending) {
            acc = apply(acc, atof(entry), pending);
            if (is_bad(acc)) {
                err = 1;
                strcpy(entry, "Error");
            } else {
                show(acc);
            }
            pending = 0;
            fresh = 1;
        }
        return;
    }
}

static void btn_geom(int i, int *x, int *y, int *w, int *h)
{
    *x = gx + buttons[i].c * (bw + GAP);
    *y = gy + buttons[i].r * (bh + GAP);
    *w = buttons[i].cs * bw + (buttons[i].cs - 1) * GAP;
    *h = bh;
}

static void draw_centered_f(XFontStruct *f, int x, int y, int w, int h,
                            const char *s, unsigned long fg)
{
    int dir, asc, desc;
    XCharStruct overall;
    XTextExtents(f, s, strlen(s), &dir, &asc, &desc, &overall);
    int tw = overall.rbearing - overall.lbearing;
    XSetFont(dpy, gc, f->fid);
    XSetForeground(dpy, gc, fg);
    XDrawString(dpy, win, gc, x + (w - tw) / 2 - overall.lbearing,
                y + (h + asc - desc) / 2, s, strlen(s));
}

static void draw_centered(int x, int y, int w, int h, const char *s,
                          unsigned long fg)
{
    draw_centered_f(font, x, y, w, h, s, fg);
}

static void draw(void)
{
    unsigned i;
    int x, y, w, h;
    XSetForeground(dpy, gc, c_bg);
    XFillRectangle(dpy, win, gc, 0, 0, WIN_W, WIN_H);
    /* display */
    XSetForeground(dpy, gc, c_disp);
    XFillRectangle(dpy, win, gc, MARGIN, MARGIN, WIN_W - 2 * MARGIN, DISP_H);
    XSetForeground(dpy, gc, c_btn);
    XDrawRectangle(dpy, win, gc, MARGIN, MARGIN, WIN_W - 2 * MARGIN - 1,
                   DISP_H - 1);
    draw_centered_f(bigfont ? bigfont : font, MARGIN + 4, MARGIN,
                    WIN_W - 2 * MARGIN - 8, DISP_H,
                    entry, err ? c_err : c_dispfg);
    for (i = 0; i < NBTN; i++) {
        int is_op = (buttons[i].c == NCOL - 1) || i < 4;
        int is_eq = buttons[i].label[0] == '=';
        int is_c = buttons[i].label[0] == 'C';
        unsigned long bg = is_op ? c_op : c_btn;
        unsigned long fg = c_fg;
        btn_geom(i, &x, &y, &w, &h);
        if (is_eq)
            bg = c_eq;
        else if (is_c)
            bg = c_clr;
        if ((int)i == pressed_btn) {
            fg = bg;
            bg = c_fg;
        }
        XSetForeground(dpy, gc, bg);
        XFillRectangle(dpy, win, gc, x, y, w, h);
        XSetForeground(dpy, gc, c_bg);
        XDrawRectangle(dpy, win, gc, x, y, w - 1, h - 1);
        draw_centered(x, y, w, h, buttons[i].label, fg);
    }
}

static int at_button(int px, int py)
{
    unsigned i;
    int x, y, w, h;
    for (i = 0; i < NBTN; i++) {
        btn_geom(i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h)
            return (int)i;
    }
    return -1;
}

static const char *key_to_btn(KeySym k)
{
    if (k >= XK_0 && k <= XK_9) {
        static char s[2];
        s[0] = (char)('0' + (k - XK_0));
        s[1] = '\0';
        return s;
    }
    if (k >= XK_KP_0 && k <= XK_KP_9) {
        static char s[2];
        s[0] = (char)('0' + (k - XK_KP_0));
        s[1] = '\0';
        return s;
    }
    switch (k) {
    case XK_plus: case XK_KP_Add: return "+";
    case XK_minus: case XK_KP_Subtract: return "-";
    case XK_asterisk: case XK_KP_Multiply: return "*";
    case XK_slash: case XK_KP_Divide: return "/";
    case XK_percent: return "%";
    case XK_period: case XK_comma: case XK_KP_Decimal: return ".";
    case XK_equal: case XK_Return: case XK_KP_Enter: return "=";
    case XK_Escape: return "C";
    case XK_BackSpace: return "B";
    case XK_c: case XK_C: return "C";
    default: return NULL;
    }
}

int main(void)
{
    XEvent ev;
    Atom del;
    const char *fonts[] = { "fixed", "9x15", "6x13", NULL };
    int i;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "toycalc: cannot open display\n");
        return 1;
    }
    scr = DefaultScreen(dpy);
    c_bg = 0x101828;
    c_fg = 0xFFFFFF;
    c_btn = 0x2A4A73;
    c_op = 0x1E3A5F;
    c_disp = 0x0B1220;
    c_dispfg = 0xE0F0FF;
    c_err = 0xFF6060;
    {
        XColor xc;
        Colormap cm = DefaultColormap(dpy, scr);
        unsigned long *slots[] = { &c_bg, &c_fg, &c_btn, &c_op,
                                   &c_disp, &c_dispfg, &c_err,
                                   &c_eq, &c_clr };
        unsigned vals[] = { 0x101828, 0xFFFFFF, 0x2A4A73, 0x1E3A5F,
                            0x0B1220, 0xE0F0FF, 0xFF6060,
                            0x2A7A3A, 0x7A2020 };
        for (i = 0; i < 9; i++) {
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
        fprintf(stderr, "toycalc: no font\n");
        return 1;
    }
    {
        const char *bigfonts[] = {
            "-misc-fixed-medium-r-normal--20-200-75-75-c-100-iso8859-1",
            "10x20",
            "9x15",
            NULL
        };
        bigfont = NULL;
        for (i = 0; bigfonts[i]; i++) {
            bigfont = XLoadQueryFont(dpy, bigfonts[i]);
            if (bigfont)
                break;
        }
    }

    bw = (WIN_W - 2 * MARGIN - (NCOL - 1) * GAP) / NCOL;
    bh = (WIN_H - 2 * MARGIN - DISP_H - GAP - (NROW - 1) * GAP) / NROW;
    gx = MARGIN;
    gy = MARGIN + DISP_H + GAP;

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 200, 120,
                              WIN_W, WIN_H, 1,
                              WhitePixel(dpy, scr), c_bg);
    XStoreName(dpy, win, "Toyium Calc");
    {
        XClassHint ch;
        ch.res_name = "toycalc";
        ch.res_class = "ToyCalc";
        XSetClassHint(dpy, win, &ch);
    }
    del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &del, 1);
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | KeyPressMask);
    gc = XCreateGC(dpy, win, 0, NULL);
    XSetFont(dpy, gc, font->fid);
    XMapWindow(dpy, win);

    for (;;) {
        XNextEvent(dpy, &ev);
        if (ev.type == Expose && ev.xexpose.count == 0) {
            draw();
        } else if (ev.type == ButtonPress) {
            int b = at_button(ev.xbutton.x, ev.xbutton.y);
            if (b >= 0) {
                pressed_btn = b;
                draw();
                XFlush(dpy);
                usleep(70000);
                press(buttons[b].key);
                pressed_btn = -1;
                draw();
            }
        } else if (ev.type == KeyPress) {
            const char *k = key_to_btn(XLookupKeysym(&ev.xkey, 0));
            if (k) {
                press(k);
                draw();
            }
        } else if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == del)
                break;
        }
    }
    XCloseDisplay(dpy);
    return 0;
}
