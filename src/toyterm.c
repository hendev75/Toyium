/* SPDX-License-Identifier: MIT */
/* toyterm - a terminal window (toywm client): prompt, line editing,
 * scrollback and a small set of built-in commands. */
#include "toy.h"

struct linux_dirent64 { u64 d_ino; s64 d_off; u16 d_reclen; u8 d_type; char d_name[]; };
#define DT_DIR 4

#define COLS 50
#define ROWS 20
#define MAXLINES 300
#define CW 320
#define CH (ROWS * 10 + 26)

static char scr[MAXLINES][COLS + 1];
static int  nlines = 0;
static char input[COLS + 1];
static int  inlen = 0;
static char cwd[128] = "/toy";
static int  wmfd = -1;
static int  cursor_on = 1;

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

static void addline(const char *s) {
    if (nlines >= MAXLINES) {
        for (int i = 0; i < MAXLINES - 1; i++)
            for (int j = 0; j <= COLS; j++) scr[i][j] = scr[i + 1][j];
        nlines--;
    }
    int i = 0; for (; s[i] && i < COLS; i++) scr[nlines][i] = s[i];
    scr[nlines][i] = 0; nlines++;
}

static void joinpath(char *out, const char *dir, const char *name) {
    int i = 0; while (dir[i]) { out[i] = dir[i]; i++; }
    if (i && out[i-1] != '/') out[i++] = '/';
    for (int j = 0; name[j] && i < 120; j++) out[i++] = name[j];
    out[i] = 0;
}

static void split(const char *s, char *a, char *b) {
    int i = 0; while (s[i] && s[i] != ' ') { a[i] = s[i]; i++; } a[i] = 0;
    while (s[i] == ' ') i++;
    int j = 0; while (s[i]) b[j++] = s[i++]; b[j] = 0;
}

static void cmd_ls(const char *dir) {
    const char *d = dir[0] ? dir : cwd;
    int fd = (int)t_open(d, O_RDONLY | O_DIRECTORY);
    if (fd < 0) { addline("ls: cannot open"); return; }
    char buf[4096], line[COLS + 1]; int n = 0; line[0] = 0;
    for (;;) {
        long r = sc3(SYS_getdents64, fd, (long)buf, (long)sizeof buf);
        if (r <= 0) break;
        u64 off = 0;
        while (off < (u64)r) {
            struct linux_dirent64 *de = (struct linux_dirent64 *)(buf + off);
            if (de->d_name[0] != '.') {
                const char *nm = de->d_name; int l = 0; while (nm[l]) l++;
                if (n + l + 2 > COLS) { addline(line); n = 0; line[0] = 0; }
                for (int k = 0; k < l; k++) line[n++] = nm[k];
                if (de->d_type == DT_DIR) line[n++] = '/';
                line[n++] = ' '; line[n] = 0;
            }
            off += de->d_reclen;
        }
    }
    if (n) addline(line);
    t_close(fd);
}

static void cmd_cat(const char *file) {
    char full[160];
    if (file[0] == '/') { int i = 0; while (file[i]) { full[i] = file[i]; i++; } full[i] = 0; }
    else joinpath(full, cwd, file);
    int fd = (int)t_open(full, O_RDONLY);
    if (fd < 0) { addline("cat: no such file"); return; }
    char buf[1024], line[COLS + 1]; int n = 0; line[0] = 0;
    long r;
    while ((r = t_read(fd, buf, sizeof buf)) > 0) {
        for (long i = 0; i < r; i++) {
            char c = buf[i];
            if (c == '\n') { addline(line); n = 0; line[0] = 0; }
            else if (c != '\r') { if (n < COLS) { line[n++] = c; line[n] = 0; } }
        }
    }
    if (n) addline(line);
    t_close(fd);
}

static void show_help(void) {
    addline("toyterm - Toyium terminal");
    addline("commands: ls, cd, pwd, cat, echo, clear,");
    addline("          help, about, exit");
}

static void execute(const char *cmdline) {
    char cmd[64], arg[128];
    split(cmdline, cmd, arg);
    if (!cmd[0]) return;
    if (scmp(cmd, "ls") == 0) cmd_ls(arg);
    else if (scmp(cmd, "cd") == 0) {
        char full[160];
        if (arg[0] == '/') { int i = 0; while (arg[i]) { full[i] = arg[i]; i++; } full[i] = 0; }
        else if (arg[0]) joinpath(full, cwd, arg);
        else { int i = 0; const char *h = "/toy"; while (h[i]) { full[i] = h[i]; i++; } full[i] = 0; }
        int fd = (int)t_open(full, O_RDONLY | O_DIRECTORY);
        if (fd < 0) addline("cd: no such directory");
        else { t_close(fd); int i = 0; while (full[i]) { cwd[i] = full[i]; i++; } cwd[i] = 0; }
    }
    else if (scmp(cmd, "pwd") == 0) addline(cwd);
    else if (scmp(cmd, "cat") == 0) cmd_cat(arg);
    else if (scmp(cmd, "echo") == 0) addline(arg);
    else if (scmp(cmd, "clear") == 0) nlines = 0;
    else if (scmp(cmd, "help") == 0) show_help();
    else if (scmp(cmd, "about") == 0) {
        addline("Toyium OS - made by xex & ayham");
        addline("a window manager + terminal from scratch");
    }
    else if (scmp(cmd, "exit") == 0) { struct win_msg m; m.type = WIN_DESTROY; send(&m); t_exit_group(0); }
    else addline("command not found (try help)");
}

static void prompt(char *out) {
    int i = 0; const char *a = "toyium ";
    while (a[i]) { out[i] = a[i]; i++; }
    for (int j = 0; cwd[j] && i < 60; j++) out[i++] = cwd[j];
    const char *b = "> "; int j = 0; while (b[j]) out[i++] = b[j++];
    out[i] = 0;
}

static void draw(void) {
    struct win_msg m; m.type = WIN_CLEAR; m.color = 0x0A0E14; send(&m);
    rect(0, 0, CW, 18, 0x1E2A44);
    txt(6, 5, 0x9CD0FF, "Terminal");

    int start = nlines > ROWS - 2 ? nlines - (ROWS - 2) : 0;
    for (int i = start; i < nlines; i++) txt(6, 22 + (i - start) * 10, 0xC8D2E0, scr[i]);

    char pr[80]; prompt(pr);
    int py = 22 + (nlines - start) * 10;
    if (py > ROWS * 10 + 10) py = ROWS * 10 + 10;
    txt(6, py, 0x7CE38B, pr);
    txt(6 + (int)slen(pr) * 8, py, 0xE8F0FF, input);
    if (cursor_on) rect(6 + ((int)slen(pr) + inlen) * 8, py - 1, 8, 10, 0x9CD0FF);

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
    if (wmfd < 0) { puts_("toyterm: no toywm\n"); return 1; }

    struct win_msg m; m.type = WIN_CREATE; m.x = m.y = 0; m.w = CW; m.h = CH; m.color = 0; m.len = 0;
    const char *t = "Terminal"; int i = 0; for (; t[i]; i++) m.data[i] = t[i]; m.data[i] = 0; m.len = i;
    send(&m);

    addline("Toyium terminal - type 'help'");
    for (;;) {
        draw();
        struct pollfd { int fd; short events; short revents; } pf;
        pf.fd = wmfd; pf.events = POLLIN; pf.revents = 0;
        long pr = t_poll(&pf, 1, 500);
        cursor_on = !cursor_on;
        if (pr > 0 && (pf.revents & POLLIN)) {
            struct win_msg ev;
            if (read_full(wmfd, &ev, sizeof ev) <= 0) break;
            if (ev.type == WIN_EV_CLOSE) break;
            else if (ev.type == WIN_EV_KEY) {
                unsigned char c = (unsigned char)ev.data[0];
                cursor_on = 1;
                if (c == '\r' || c == '\n') {
                    char pr[80]; prompt(pr);
                    char echo[COLS + 80]; int k = 0;
                    for (int j = 0; pr[j]; j++) echo[k++] = pr[j];
                    for (int j = 0; j < inlen; j++) echo[k++] = input[j];
                    echo[k] = 0; addline(echo);
                    execute(input);
                    inlen = 0; input[0] = 0;
                } else if (c == 0x7f || c == 0x08) {
                    if (inlen > 0) input[--inlen] = 0;
                } else if (c == 0x03) {
                    inlen = 0; input[0] = 0;
                    addline("^C");
                } else if (c >= 0x20 && c < 0x7f) {
                    if (inlen < COLS) { input[inlen++] = (char)c; input[inlen] = 0; }
                }
            }
        }
    }
    t_close(wmfd);
    return 0;
}
