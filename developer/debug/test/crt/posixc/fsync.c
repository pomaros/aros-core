/*
    Copyright (C) 2026 John Knipper.
    Licensed under the AROS Public License (APL), Version 1.1.

    File and directory handler barriers. This checks the API contract, not
    persistence across a power failure. T: may be a volatile filesystem.
*/
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

static int make_directory(char *name, size_t size)
{
    unsigned int attempt;
    for (attempt = 0; attempt < 1000; attempt++)
    {
        snprintf(name, size, "T:fsync-contract.%lu.%u", (unsigned long)getpid(), attempt);
        if (mkdir(name, 0700) == 0) return 1;
        if (errno != EEXIST) return 0;
    }
    errno = EEXIST;
    return 0;
}

int main(void)
{
    char directory[64];
    char path[128] = {0};
    char bytes[4];
    int file = -1, dir = -1, result = 20, created = 0, length;
    const char *stage = "mkdir";

#define CHECK(label, operation) do { stage = label; if (!(operation)) goto out; } while (0)
    CHECK("mkdir", make_directory(directory, sizeof(directory))); created = 1;
    CHECK("path", (length = snprintf(path, sizeof(path), "%s/file", directory)) >= 0 && length < (int)sizeof(path));
    CHECK("open-write", (file = open(path, O_CREAT | O_EXCL | O_RDWR, 0600)) >= 0);
    CHECK("write", write(file, "AROS", 4) == 4);
    CHECK("file-write-barrier", fsync(file) == 0);
    CHECK("close-write", close(file) == 0); file = -1;
    CHECK("open-read", (file = open(path, O_RDONLY)) >= 0);
    CHECK("readback", read(file, bytes, sizeof(bytes)) == sizeof(bytes));
    CHECK("compare", memcmp(bytes, "AROS", 4) == 0);
    CHECK("file-read-barrier", fsync(file) == 0);
    CHECK("close-read", close(file) == 0); file = -1;
    CHECK("open-directory", (dir = open(directory, O_RDONLY)) >= 0);
    CHECK("directory-barrier", fsync(dir) == 0);
    CHECK("unlink", unlink(path) == 0); path[0] = 0;
    CHECK("directory-unlink-barrier", fsync(dir) == 0);
    errno = 0;
    CHECK("invalid-descriptor", fsync(-1) == -1 && errno == EBADF);
    result = 0;
out:
    {
        int saved_error = errno;
#define CLEANUP(label, operation) do { if ((operation) != 0 && !result) { result = 20; stage = label; saved_error = errno; } } while (0)
        if (file >= 0) CLEANUP("cleanup-file", close(file));
        if (dir >= 0) CLEANUP("cleanup-directory", close(dir));
        if (path[0]) CLEANUP("cleanup-unlink", unlink(path));
        if (created) CLEANUP("cleanup-rmdir", rmdir(directory));
        if (result) printf("[FSYNC] contract FAIL stage=%s errno=%d\n", stage, saved_error);
        else puts("[FSYNC] contract PASS (handler barrier; power-loss persistence unclaimed)");
    }
    return result;
}
