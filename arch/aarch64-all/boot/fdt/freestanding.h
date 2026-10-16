/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Minimal byte operations for libfdt before any runtime or allocator exists.
 */
#ifndef AROS_FDT_FREESTANDING_H
#define AROS_FDT_FREESTANDING_H
#include <stddef.h>
#include <stdint.h>
static inline size_t afdt_strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}
static inline size_t afdt_strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i])
        ++i;
    return i;
}
static inline int afdt_memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; ++i)
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    return 0;
}
static inline void *afdt_memchr(const void *p, int c, size_t n)
{
    const uint8_t *x = p;
    for (size_t i = 0; i < n; ++i)
        if (x[i] == (uint8_t)c)
            return (void *)(x + i);
    return NULL;
}
static inline void *afdt_memcpy(void *d, const void *s, size_t n)
{
    uint8_t *x = d;
    const uint8_t *y = s;
    for (size_t i = 0; i < n; ++i)
        x[i] = y[i];
    return d;
}
static inline void *afdt_memmove(void *d, const void *s, size_t n)
{
    uint8_t *x = d;
    const uint8_t *y = s;
    if ((uintptr_t)x < (uintptr_t)y)
        return afdt_memcpy(d, s, n);
    while (n)
    {
        --n;
        x[n] = y[n];
    }
    return d;
}
static inline void *afdt_memset(void *d, int c, size_t n)
{
    uint8_t *x = d;
    for (size_t i = 0; i < n; ++i)
        x[i] = (uint8_t)c;
    return d;
}
static inline int afdt_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return (uint8_t)*a - (uint8_t)*b;
}
static inline int afdt_strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b)
    {
        ++a;
        ++b;
        --n;
    }
    return n ? (uint8_t)*a - (uint8_t)*b : 0;
}
static inline char *afdt_strchr(const char *s, int c)
{
    for (;; ++s)
    {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return NULL;
    }
}
static inline char *afdt_strrchr(const char *s, int c)
{
    const char *p = NULL;
    do
    {
        if (*s == (char)c)
            p = s;
    } while (*s++);
    return (char *)p;
}
#define strlen afdt_strlen
#define strnlen afdt_strnlen
#define memcmp afdt_memcmp
#define memchr afdt_memchr
#define memcpy afdt_memcpy
#define memmove afdt_memmove
#define memset afdt_memset
#define strcmp afdt_strcmp
#define strncmp afdt_strncmp
#define strchr afdt_strchr
#define strrchr afdt_strrchr
#endif
