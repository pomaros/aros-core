/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: the test module of DLTest: constructors, data, the C library and
          dos.library through the module's own bases
*/

#include <proto/dos.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

static int constructed;

/* The task is only known at run time, so the compiler cannot fold this. */
__attribute__((constructor)) static void dltest_ctor(void)
{
    constructed = FindTask(NULL) != NULL ? 42 : 1;
}

/* The destructor leaves RAM:DLTest.fini behind, so that the harness can
   see that a module still loaded at exit was unloaded. */
__attribute__((destructor)) static void dltest_dtor(void)
{
    BPTR file = Open((CONST_STRPTR)"RAM:DLTest.fini", MODE_NEWFILE);

    if (file != BNULL)
    {
        FPuts(file, (CONST_STRPTR)"fini\n");
        Close(file);
    }
}

const char dltest_name[] = "dltest";
int dltest_counter;

int dltest_add(int a, int b)
{
    return a + b;
}

int dltest_constructed(void)
{
    return constructed;
}

int dltest_bump(void)
{
    return ++dltest_counter;
}

int dltest_format(char *buffer, int size, int value)
{
    return snprintf(buffer, (size_t)size, "v=%d len=%d", value, (int)strlen(dltest_name));
}

int dltest_lock_ram(void)
{
    BPTR lock = Lock((CONST_STRPTR)"RAM:", SHARED_LOCK);

    if (lock == BNULL)
        return 0;
    UnLock(lock);
    return 1;
}
