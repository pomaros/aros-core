/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: TLSGrow - a task's slot table grows between two TLS accesses of one
          function (integration repository D-137)

    Compiled TLS descriptor code may read TPIDR_EL0 once and keep it for the
    whole function. The function below reads its own TLS variable, then
    loads nine copies of a TLS module with dlopen() and touches each one, so
    that the task's slot table grows past its first eight slots, then reads
    its variable again through the same thread pointer. Usage: TLSGrow DIR,
    DIR holding tlsgrow.so. Each line: "TLSGrow: <check> PASS|FAIL".
*/

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

_Thread_local long mine = 77;
_Thread_local long mine2 = 99;
static void *handles[9];
static char from[256];
static int failures, values = 1;

static void check(const char *what, int ok)
{
    printf("TLSGrow: %s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

static int copy(const char *src, const char *to)
{
    FILE *in = fopen(src, "rb"), *out = fopen(to, "wb");
    char buf[4096];
    size_t n;
    int ok = in && out;

    while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0)
        ok = fwrite(buf, 1, n, out) == n;
    if (in) fclose(in);
    if (out) fclose(out);
    return ok;
}

/* Nine more TLS segments, each touched once: the table grows past 8 slots */
__attribute__((noinline)) static void load_nine(void)
{
    char path[256];
    int i;

    for (i = 0; i < 9; i++)
    {
        int *(*fn)(void);

        snprintf(path, sizeof(path), "RAM:tlsgrow%d.so", i);
        handles[i] = copy(from, path) ? dlopen(path, RTLD_NOW) : NULL;
        fn = handles[i] ? (int *(*)(void))dlsym(handles[i], "module_tls") : NULL;
        values &= fn != NULL && *fn() == 5;
    }
}

/* The compiler reads TPIDR_EL0 once here and keeps it across the call */
__attribute__((noinline)) static long across_growth(void)
{
    long a = mine;

    load_nine();
    return a + mine2;
}

int main(int argc, char **argv)
{
    int i;

    if (argc < 2)
        return 20;
    snprintf(from, sizeof(from), "%s/tlsgrow.so", argv[1]);

    check("two TLS reads across the table's growth", across_growth() == 77 + 99);
    check("nine TLS modules loaded, each with its initial value", values);
    for (i = 0; i < 9; i++)
        if (handles[i])
            dlclose(handles[i]);
    printf("TLSGrow: done\n");
    return failures ? 20 : 0;
}
