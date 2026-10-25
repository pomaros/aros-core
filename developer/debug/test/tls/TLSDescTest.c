/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: TLSDescTest - native TLS through the descriptors LoadSeg() builds
          (integration repository D-137)

    The TLS variables and their access sequences are in tlsdesc-access.S.
    The first access in the program goes through the resolver's slow path
    (no control block yet), later ones through its fast path. TPIDR_EL0 is
    the address of the task's TLS anchor, set by exec for the task's whole
    life: the first access must not change it. LEAKCHECK runs 50 short-lived
    tasks and checks that their TLS memory went with them. Each line:
    "TLSDescTest: <check> PASS|FAIL".
*/

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <dos/dos.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

extern void *tls_addr_init(void);
extern void *tls_addr_zero(void);
extern void tls_preserve(UQUAD out[49]);
extern UQUAD tls_global;

static int failures;

static void check(const char *what, int ok)
{
    printf("TLSDescTest: %s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

static IPTR read_tp(void)
{
    IPTR tp;

    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp));
    return tp;
}

static int preserved(const UQUAD *regs)
{
    int i;

    for (i = 0; i < 17; i++)
        if (regs[i] != 0x5a00 + 2 + i)
            return 0;
    for (i = 0; i < 32; i++)
        if (regs[17 + i] != 0x5a00 + 0x100 + i)
            return 0;
    return 1;
}

static UQUAD *child_init;
static UQUAD  child_value;

static void *child(void *arg)
{
    (void)arg;
    child_init = tls_addr_init();
    child_value = child_init[0];
    child_init[0] = 7;
    return NULL;
}

static void *short_lived(void *arg)
{
    (void)arg;
    ((UQUAD *)tls_addr_init())[0] = 1;
    ((UBYTE *)tls_addr_zero())[0] = 1;
    return NULL;
}

static void *no_tls(void *arg)
{
    return arg;
}

/* Memory lost by 50 short-lived tasks running fn */
static IPTR lost_by(void *(*fn)(void *))
{
    IPTR      before, after;
    pthread_t thread;
    int       i;

    before = AvailMem(MEMF_ANY);
    for (i = 0; i < 50; i++)
    {
        pthread_create(&thread, NULL, fn, NULL);
        pthread_join(thread, NULL);
    }
    after = AvailMem(MEMF_ANY);
    return before - after;
}

/* Compared with tasks that use no TLS, so that what pthreads keep cancels */
static int no_leak(void)
{
    IPTR control, tls;
    BYTE pri;

    /* exec's service task frees an ended task's memory: below it, this task
       runs only once it has (no Delay(): a QEMU native image has no timer) */
    pri = SetTaskPri(FindTask(NULL), -10);
    lost_by(short_lived);
    control = lost_by(no_tls);
    tls = lost_by(short_lived);
    SetTaskPri(FindTask(NULL), pri);
    printf("TLSDescTest: 50 tasks lose %ld bytes without TLS, %ld with\n", (long)control, (long)tls);
    return (SIPTR)(tls - control) < 2048;
}

int main(int argc, char **argv)
{
    UQUAD     regs[49];
    UQUAD    *init;
    UBYTE    *zero;
    pthread_t thread;
    IPTR      tp;
    int       i, clear;

    tp = read_tp();
    check("the task has a TLS anchor", tp != 0);
    check("no control block before the first access", tp && *(APTR *)tp == NULL);
    tls_preserve(regs);
    check("slow path preserves x2-x18 and v0-v31", preserved(regs));
    check("control block installed in the anchor", tp && *(APTR *)tp != NULL);
    check("thread pointer unchanged by the first access", read_tp() == tp);
    tls_preserve(regs);
    check("fast path preserves x2-x18 and v0-v31", preserved(regs));

    init = tls_addr_init();
    zero = tls_addr_zero();
    check(".tdata initial value", init[0] == 0x1122334455667788ULL);
    check(".tdata relocated pointer", init[1] == (UQUAD)(IPTR)&tls_global);
    for (i = 0, clear = 1; i < 256; i++)
        clear &= zero[i] == 0;
    check(".tbss zeroed", clear);
    check(".tbss 64-byte alignment", ((IPTR)zero & 63) == 0);
    check("same address on the next access", tls_addr_init() == init && tls_addr_zero() == zero);
    init[0] = 42;
    check("value kept between accesses", ((UQUAD *)tls_addr_init())[0] == 42);

    if (argc > 1 && strcmp(argv[1], "LEAKCHECK") == 0)
        check("50 ended tasks left no TLS memory behind", no_leak());

    pthread_create(&thread, NULL, child, NULL);
    pthread_join(thread, NULL);
    check("second task gets its own block", child_init != init && child_value == 0x1122334455667788ULL
          && init[0] == 42);

    printf("TLSDescTest: done\n");
    return failures ? RETURN_FAIL : RETURN_OK;
}
