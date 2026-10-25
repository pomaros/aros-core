/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    POSIX.1-2008 pthread_atfork() function
*/

/*****************************************************************************

    NAME */
#include <pthread.h>

        int pthread_atfork(

/*  SYNOPSIS */
        void (*prepare)(void),
        void (*parent)(void),
        void (*child)(void))

/*  FUNCTION
        Registers handlers to run around fork().

    INPUTS
        prepare - called before fork() in the parent, or NULL
        parent  - called after fork() in the parent, or NULL
        child   - called after fork() in the child, or NULL

    RESULT
        0.

    NOTES
        AROS has no fork(): one address space holds every task, and
        posix_spawn() creates processes. No handler can ever be due, so
        none is kept, and the registration succeeds as POSIX requires.

    EXAMPLE

    BUGS

    SEE ALSO
        posix_spawn()

    INTERNALS

******************************************************************************/
{
    (void)prepare;
    (void)parent;
    (void)child;
    return 0;
} /* pthread_atfork() */
