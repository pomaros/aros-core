/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1.
 */
#define _FILE_OFFSET_BITS 64
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef __APPLE__
#define MODIFIED(st) ((st).st_mtimespec)
#else
#define MODIFIED(st) ((st).st_mtim)
#endif

static unsigned checks, failures;
#define CHECK(expression) do { ++checks; if (!(expression)) { ++failures; \
    printf("[FILES] FAIL line=%u errno=%d\n", __LINE__, errno); } } while (0)

static int save(const char *path, const char *text)
{
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    int result;
    if (fd < 0) return -1;
    result = write(fd, text, strlen(text)) == (ssize_t)strlen(text) ? 0 : -1;
    if (close(fd)) result = -1;
    return result;
}

int main(int argc, char **argv)
{
    char names[8][1024], renamed[1024], alias[1024], plain[1024], soft[1024];
    char target[521], buffer[600];
    struct stat before = {0}, after = {0};
    struct timespec times[2] = {{1729000010, 0}, {1729000010, 0}};
    ino_t ids[8] = {0};
    unsigned i, j;
    int ticks, fd;

    if (argc != 3 || strlen(argv[1]) > 900 ||
        (strcmp(argv[2], "nanoseconds") && strcmp(argv[2], "ticks"))) {
        puts("Usage: FilesProbe unused-directory nanoseconds|ticks");
        return 20;
    }
    /* Refuse an existing root before modifying or cleaning anything. */
    if (mkdir(argv[1], 0700)) {
        perror("FilesProbe mkdir");
        return 20;
    }
    ticks = !strcmp(argv[2], "ticks");
    snprintf(renamed, sizeof(renamed), "%s/renamed", argv[1]);
    snprintf(alias, sizeof(alias), "%s/alias", argv[1]);
    snprintf(plain, sizeof(plain), "%s/plain", argv[1]);
    snprintf(soft, sizeof(soft), "%s/soft", argv[1]);
    for (i = 0; i < 8; ++i) {
        long expected;
        snprintf(names[i], sizeof(names[i]), "%s/f%u", argv[1], i);
        CHECK(save(names[i], "new") == 0);
        times[1].tv_nsec = (i + 1) * 1234567L;
        expected = ticks ? times[1].tv_nsec / 20000000 * 20000000 : times[1].tv_nsec;
        CHECK(utimensat(AT_FDCWD, names[i], times, 0) == 0);
        CHECK(stat(names[i], &after) == 0);
        CHECK(MODIFIED(after).tv_sec == times[1].tv_sec);
        CHECK(MODIFIED(after).tv_nsec == expected);
        CHECK(lstat(names[i], &before) == 0 && before.st_ino == after.st_ino);
        ids[i] = after.st_ino;
        for (j = 0; j < i; ++j) CHECK(ids[j] != ids[i]);
    }
    CHECK(stat(names[0], &before) == 0);
    CHECK(rename(names[0], renamed) == 0);
    CHECK(stat(renamed, &after) == 0 && after.st_ino == before.st_ino);
    CHECK(link(renamed, alias) == 0);
    CHECK(stat(alias, &after) == 0 && after.st_ino == before.st_ino && after.st_nlink == 2);
    CHECK(save(plain, "old") == 0);
    errno = 0;
    CHECK(readlink(plain, buffer, sizeof(buffer)) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(readlink(argv[1], buffer, sizeof(buffer)) == -1 && errno == EINVAL);
    memset(target, 'x', sizeof(target) - 1);
    target[sizeof(target) - 1] = 0;
    CHECK(symlink(target, soft) == 0);
    CHECK(lstat(soft, &after) == 0 && S_ISLNK(after.st_mode));
    memset(buffer, 0x5a, sizeof(buffer));
    CHECK(readlink(soft, buffer, 7) == 7);
    CHECK(!memcmp(buffer, target, 7) && buffer[7] == 0x5a);
    memset(buffer, 0x5a, sizeof(buffer));
    CHECK(readlink(soft, buffer, sizeof(buffer)) == 520);
    CHECK(!memcmp(buffer, target, 520) && buffer[520] == 0x5a);
    CHECK(rename(renamed, plain) == 0);
    CHECK(stat(plain, &after) == 0 && after.st_ino == before.st_ino);
    fd = open(plain, O_RDONLY);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK(fstat(fd, &after) == 0 && after.st_ino == before.st_ino);
        errno = 0;
        CHECK(fstatat(fd, "ignored", &after, 0) == -1 && errno == ENOTDIR);
        CHECK(read(fd, buffer, 3) == 3 && !memcmp(buffer, "new", 3));
        CHECK(close(fd) == 0);
    }
    CHECK(unlink(soft) == 0);
    CHECK(unlink(alias) == 0);
    CHECK(unlink(plain) == 0);
    for (i = 1; i < 8; ++i) CHECK(unlink(names[i]) == 0);
    /* Cleanup the original name if a failed rename left it behind. */
    if (unlink(names[0]) != 0) CHECK(errno == ENOENT);
    if (unlink(renamed) != 0) CHECK(errno == ENOENT);
    CHECK(rmdir(argv[1]) == 0);
    printf("[FILES] result=%s checks=%u failures=%u\n",
        failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 20 : 0;
}
