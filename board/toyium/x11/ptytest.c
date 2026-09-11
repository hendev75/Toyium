#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>

int main(void)
{
    int fd, s, t, rc;
    char *name;
    pid_t pid;

    fd = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    printf("open /dev/ptmx = %d errno=%d (%s)\n", fd, errno, strerror(errno));
    if (fd < 0)
        return 1;

    grantpt(fd);
    unlockpt(fd);
    name = ptsname(fd);
    printf("ptsname=%s\n", name ? name : "(null)");

    pid = fork();
    if (pid == 0) {
        rc = setsid();
        printf("child: setsid = %d errno=%d (%s)\n", rc, errno, strerror(errno));

        errno = 0;
        s = open(name, O_RDWR);
        printf("child: open slave %s = %d errno=%d (%s)\n", name, s, errno, strerror(errno));

        errno = 0;
        t = open("/dev/tty", O_RDWR);
        printf("child: open /dev/tty = %d errno=%d (%s)\n", t, errno, strerror(errno));

        if (t >= 0)
            close(t);
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    printf("parent: done\n");
    return 0;
}
