/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1.
 */
#ifndef POSIXC_PIPE_NAME_H
#define POSIXC_PIPE_NAME_H

#include <stddef.h>
#include <stdio.h>

/* Two hex digits and at most three decimal digits per byte, plus '-' and NUL. */
#define POSIXC_PIPE_NAME_SIZE (sizeof("PIPE:cpipe-") + 5 * sizeof(unsigned long) + 1)

static inline void __posixc_pipe_name(char *name, unsigned long pid)
{
    static unsigned long serial;
    /* Keep the allocated number local: another caller can advance the shared
     * counter before snprintf. No scheduling exclusion or ordering of other
     * memory is required to reserve a unique number. Unsigned wrap is defined. */
    unsigned long number = __atomic_add_fetch(&serial, 1, __ATOMIC_RELAXED);
    snprintf(name, POSIXC_PIPE_NAME_SIZE, "PIPE:cpipe-%lx-%lu", pid, number);
}

#endif
