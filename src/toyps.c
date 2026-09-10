/* SPDX-License-Identifier: MIT */
/* toyps - list processes from /proc */
#include "toy.h"

struct linux_dirent64 { u64 d_ino; s64 d_off; u16 d_reclen; u8 d_type; char d_name[]; };

static int isnum(const char *s) { if (!*s) return 0; while (*s) { if (*s < '0' || *s > '9') return 0; s++; } return 1; }

int _start(void) {
    int fd = (int)t_open("/proc", O_RDONLY | O_DIRECTORY);
    if (fd < 0) { puts_("toyps: no /proc\n"); return 1; }
    static char buf[4096];
    puts_("  PID  COMMAND\n");
    for (;;) {
        long n = sc3(SYS_getdents64, fd, (long)buf, (long)sizeof buf);
        if (n <= 0) break;
        u64 off = 0;
        while (off < (u64)n) {
            struct linux_dirent64 *de = (struct linux_dirent64 *)(buf + off);
            if (isnum(de->d_name)) {
                char path[64]; int i = 0;
                const char *a = "/proc/"; while (*a) path[i++] = *a++;
                const char *nm = de->d_name; while (*nm) path[i++] = *nm++;
                const char *c = "/comm"; while (*c) path[i++] = *c++;
                path[i] = 0;
                char comm[64]; long r = 0;
                int cf = (int)t_open(path, O_RDONLY);
                if (cf >= 0) { r = t_read(cf, comm, 63); t_close(cf); }
                if (r < 0) r = 0; comm[r] = 0;
                for (long q = 0; q < r; q++) if (comm[q] == '\n') comm[q] = 0;
                puts_("  "); puts_(de->d_name); puts_("  "); puts_(comm); putc_('\n');
            }
            off += de->d_reclen;
        }
    }
    t_close(fd);
    return 0;
}
