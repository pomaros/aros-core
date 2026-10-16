/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: The four block primitives a freestanding kernel must own.
 */

#include <stddef.h>
#include <stdint.h>

/*
 * A C compiler is free to turn an ordinary loop or an aggregate assignment
 * into a call to memset, memcpy, memmove or memcmp, whatever the source said
 * and whatever the compiler flags ask for: a freestanding implementation is
 * still required to provide these four. AROS resolves such a call through
 * StdCBase, which the kernel never opens, so the generated wrapper subtracts
 * the library vector offset from a null base and reads the jump table at a
 * negative address. That is a data abort at 0xfffffffffffff480 in a kernel
 * whose only fault was to zero a structure with a loop.
 *
 * Defining the four here keeps every synthesized call inside the kernel. The
 * bodies address memory through volatile pointers so the compiler cannot
 * recognise them as the very functions it is compiling and call them.
 */

void *memset(void *destination, int value, size_t length);
void *memcpy(void *destination, const void *source, size_t length);
void *memmove(void *destination, const void *source, size_t length);
int memcmp(const void *left, const void *right, size_t length);

void *memset(void *destination, int value, size_t length)
{
    volatile uint8_t *bytes = destination;
    size_t index;

    for (index = 0; index < length; ++index)
        bytes[index] = (uint8_t)value;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t length)
{
    volatile uint8_t *to = destination;
    const volatile uint8_t *from = source;
    size_t index;

    for (index = 0; index < length; ++index)
        to[index] = from[index];
    return destination;
}

void *memmove(void *destination, const void *source, size_t length)
{
    volatile uint8_t *to = destination;
    const volatile uint8_t *from = source;
    size_t index;

    if (to == from || length == 0)
        return destination;
    /* Overlapping regions copy away from the overlap. */
    if (to < from) {
        for (index = 0; index < length; ++index)
            to[index] = from[index];
    } else {
        for (index = length; index != 0; --index)
            to[index - 1] = from[index - 1];
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t length)
{
    const volatile uint8_t *one = left;
    const volatile uint8_t *two = right;
    size_t index;

    for (index = 0; index < length; ++index) {
        if (one[index] != two[index])
            return one[index] < two[index] ? -1 : 1;
    }
    return 0;
}
