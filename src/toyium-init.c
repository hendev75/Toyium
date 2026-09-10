/* Toyium OS — toyium-init.c
 * Alternative C init (PID 1) — lighter than shell /init.
 * Compile: gcc -static -O2 -o toyium-init toyium-init.c
 * Use by setting rdinit=/toyium-init or replacing /init.
 *
 * This is an example. The default Toyium build uses overlay/init (shell).
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <string.h>

static void do_mount(const char *src, const char *tgt, const char *fstype) {
    if (mount(src, tgt, fstype, 0, NULL) == 0) {
        printf("[toyium-init] mounted %s -> %s (%s)\n", src, tgt, fstype);
    } else {
        perror(tgt);
    }
}

int main(void) {
    printf("\n[toyium-init] Toyium OS C init v0.1.0 (PID 1)\n");

    do_mount("proc", "/proc", "proc");
    do_mount("sysfs", "/sys", "sysfs");
    if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) != 0) {
        perror("devtmpfs");
    }
    mkdir("/dev/pts", 0755);
    mkdir("/dev/shm", 0755);
    mkdir("/tmp", 0755);
    mkdir("/run", 0755);
    do_mount("devpts", "/dev/pts", "devpts");
    do_mount("tmpfs", "/tmp", "tmpfs");
    do_mount("tmpfs", "/run", "tmpfs");

    // hostname
    sethostname("toyium", 6);

    // banner
    FILE *f = fopen("/etc/motd", "r");
    if (f) { char buf[1024]; while (fgets(buf, sizeof(buf), f)) fputs(buf, stdout); fclose(f); }
    printf("\n[toyium-init] exec /bin/sh\n");

    char *argv[] = { "/bin/sh", "-l", NULL };
    char *envp[] = { "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
                     "HOME=/root", "TERM=linux", "PS1=toyium@toyium:\\w$ ", NULL };
    execve("/bin/sh", argv, envp);
    perror("execve /bin/sh");
    // fallback
    execve("/bin/busybox", (char*[]){ "busybox", "sh", "-l", NULL }, envp);
    perror("execve busybox");
    return 1;
}
