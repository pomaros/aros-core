/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: TLSBench - the cost of one thread-local access, emulated against
          native (integration repository D-137)

    Emulated: pthread_getspecific(), the path compiler-rt's emutls and
    Rust's thread_local! take. It is timed with 2 and then 40 pthreads
    alive, since pthread_self() scans the thread table: from the main
    thread (its first entry, the best case) and from a thread created after
    the 40 (its last entry, the worst case). Native: what a TLS
    descriptor access costs once exec keeps a per-task thread pointer: read
    TPIDR_EL0, index a per-task block table, load. TPIDR_EL0 is not saved
    across task switches yet, so the native loop runs in one task with no
    other user of the register. Each line: "TLSBench: <case> <ns> ns".
*/

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define LOOPS 2000000

static pthread_key_t key;
static volatile int stop;
static uintptr_t blocks[4] = { 0 };

static void *idle_thread(void *arg)
{
    (void)arg;
    while (!stop)
    {
        struct timespec ts = { 0, 10000000 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static double time_emulated(void)
{
    volatile uintptr_t sum = 0;
    double t0 = now_ns();
    for (int i = 0; i < LOOPS; i++)
        sum += (uintptr_t)pthread_getspecific(key);
    return (now_ns() - t0) / LOOPS;
}

static double last_result;

/* pthread_self() scans the thread table from its start: the main thread is
   its first entry (the best case), the thread created last its worst. */
static void *measure_thread(void *arg)
{
    (void)arg;
    pthread_setspecific(key, (void *)2);
    last_result = time_emulated();
    return NULL;
}

static double time_native(void)
{
    volatile uintptr_t sum = 0;
    uintptr_t table[2] = { (uintptr_t)blocks, 0 };
    double t0;

    __asm__ volatile("msr tpidr_el0, %0" :: "r"(table));
    t0 = now_ns();
    for (int i = 0; i < LOOPS; i++)
    {
        uintptr_t *tp;
        __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp));
        sum += ((uintptr_t *)tp[0])[1];
    }
    return (now_ns() - t0) / LOOPS;
}

int main(void)
{
    pthread_t threads[40];
    int started = 0;

    pthread_key_create(&key, NULL);
    pthread_setspecific(key, (void *)1);

    for (; started < 1; started++)
        pthread_create(&threads[started], NULL, idle_thread, NULL);
    printf("TLSBench: emulated threads=2 %.1f ns\n", time_emulated());
    for (; started < 39; started++)
        pthread_create(&threads[started], NULL, idle_thread, NULL);
    printf("TLSBench: emulated threads=40 main %.1f ns\n", time_emulated());
    {
        pthread_t last;
        pthread_create(&last, NULL, measure_thread, NULL);
        pthread_join(last, NULL);
        printf("TLSBench: emulated threads=41 last %.1f ns\n", last_result);
    }
    printf("TLSBench: native %.1f ns\n", time_native());

    stop = 1;
    for (int i = 0; i < started; i++)
        pthread_join(threads[i], NULL);
    printf("TLSBench: done\n");
    return 0;
}
