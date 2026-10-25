/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: TLSNative - _Thread_local compiled to TLS descriptors by the
          compiler (integration repository D-137)

    Built without -femulated-tls by a compiler that knows AROS TLS: every
    access becomes an AAELF64 descriptor sequence that LoadSeg() resolves.
    tlsnative-other.c defines tls_extern, so one variable is reached across
    translation units, and a second task that gets its own copy.
    Each line: "TLSNative: <check> PASS|FAIL".
*/

#include <dos/dos.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

extern _Thread_local int tls_extern;
int *tls_extern_addr(void);

static _Thread_local long counter = 40;
static _Thread_local char buffer[300];
static int global_value;
_Thread_local int *global_pointer = &global_value;
static int failures;

static void check(const char *what, int ok)
{
    printf("TLSNative: %s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

static long *child_counter;
static long  child_value;

static void *child(void *arg)
{
    (void)arg;
    child_counter = &counter;
    child_value = counter;
    counter = 1;
    return NULL;
}

int main(void)
{
    pthread_t thread;
    int i, clear;


    check("initial value", counter == 40);
    counter += 2;
    check("value kept", counter == 42);
    for (i = 0, clear = 1; i < (int)sizeof(buffer); i++)
        clear &= buffer[i] == 0;
    check("zero-initialised array", clear);
    buffer[299] = 7;
    check("array write", buffer[299] == 7);
    check("relocated pointer initialiser", global_pointer == &global_value);
    check("extern variable, other unit", tls_extern == 1234 && &tls_extern == tls_extern_addr());

    pthread_create(&thread, NULL, child, NULL);
    pthread_join(thread, NULL);
    check("second task gets its own copy", child_counter != &counter && child_value == 40
          && counter == 42);

    printf("TLSNative: done\n");
    return failures ? RETURN_FAIL : RETURN_OK;
}
