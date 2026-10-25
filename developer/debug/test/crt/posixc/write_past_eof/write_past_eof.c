/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: WritePastEOF DIR - pwrite and lseek+write beyond the end of a file,
          as Chromium's disk cache does: the gap reads as zeros, the file
          grows to the written end, and a failure sets errno.
*/
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int failures;

static void check(const char *what, int ok)
{
    printf("WPE %s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

int main(int argc, char **argv)
{
    char path[256], buf[64];
    const char *dir = argc > 1 ? argv[1] : "RAM:";
    struct stat st;
    ssize_t n;
    int fd, i;

    snprintf(path, sizeof(path), "%swpe.bin", dir);
    unlink(path);
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    check("open", fd >= 0);
    if (fd < 0) return 20;
    errno = 0;
    n = pwrite(fd, "HEAD", 4, 0);
    printf("WPE pwrite head n=%ld errno=%d\n", (long)n, errno);
    check("pwrite at 0", n == 4);
    errno = 0;
    n = pwrite(fd, "TAIL", 4, 100);
    printf("WPE pwrite past eof n=%ld errno=%d\n", (long)n, errno);
    check("pwrite at 100 past eof", n == 4);
    check("fstat", fstat(fd, &st) == 0);
    printf("WPE size=%ld\n", (long)st.st_size);
    check("size 104", st.st_size == 104);
    n = pread(fd, buf, 8, 96);
    check("gap reads zeros then TAIL", n == 8 && !memcmp(buf, "\0\0\0\0TAIL", 8));
    errno = 0;
    check("lseek past eof", lseek(fd, 200, SEEK_SET) == 200);
    n = write(fd, "MORE", 4);
    printf("WPE write after lseek n=%ld errno=%d\n", (long)n, errno);
    check("write after lseek past eof", n == 4);
    check("fstat 2", fstat(fd, &st) == 0 && st.st_size == 204);
    n = pread(fd, buf, 4, 150);
    for (i = 0; i < 4 && n == 4; i++) if (buf[i]) n = -2;
    check("second gap zeros", n == 4);
    close(fd);
    unlink(path);
    printf("WPE result=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 10 : 0;
}
