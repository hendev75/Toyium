/* Toyium OS — toyium-fetch
 * Minimal neofetch-like system info for TTY.
 * gcc -O2 -o toyium-fetch toyium-fetch.c  (or -static)
 * BusyBox ash will have it as /bin/toyium-fetch
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>

static void print_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char buf[512];
    if (fgets(buf, sizeof(buf), f)) {
        // strip newline
        buf[strcspn(buf, "\r\n")] = 0;
        printf("%s\n", buf);
    }
    fclose(f);
}

static void print_mem(void) {
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        unsigned long total_mb = si.totalram * si.mem_unit / (1024*1024);
        unsigned long free_mb  = si.freeram  * si.mem_unit / (1024*1024);
        unsigned long used_mb  = total_mb > free_mb ? total_mb - free_mb : 0;
        printf("Memory: %lu MB used / %lu MB total\n", used_mb, total_mb);
        printf("Uptime: %ld seconds\n", si.uptime);
        printf("Procs:  %d\n", si.procs);
    }
}

int main(void) {
    struct utsname u;
    uname(&u);

    // colors
    const char *C_RST = "\033[0m";
    const char *C_GRN = "\033[1;32m";
    const char *C_CYN = "\033[1;36m";
    const char *C_BLU = "\033[1;34m";
    const char *C_YEL = "\033[1;33m";

    printf("\n");
    printf("%s", C_GRN);
    printf("  _____           _\n");
    printf(" |_   _|__  _   _(_)_   _ _ __ ___\n");
    printf("   | |/ _ \\| | | | | | | | '_ ` _ \\\n");
    printf("   | | (_) | |_| | | |_| | | | | | |\n");
    printf("   |_|\\___/ \\__, |_|\\__,_|_| |_| |_|\n");
    printf("             |___/  Toyium OS\n");
    printf("%s", C_RST);

    printf("%s ─────────────────────────%s\n", C_BLU, C_RST);
    printf("%sOS:%s     Toyium OS 0.1.0 (x86_64 TTY)\n", C_CYN, C_RST);
    printf("%sKernel:%s %s %s\n", C_CYN, C_RST, u.sysname, u.release);
    printf("%sArch:%s   %s\n", C_CYN, C_RST, u.machine);
    printf("%sHost:%s   ", C_CYN, C_RST);
    print_file("/etc/hostname");
    printf("%sShell:%s  %s\n", C_CYN, C_RST, getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
    print_mem();
    printf("%sCPU:%s    ", C_CYN, C_RST);
    // try /proc/cpuinfo
    {
        FILE *f = fopen("/proc/cpuinfo", "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "model name", 10) == 0) {
                    char *p = strchr(line, ':');
                    if (p) {
                        p += 2;
                        p[strcspn(p, "\r\n")] = 0;
                        printf("%s\n", p);
                        break;
                    }
                }
            }
            fclose(f);
        } else {
            printf("(unknown)\n");
        }
    }
    printf("%sPackages:%s BusyBox\n", C_CYN, C_RST);
    printf("%sTTY:%s    %s\n", C_CYN, C_RST, ttyname(STDIN_FILENO) ? ttyname(STDIN_FILENO) : "unknown");
    printf("%s ─────────────────────────%s\n", C_BLU, C_RST);
    // palette
    printf("%s███%s%s███%s%s███%s%s███%s%s███%s%s███%s%s███%s%s███%s\n",
        "\033[40m", C_RST, "\033[41m", C_RST, "\033[42m", C_RST, "\033[43m", C_RST,
        "\033[44m", C_RST, "\033[45m", C_RST, "\033[46m", C_RST, "\033[47m", C_RST);
    printf("\n");
    // extra: /etc/os-release
    printf("%s%s%s\n", C_YEL, "--- /etc/os-release ---", C_RST);
    print_file("/etc/os-release");
    // cat full os-release
    FILE *f = fopen("/etc/os-release", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) printf("%s", line);
        fclose(f);
    }
    printf("\n");
    return 0;
}
