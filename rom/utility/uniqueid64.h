/*
    Copyright (C) 2026 John KNIPPER. All rights reserved.
    Licensed under the AROS Public License (APL) 1.1; see LICENSE.
*/
#ifndef UTILITY_UNIQUEID64_H
#define UTILITY_UNIQUEID64_H
#include <stdint.h>
/* Caller serializes the shared counter. Exhaustion is sticky: never wrap. */
static inline uint64_t utility_unique_id64_next(uint64_t *previous)
{
    if (*previous == UINT64_MAX) return 0;
    return ++*previous;
}
#endif
