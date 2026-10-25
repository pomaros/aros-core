/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: ThreadCrashProbe - a pthread writes through NULL. The kernel's crash
          report must name the program the thread runs (its creator's
          segments), not "?".
*/
#include <pthread.h>
#include <stdio.h>

static void *crash(void *arg)
{
    volatile int *nothing = (volatile int *)arg;

    *nothing = 42;
    return NULL;
}

int main(void)
{
    pthread_t thread;

    printf("THREADCRASH starting\n");
    fflush(stdout);
    if (pthread_create(&thread, NULL, crash, NULL) != 0)
        return 20;
    pthread_join(thread, NULL);
    return 0;
}
