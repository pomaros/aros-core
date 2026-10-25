/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: TimesTest DIR/A - futimens(), utimensat() and utimes() on a real
          DOS volume, read back with stat()

    Creates DIR/times.test, sets and reads back its modification time in
    several ways, and removes it. Each check prints one line ending in PASS
    or FAIL; the last line is "TimesTest: result=PASSED" (0) or
    "TimesTest: result=FAIL" (10).
*/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static int failures;

static void check(int ok, const char *what)
{
    printf("TimesTest: %s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

static int mtime_is(const char *path, time_t sec, long nsec)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;
    printf("TimesTest:   %s mtime %lld.%09ld\n", path, (long long)st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
    return st.st_mtim.tv_sec == sec && st.st_mtim.tv_nsec == nsec;
}

int main(int argc, char **argv)
{
    char path[256];
    struct timespec ts[2];
    struct timeval tv[2];
    int fd;

    if (argc < 2)
    {
        printf("usage: TimesTest DIR\n");
        return 20;
    }
    {
        size_t n = strlen(argv[1]);
        const char *sep = (n > 0 && (argv[1][n - 1] == ':' || argv[1][n - 1] == '/')) ? "" : "/";

        snprintf(path, sizeof(path), "%s%stimes.test", argv[1], sep);
    }
    fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    check(fd >= 0 && write(fd, "times\n", 6) == 6, "create the file");

    /* 2024-02-29 12:34:56.5 UTC: ticks keep the half second. */
    ts[0].tv_sec = 0; ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = 1709210096; ts[1].tv_nsec = 500000000;
    check(utimensat(AT_FDCWD, path, ts, 0) == 0, "utimensat");
    check(mtime_is(path, 1709210096, 500000000), "stat reads back the same second and tick");

    ts[1].tv_sec = 1709210096 + 3600; ts[1].tv_nsec = 999999999;
    check(futimens(fd, ts) == 0, "futimens on the open file");
    check(mtime_is(path, 1709210096 + 3600, 980000000), "nanoseconds round down to the tick");

    ts[1].tv_nsec = UTIME_OMIT;
    check(futimens(fd, ts) == 0 && mtime_is(path, 1709210096 + 3600, 980000000), "UTIME_OMIT leaves it");
    close(fd);

    tv[0].tv_sec = 0; tv[0].tv_usec = 0;
    tv[1].tv_sec = 1500000000; tv[1].tv_usec = 260000;    /* 13 ticks */
    check(utimes(path, tv) == 0 && mtime_is(path, 1500000000, 260000000), "utimes keeps the microseconds");
    tv[1].tv_usec = 250000;                                  /* 12.5 ticks */
    check(utimes(path, tv) == 0 && mtime_is(path, 1500000000, 240000000), "microseconds round down to the tick");

    ts[1].tv_sec = 1709210096; ts[1].tv_nsec = 1000000000;
    errno = 0;
    check(utimensat(AT_FDCWD, path, ts, 0) == -1 && errno == EINVAL, "invalid nanoseconds refused");
    ts[1].tv_sec = 100; ts[1].tv_nsec = 0;
    errno = 0;
    check(utimensat(AT_FDCWD, path, ts, 0) == -1 && errno == EINVAL, "a date before 1978 refused");
    errno = 0;
    check(utimensat(AT_FDCWD, "RAM:no/such/file", NULL, 0) == -1 && errno == ENOENT, "missing file is ENOENT");
    check(futimens(-1, NULL) == -1 && errno == EBADF, "bad descriptor is EBADF");

    check(utimensat(AT_FDCWD, path, NULL, 0) == 0, "NULL means now");
    check(unlink(path) == 0, "remove the file");

    printf("TimesTest: result=%s\n", failures ? "FAIL" : "PASSED");
    return failures ? 10 : 0;
}
