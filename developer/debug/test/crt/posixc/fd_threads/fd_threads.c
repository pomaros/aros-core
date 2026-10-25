/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: FdThreads - descriptors stay private to whoever opened them while
          other threads open, duplicate and close their own. Each thread
          opens a file of its own, checks that the descriptor reads back that
          file, duplicates it and closes both; a close that fails, or a
          descriptor that reads another thread's file, means two threads
          were handed the same slot. Every eighth round also passes a byte
          through a pipe of its own.
*/
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_THREADS 16

static unsigned long rounds = 2000;
static unsigned threads = 4;
static const char *dir = "RAM:";

struct worker
{
    pthread_t thread;
    unsigned id;
    unsigned long bad;
    int setup_fd;
    char first[160];
};

static pthread_barrier_t opened;

static void fail(struct worker *w, const char *what, unsigned long round, int value)
{
    if (w->bad++ == 0)
        snprintf(w->first, sizeof(w->first), "round %lu %s %d", round, what, value);
}

static void *run(void *arg)
{
    struct worker *w = arg;
    char path[64], line[16], back[16];
    unsigned long round;
    int fd;

    snprintf(path, sizeof(path), "%sfdthreads.%u", dir, w->id);
    snprintf(line, sizeof(line), "thread %u", w->id);
    /* Every thread holds its first descriptor open while the others open
       theirs: the numbers must differ. */
    fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    w->setup_fd = fd;
    pthread_barrier_wait(&opened);
    if (fd < 0 || write(fd, line, strlen(line)) != (ssize_t)strlen(line) || close(fd) != 0) {
        fail(w, "setup", 0, fd);
        return NULL;
    }
    for (round = 0; round < rounds; ++round) {
        int copy;
        ssize_t got;

        fd = open(path, O_RDONLY);
        if (fd < 0) {
            fail(w, "open", round, fd);
            continue;
        }
        copy = dup(fd);
        got = read(fd, back, sizeof(back) - 1);
        if (got != (ssize_t)strlen(line) || memcmp(back, line, got) != 0)
            fail(w, "read another file on fd", round, fd);
        if (copy < 0)
            fail(w, "dup", round, copy);
        else if (close(copy) != 0)
            fail(w, "close of dup", round, copy);
        if (close(fd) != 0)
            fail(w, "close", round, fd);
        if ((round & 7) == 0) {
            int ends[2];
            char byte = (char)w->id, seen = -1;

            /* Both ends are numbers of this thread's own */
            if (pipe(ends) != 0)
                fail(w, "pipe failed, errno", round, errno);
            else {
                if (write(ends[1], &byte, 1) != 1 || read(ends[0], &seen, 1) != 1 ||
                        seen != byte)
                    fail(w, "pipe carried another byte", round, seen);
                if (close(ends[1]) != 0)
                    fail(w, "close of pipe writer", round, ends[1]);
                if (close(ends[0]) != 0)
                    fail(w, "close of pipe reader", round, ends[0]);
            }
        }
    }
    unlink(path);
    return NULL;
}

int main(int argc, char **argv)
{
    struct worker workers[MAX_THREADS] = {{0}};
    unsigned long bad = 0;
    unsigned i;

    if (argc > 1)
        rounds = strtoul(argv[1], NULL, 0);
    if (argc > 2)
        dir = argv[2];
    if (argc > 3)
        threads = strtoul(argv[3], NULL, 0);
    if (threads < 1 || threads > MAX_THREADS)
        return 20;
    pthread_barrier_init(&opened, NULL, threads + 1);
    for (i = 0; i < threads; ++i) {
        workers[i].id = i;
        if (pthread_create(&workers[i].thread, NULL, run, &workers[i]) != 0)
            return 20;
    }
    pthread_barrier_wait(&opened);
    printf("FDTHREADS setup fds:");
    for (i = 0; i < threads; ++i) {
        unsigned j;

        printf(" %d", workers[i].setup_fd);
        for (j = 0; j < i; ++j)
            if (workers[j].setup_fd == workers[i].setup_fd)
                ++bad;
    }
    printf("%s\n", bad ? " (shared)" : "");
    fflush(stdout);
    for (i = 0; i < threads; ++i) {
        pthread_join(workers[i].thread, NULL);
        printf("FDTHREADS thread %u bad=%lu %s\n", i, workers[i].bad, workers[i].first);
        bad += workers[i].bad;
    }
    printf("FDTHREADS result=%s bad=%lu\n", bad ? "FAIL" : "PASS", bad);
    return bad ? 10 : 0;
}
