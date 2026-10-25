/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: DLTest MODULE/A [KEEP/S] - dlopen(), dlsym(), dlclose() and
          dlerror() against dltest.so

    MODULE is a path, or a bare name that dlopen() looks up (PROGDIR:,
    LIBS:dl/, without the version numbers after ".so").

    Each check prints one line ending in PASS or FAIL; the last line is
    "DLTest: result=PASSED" (0) or "DLTest: result=FAIL" (10). With KEEP the
    module stays loaded when the program ends, for the harness to check that
    it is unloaded then (its destructor writes RAM:DLTest.fini).
*/

#include <dlfcn.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what)
{
    printf("DLTest: %s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

int main(int argc, char **argv)
{
    const char *path;
    int keep;
    void *h, *h2, *self;
    int (*add)(int, int);
    int (*constructed)(void);
    int (*bump)(void);
    int (*format)(char *, int, int);
    int (*lock_ram)(void);
    const char *name;
    int *counter;
    char text[32];

    if (argc < 2)
    {
        printf("usage: DLTest MODULE [KEEP]\n");
        return 20;
    }
    path = argv[1];
    keep = argc > 2 && strcmp(argv[2], "KEEP") == 0;

    check(dlopen("PROGDIR:no-such-module.so", RTLD_NOW) == NULL && dlerror() != NULL,
          "missing module refused with a reason");
    check(dlerror() == NULL, "dlerror cleared after reading");
    check(dlopen(argv[0], RTLD_NOW) == NULL && dlerror() != NULL,
          "an ordinary program refused, not run");
    check(dlopen(path, RTLD_NOLOAD) == NULL, "NOLOAD before loading");
    check(dlopen(path, RTLD_NOW | RTLD_GLOBAL) == NULL && dlerror() != NULL,
          "RTLD_GLOBAL refused, not pretended");
    check(dlopen(path, 0x40000) == NULL && dlerror() != NULL, "unknown mode bits refused");

    /* Run from a Shell, a module does nothing and fails. */
    if (strchr(path, ':') != NULL || strchr(path, '/') != NULL)
        check(SystemTags((CONST_STRPTR)path, SYS_Output, Output(), TAG_DONE) == RETURN_FAIL,
              "module run as a command returns RETURN_FAIL");

    h = dlopen(path, RTLD_NOW);
    check(h != NULL, "dlopen");
    if (h == NULL)
    {
        printf("DLTest: dlerror %s\n", dlerror());
        printf("DLTest: result=FAIL\n");
        return 10;
    }
    constructed = (int (*)(void))dlsym(h, "dltest_constructed");
    check(constructed != NULL && constructed() == 42, "constructor ran in dlopen");
    add = (int (*)(int, int))dlsym(h, "dltest_add");
    check(add != NULL && add(2, 3) == 5, "function export");
    name = (const char *)dlsym(h, "dltest_name");
    check(name != NULL && strcmp(name, "dltest") == 0, "data export");
    counter = (int *)dlsym(h, "dltest_counter");
    bump = (int (*)(void))dlsym(h, "dltest_bump");
    check(counter != NULL && bump != NULL && bump() == 1 && *counter == 1, "module data shared with its code");
    format = (int (*)(char *, int, int))dlsym(h, "dltest_format");
    check(format != NULL && format(text, sizeof(text), 7) > 0 && strcmp(text, "v=7 len=6") == 0,
          "C library through the module's bases");
    lock_ram = (int (*)(void))dlsym(h, "dltest_lock_ram");
    check(lock_ram != NULL && lock_ram() == 1, "dos.library through the module's base");
    check(dlsym(h, "dltest_no_such_symbol") == NULL && dlerror() != NULL, "unknown symbol refused");
    check(dlsym(h, "dltest_dtor") == NULL, "static symbols not exported");
    check(dlsym(RTLD_DEFAULT, "dltest_add") == (void *)add, "RTLD_DEFAULT searches loaded modules");
    {
        Dl_info info;

        check(dladdr((void *)add, &info) != 0 && info.dli_sname != NULL
              && strcmp(info.dli_sname, "dltest_add") == 0 && info.dli_saddr == (void *)add,
              "dladdr names an export");
        check(dladdr((void *)&check, &info) == 0, "dladdr outside every module");
    }
    self = dlopen(NULL, RTLD_NOW);
    check(self != NULL && dlsym(self, "dltest_add") == (void *)add && dlclose(self) == 0,
          "dlopen(NULL) handle");

    h2 = dlopen(path, RTLD_LAZY);
    check(h2 == h, "second dlopen returns the same handle");
    check(dlopen(path, RTLD_NOLOAD) == h, "NOLOAD while loaded");
    check(dlclose(h2) == 0 && dlclose(h) == 0 && bump() == 2, "references counted");
    check(dlclose(h) == 0, "last reference released");
    check(dlclose(h) == -1 && dlerror() != NULL, "closed handle refused");
    check(dlopen(path, RTLD_NOLOAD) == NULL, "NOLOAD after unloading");

    h = dlopen(path, RTLD_NOW);
    bump = h ? (int (*)(void))dlsym(h, "dltest_bump") : NULL;
    check(bump != NULL && bump() == 1, "reloaded module starts afresh");
    if (h != NULL && !keep)
        check(dlclose(h) == 0, "closed before exit");

    printf("DLTest: result=%s\n", failures ? "FAIL" : "PASSED");
    return failures ? 10 : 0;
}
