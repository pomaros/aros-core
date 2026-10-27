/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>

/* Only call on a disposable 64 MiB filesystem after other probes.
 * No preassigned errno or synthetic DOS error is used here. */
int filesystem_full_volume_probe(const char *directory)
{
    const size_t chunk_size = 65536;
    unsigned char *chunk = NULL;
    char path[128], control[128], message[192];
    unsigned writes = 0;
    unsigned long long bytes = 0;
    int fd = -1, saved_error = 0, failure = 0, found_full = 0;
    ssize_t written;
    int fill_created = 0, control_created = 0;
    if (!directory || !*directory ||
        snprintf(path, sizeof(path), "%s/full-%ld", directory, (long)getpid()) >= (int)sizeof(path) ||
        snprintf(control, sizeof(control), "%s/recovered-%ld", directory, (long)getpid()) >= (int)sizeof(control))
        return 20;
    chunk = malloc(chunk_size);
    if (!chunk) { failure = 1; goto out; }
    memset(chunk, 0xa7, chunk_size);
    fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) { failure = 1; goto out; }
    fill_created = 1;
    /* At most 128 MiB of writes for the fixed 64 MiB fixture. */
    while (writes++ != 2048) {
        written = write(fd, chunk, chunk_size);
        saved_error = errno;
        if (written > 0) { bytes += written; continue; }
        if (written < 0 && saved_error == EINTR) continue;
        found_full = written == -1 && saved_error == ENOSPC;
        break;
    }
    if (!found_full || !bytes) failure = 2;
    /* A buffered failure during close must not erase the admitted write error. */
    close(fd); fd = -1;
    if (unlink(path)) { failure = 3; goto out; }
    fill_created = 0;
    fd = open(control, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) { failure = 4; goto out; }
    control_created = 1;
    if (write(fd, "recovered", 9) != 9) { failure = 4; goto out; }
    if (close(fd)) failure = 5;
    fd = -1;
    if (unlink(control)) failure = 6;
    else control_created = 0;
out:
    if (fd >= 0) close(fd);
    if (fill_created) unlink(path);
    if (control_created) unlink(control);
    free(chunk);
    if (failure)
        snprintf(message, sizeof(message), "[FILESYSTEM] FAIL actual-full-volume stage=%d errno=%d bytes=%llu writes=%u\n",
                 failure, saved_error, bytes, writes);
    else
        snprintf(message, sizeof(message), "[FILESYSTEM] PASS actual-full-volume errno=ENOSPC bytes=%llu recovery-write=1\n", bytes);
    for (const char *cursor = message; *cursor; ++cursor) RawPutChar(*cursor);
    return failure ? 20 : 0;
}
