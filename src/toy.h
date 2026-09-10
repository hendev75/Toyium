/* SPDX-License-Identifier: MIT */
/* toy.h - shared freestanding runtime for Toyium userland (no libc)
 *
 * syscall wrappers, string helpers, a bitmap graphics toolkit (framebuffer,
 * rects, 8x8 text, clipping) and the toywm client/server window protocol.
 *
 * made by xex & ayham
 */
#ifndef TOY_H
#define TOY_H

typedef unsigned long u64;
typedef unsigned int  u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long s64;
typedef long ssize_t;

/* ---- syscalls ---- */
#define SYS_read        0
#define SYS_write       1
#define SYS_open        2
#define SYS_close       3
#define SYS_poll        7
#define SYS_lseek       8
#define SYS_mmap        9
#define SYS_munmap      11
#define SYS_ioctl       16
#define SYS_dup2        33
#define SYS_nanosleep   35
#define SYS_getpid      39
#define SYS_socket      41
#define SYS_connect     42
#define SYS_accept      43
#define SYS_bind        49
#define SYS_listen      50
#define SYS_fork        57
#define SYS_execve      59
#define SYS_wait4       61
#define SYS_kill        62
#define SYS_uname       63
#define SYS_getcwd      79
#define SYS_chdir       80
#define SYS_mkdir       83
#define SYS_getppid     110
#define SYS_mount       165
#define SYS_reboot      169
#define SYS_getdents64  217
#define SYS_exit_group  231

#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0100
#define O_TRUNC     01000
#define O_DIRECTORY 00200000
#define O_NONBLOCK  04000

#define PROT_READ   1
#define PROT_WRITE  2
#define MAP_SHARED  1
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20

#define AF_UNIX     1
#define SOCK_STREAM 1

#define SYS_unlink 87

#define POLLIN      1

#define TCGETS      0x5401
#define TCSETS      0x5402
#define TIOCGWINSZ  0x5413
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

/* ---- syscall wrappers ---- */
static inline long sc0(long n) { long r; asm volatile("syscall" : "=a"(r) : "a"(n) : "rcx","r11","memory"); return r; }
static inline long sc1(long n, long a) { long r; asm volatile("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx","r11","memory"); return r; }
static inline long sc2(long n, long a, long b) { long r; asm volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b) : "rcx","r11","memory"); return r; }
static inline long sc3(long n, long a, long b, long c) { long r; asm volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx","r11","memory"); return r; }
static inline long sc4(long n, long a, long b, long c, long d) { long r; register long r10 asm("r10")=d; asm volatile("syscall" : "=a"(r) : "a"(n),"D"(a),"S"(b),"d"(c),"r"(r10) : "rcx","r11","memory"); return r; }
static inline long sc5(long n, long a, long b, long c, long d, long e) { long r; register long r10 asm("r10")=d; register long r8 asm("r8")=e; asm volatile("syscall" : "=a"(r) : "a"(n),"D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8) : "rcx","r11","memory"); return r; }
static inline long sc6(long n, long a, long b, long c, long d, long e, long f) { long r; register long r10 asm("r10")=d; register long r8 asm("r8")=e; register long r9 asm("r9")=f; asm volatile("syscall" : "=a"(r) : "a"(n),"D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8),"r"(r9) : "rcx","r11","memory"); return r; }

static long t_read(int fd, void *b, u64 n)  { return sc3(SYS_read, fd, (long)b, (long)n); }
static long t_write(int fd, const void *b, u64 n) { return sc3(SYS_write, fd, (long)b, (long)n); }
static long t_open(const char *p, long f)   { return sc2(SYS_open, (long)p, f); }
static long t_open3(const char *p, long f, long m) { return sc3(SYS_open, (long)p, f, m); }
static long t_close(long fd)                { return sc1(SYS_close, fd); }
static long t_ioctl(long fd, long r, void *a){ return sc3(SYS_ioctl, fd, r, (long)a); }
static void *t_mmap(void *a, u64 l, long p, long f, long fd, long o) { return (void *)sc6(SYS_mmap,(long)a,(long)l,p,f,fd,o); }
static long t_getpid(void)                  { return sc0(SYS_getpid); }
static long t_fork(void)                    { return sc0(SYS_fork); }
static long t_execve(const char *p, char **argv, char **envp) { return sc3(SYS_execve,(long)p,(long)argv,(long)envp); }
static long t_wait4(long pid, int *st, long opt, void *ru) { return sc4(SYS_wait4, pid, (long)st, opt, (long)ru); }
static long t_exit_group(long c)            { return sc1(SYS_exit_group, c); }
static long t_poll(void *pfds, long n, long to){ return sc3(SYS_poll,(long)pfds,n,to); }
static long t_socket(long d, long t, long p){ return sc3(SYS_socket,d,t,p); }
static long t_bind(long fd, void *a, long l) { return sc3(SYS_bind, fd, (long)a, l); }
static long t_listen(long fd, long n)       { return sc2(SYS_listen, fd, n); }
static long t_accept(long fd, void *a, void *l) { return sc3(SYS_accept, fd, (long)a, (long)l); }
static long t_connect(long fd, void *a, long l) { return sc3(SYS_connect, fd, (long)a, l); }
static long t_chdir(const char *p)          { return sc1(SYS_chdir, (long)p); }
static long t_getcwd(char *b, u64 n)        { return sc2(SYS_getcwd, (long)b, (long)n); }
static long t_mkdir(const char *p, long m)  { return sc2(SYS_mkdir, (long)p, m); }
static long t_unlink(const char *p)          { return sc1(SYS_unlink, (long)p); }
static long t_mount(const char *s, const char *t, const char *f, u64 fl, const void *d) { return sc5(SYS_mount,(long)s,(long)t,(long)f,(long)fl,(long)d); }

/* ---- mini string / io ---- */
static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static int scmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }
static void scpy(char *d, const char *s) { while ((*d++ = *s++)) {} }
static void puts_(const char *s) { t_write(1, s, slen(s)); }
static void putc_(char c) { t_write(1, &c, 1); }
static void putu_(u64 v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = '0' + (v % 10); v /= 10; } puts_(&b[i]); }

static int s_atoi(const char *s) { int v = 0, neg = 0; if (*s == '-') { neg = 1; s++; } while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; } return neg ? -v : v; }

/* ---- graphics toolkit ---- */
#include "font8x8.h"

struct fb {
    int fd;
    u8 *mem;
    u32 *px;
    int w, h, stride, bpp;   /* stride in bytes */
    u32 bytes;
};

static int fb_open(struct fb *fb, const char *dev) {
    u8 vi[256];
    long fd = t_open(dev, O_RDWR);
    if (fd < 0) return -1;
    for (int i = 0; i < 256; i++) vi[i] = 0;
    if (t_ioctl(fd, FBIOGET_VSCREENINFO, vi) != 0) { t_close(fd); return -1; }
    fb->fd = (int)fd;
    fb->w = *(u32 *)(vi + 0);
    fb->h = *(u32 *)(vi + 4);
    u32 xres_v = *(u32 *)(vi + 8);
    u32 yres_v = *(u32 *)(vi + 12);
    fb->bpp = *(u32 *)(vi + 24);
    if (xres_v < (u32)fb->w) xres_v = fb->w;
    if (yres_v < (u32)fb->h) yres_v = fb->h;
    fb->stride = (int)(xres_v * (fb->bpp / 8));
    fb->bytes = (u32)(fb->stride * (int)yres_v);
    fb->mem = (u8 *)t_mmap(0, fb->bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if ((long)fb->mem < 0 && (long)fb->mem > -4096) { t_close(fb->fd); return -1; }
    fb->px = (u32 *)fb->mem;
    return 0;
}

/* blocking full read/write (stream sockets may split messages) */
static long read_full(int fd, void *buf, u64 n) {
    u64 got = 0;
    while (got < n) {
        long r = t_read(fd, (u8 *)buf + got, n - got);
        if (r <= 0) return r;
        got += (u64)r;
    }
    return (long)got;
}

static long write_full(int fd, const void *buf, u64 n) {
    u64 sent = 0;
    while (sent < n) {
        long r = t_write(fd, (const u8 *)buf + sent, n - sent);
        if (r <= 0) return r;
        sent += (u64)r;
    }
    return (long)sent;
}

static inline u32 fb_pack(struct fb *fb, u32 rgb) {
    if (fb->bpp == 32) return rgb & 0x00FFFFFF;
    if (fb->bpp == 16) {
        u32 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
    return rgb & 0x00FFFFFF;
}

static inline void fb_px(struct fb *fb, int x, int y, u32 packed) {
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    u8 *p = fb->mem + (u64)y * fb->stride + (u64)x * (fb->bpp / 8);
    if (fb->bpp == 32)      *(u32 *)p = packed;
    else if (fb->bpp == 16) *(u16 *)p = (u16)packed;
    else { p[0] = packed & 0xFF; p[1] = (packed >> 8) & 0xFF; p[2] = (packed >> 16) & 0xFF; }
}

/* filled rect with clipping */
static void gfx_fill(struct fb *fb, int x, int y, int w, int h, u32 rgb) {
    u32 packed = fb_pack(fb, rgb);
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > fb->w ? fb->w : x + w;
    int y1 = y + h > fb->h ? fb->h : y + h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            fb_px(fb, xx, yy, packed);
}

/* 1px outline */
static void gfx_rect(struct fb *fb, int x, int y, int w, int h, u32 rgb) {
    gfx_fill(fb, x, y, w, 1, rgb);
    gfx_fill(fb, x, y + h - 1, w, 1, rgb);
    gfx_fill(fb, x, y, 1, h, rgb);
    gfx_fill(fb, x + w - 1, y, 1, h, rgb);
}

/* 8x8 text; transparent when bg < 0 */
static void gfx_text(struct fb *fb, int x, int y, u32 fg, int bg, const char *s) {
    u32 pfg = fb_pack(fb, fg);
    u32 pbg = bg >= 0 ? fb_pack(fb, (u32)bg) : 0;
    int drawbg = bg >= 0;
    int cx = x;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= 128) ch = '?';
        for (int row = 0; row < 8; row++) {
            unsigned char bits = font8x8[ch][row];
            for (int col = 0; col < 8; col++) {
                int on = (bits >> col) & 1;
                if (on) fb_px(fb, cx + col, y + row, pfg);
                else if (drawbg) fb_px(fb, cx + col, y + row, pbg);
            }
        }
        cx += 8;
    }
}

/* clipped text inside a rect (x,y,w,h) */
static void gfx_text_clip(struct fb *fb, int x, int y, int w, int h, u32 fg, int bg, const char *s) {
    u32 pfg = fb_pack(fb, fg);
    u32 pbg = bg >= 0 ? fb_pack(fb, (u32)bg) : 0;
    int drawbg = bg >= 0;
    int cx = x;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= 128) ch = '?';
        for (int row = 0; row < 8; row++) {
            unsigned char bits = font8x8[ch][row];
            int yy = y + row;
            if (yy < 0 || yy >= h) continue;
            for (int col = 0; col < 8; col++) {
                int xx = cx + col;
                if (xx < 0 || xx >= w) continue;
                int on = (bits >> col) & 1;
                if (on) fb_px(fb, xx, yy, pfg);
                else if (drawbg) fb_px(fb, xx, yy, pbg);
            }
        }
        cx += 8;
    }
}

/* ---- window protocol (toywm <-> client) ---- */
#define WIN_MSG_LEN 64
struct win_msg {
    int type;
    int x, y, w, h;
    u32 color;
    int len;
    char data[WIN_MSG_LEN];
};

#define WIN_CREATE   1   /* w,h, data=title */
#define WIN_CLEAR    2   /* color */
#define WIN_RECT     3   /* x,y,w,h,color */
#define WIN_TEXT     4   /* x,y,color, data=str */
#define WIN_FLUSH    5
#define WIN_DESTROY  6

#define WIN_EV_MOUSE 100 /* x,y, buttons in w */
#define WIN_EV_KEY   101 /* data[0] */
#define WIN_EV_EXPOSE 102
#define WIN_EV_CLOSE 103

struct sockaddr_un {
    u16 sun_family;
    char sun_path[108];
};

#define WM_SOCK "/run/toywm.sock"

#endif /* TOY_H */
