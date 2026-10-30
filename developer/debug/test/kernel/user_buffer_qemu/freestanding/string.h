/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Freestanding test declarations; implementations are in the shared probe.
 */
#ifndef BUFFER_PROBE_STRING_H
#define BUFFER_PROBE_STRING_H
#include <stddef.h>
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
#endif
