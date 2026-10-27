/*
    Copyright (C) 1995-2014, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
*/

#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include "test.h"

int fd = -1;
static char pathname[] = "T:lseekXXXXXX";

int main()
{
    struct stat buf;
    int size = 12345;
    fd = mkstemp(pathname);
    TEST(fd >= 0);
    
    TEST((lseek(fd, size, SEEK_SET) == size));
    TEST((fstat(fd, &buf) != -1));
    TEST((buf.st_size == 0));

    TEST((lseek(fd, 0, SEEK_SET) == 0));
    TEST((lseek(fd, 100, SEEK_CUR) == 100));
    TEST((lseek(fd, -100, SEEK_CUR) == 0));

    TEST((lseek(fd, 0, SEEK_END) == 0));
    TEST((write(fd, "x", 1) == 1));
    TEST((fstat(fd, &buf) != -1));
    TEST((buf.st_size == 1));
    TEST((lseek(fd, -1, SEEK_END) == 0));
    TEST((lseek(fd, size, SEEK_SET) == size));
    TEST((fstat(fd, &buf) != -1));
    TEST((buf.st_size == 1));

    cleanup();
    return OK;
}

void cleanup()
{
    if (fd >= 0) {
        close(fd);
        fd = -1;
        unlink(pathname);
    }
}
