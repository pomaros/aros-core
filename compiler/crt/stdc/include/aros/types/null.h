#ifndef _AROS_TYPES_NULL_H
#define _AROS_TYPES_NULL_H

/*
    Copyright © 2010-2012, The AROS Development Team. All rights reserved.
    $Id$

    NULL
*/

#ifdef NULL
#undef NULL
#endif

/* In C++ a plain 0 is an int: passed through "..." (a NULL that ends an
   argument list) it fills only half of a 64-bit argument slot, and the
   callee reads a pointer with an undefined upper half. __null, as GCC and
   Clang define it for C++, has pointer width and still converts to every
   pointer type. */
#ifndef __cplusplus
#define NULL ((void *)0)
#elif defined(__GNUG__) || defined(__clang__)
#define NULL __null
#else
#define NULL 0L
#endif

#endif /* _AROS_TYPES_NULL_H */
