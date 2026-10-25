/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: AtomicStress - load-exclusive/store-exclusive atomics stay atomic
          across preemption. Threads increment one shared counter with an
          LDXR/STXR loop and a CAS loop; a context switch that leaves the
          exclusive monitor armed lets a stale STXR succeed and loses counts.
*/
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define THREADS 4

static volatile uint64_t counter, cas_counter;
static unsigned long count = 20000000;

static void exclusive_add(volatile uint64_t *at)
{
    uint64_t value;
    uint32_t failed;

    __asm__ volatile(
        "1: ldxr %0, [%2]\n"
        "   add %0, %0, #1\n"
        "   stxr %w1, %0, [%2]\n"
        "   cbnz %w1, 1b\n"
        : "=&r"(value), "=&r"(failed) : "r"(at) : "memory");
}

static void *run(void *arg)
{
    unsigned long i;

    (void)arg;
    for (i = 0; i < count; ++i) {
        uint64_t seen;

        exclusive_add(&counter);
        seen = cas_counter;
        while (!__atomic_compare_exchange_n(&cas_counter, &seen, seen + 1, 0,
                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            ;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t threads[THREADS];
    uint64_t expect;
    unsigned i;

    if (argc > 1)
        count = strtoul(argv[1], NULL, 0);
    for (i = 0; i < THREADS; ++i)
        if (pthread_create(&threads[i], NULL, run, NULL) != 0)
            return 20;
    for (i = 0; i < THREADS; ++i)
        pthread_join(threads[i], NULL);
    expect = (uint64_t)count * THREADS;
    printf("ATOMIC exclusive=%llu cas=%llu expect=%llu\n",
        (unsigned long long)counter, (unsigned long long)cas_counter,
        (unsigned long long)expect);
    printf("ATOMIC result=%s\n",
        counter == expect && cas_counter == expect ? "PASS" : "FAIL");
    return counter == expect && cas_counter == expect ? 0 : 10;
}
