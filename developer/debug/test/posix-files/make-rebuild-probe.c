/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * This file is distributed under the AROS Public License 1.1.
 */
#define _FILE_OFFSET_BITS 64
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <proto/dos.h>
#include <proto/exec.h>

static int save(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    int result;
    if (!file) return -1;
    result = fputs(text, file) < 0 ? -1 : 0;
    if (fclose(file)) result = -1;
    return result;
}

int main(int argc, char **argv)
{
    const char *directory;
    char path[1024], contents[32] = {0};
    int created = 0;
    char command[] = "-rR -f Makefile out\n";
    struct timespec times[2] = {{1729000010, 100000000}, {1729000010, 100000000}};
    struct stat metadata;
    DIR *stream = NULL;
    BPTR lock = BNULL, old = BNULL, program = BNULL;
    FILE *file = NULL;
    int fd = -1, result = 20;
    const char *message;

    if (argc != 3 || strlen(argv[1]) > 900) {
        puts("Usage: MakeRebuildProbe unused-directory make-executable");
        return 20;
    }
    directory = argv[1];
    /* Never modify or clean an existing directory. */
    if (mkdir(directory, 0700)) goto out;
    created = 1;
    snprintf(path, sizeof(path), "%s/out", directory);
    if (save(path, "stale\n") || utimensat(AT_FDCWD, path, times, 0)) goto out;
    if (stat(path, &metadata) || metadata.st_mtim.tv_sec != times[1].tv_sec
        || metadata.st_mtim.tv_nsec != 100000000) goto out;
    snprintf(path, sizeof(path), "%s/source", directory);
    times[1].tv_nsec = 200000000;
    if (save(path, "changed\n") || utimensat(AT_FDCWD, path, times, 0)) goto out;
    if (stat(path, &metadata) || metadata.st_mtim.tv_sec != times[1].tv_sec
        || metadata.st_mtim.tv_nsec != 200000000) goto out;
    if (lstat(path, &metadata) || metadata.st_mtim.tv_nsec != 200000000) goto out;
    fd = open(path, O_RDONLY);
    if (fd < 0 || fstat(fd, &metadata) || metadata.st_mtim.tv_nsec != 200000000) goto out;
    if (close(fd)) { fd = -1; goto out; }
    fd = -1;
    stream = opendir(directory);
    if (!stream || fstatat(dirfd(stream), "source", &metadata, 0)
        || metadata.st_mtim.tv_nsec != 200000000) goto out;
    if (closedir(stream)) { stream = NULL; goto out; }
    stream = NULL;
    snprintf(path, sizeof(path), "%s/Makefile", directory);
    /* GNU make's own file function writes the recipe output. No child Shell
     * participates: this checks the real dependency timestamp comparison. */
    if (save(path, "out: source\n\t$(file >out,rebuilt)\n")) goto out;
    program = LoadSeg((CONST_STRPTR)argv[2]);
    lock = Lock((CONST_STRPTR)directory, SHARED_LOCK);
    if (!program || !lock) goto out;
    old = CurrentDir(lock);
    if (RunCommand(program, 65536, (STRPTR)command, strlen(command)) != 0) goto restore;
    file = fopen("out", "r");
    if (!file || !fgets(contents, sizeof(contents), file) || strcmp(contents, "rebuilt\n")) goto restore;
    result = 0;
restore:
    if (file) { if (fclose(file)) result = 20; file = NULL; }
    CurrentDir(old);
out:
    if (file && fclose(file)) result = 20;
    if (fd >= 0) close(fd);
    if (stream) closedir(stream);
    if (lock) UnLock(lock);
    if (program) UnLoadSeg(program);
    if (created) {
        const char *names[] = {"Makefile", "source", "out"};
        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
            if (unlink(path) && errno != ENOENT) result = 20;
        }
        if (rmdir(directory)) result = 20;
    }
    message = result ? "[FILES] FAIL actual-make-same-second\n"
        : "[FILES] PASS actual-make-same-second stat-lstat-fstat-fstatat=1\n";
    while (*message) RawPutChar(*message++);
    return result;
}
