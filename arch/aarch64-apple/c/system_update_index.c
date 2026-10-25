/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * The shared depot's index classifier: which delivery is a system package. */
#include "system_update_index.h"
#include <string.h>

static int hex64(const char *p, size_t n)
{
    size_t i;
    if (n != 64)
        return 0;
    for (i = 0; i < n; ++i)
        if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f')))
            return 0;
    return 1;
}

/* One index line: four fields, the last a SHA-256; *system set when the
 * name is the system package's. */
static int index_line(const char *line, size_t n, int *system)
{
    const char *field[4];
    size_t length[4], i = 0, k;

    for (k = 0; k < 4; ++k) {
        while (i < n && line[i] == ' ')
            ++i;
        field[k] = line + i;
        while (i < n && line[i] != ' ')
            ++i;
        length[k] = (size_t)(line + i - field[k]);
        if (length[k] == 0)
            return 0;
    }
    while (i < n && line[i] == ' ')
        ++i;
    if (i != n || !hex64(field[3], length[3]))
        return 0;
    for (k = 0; k < 3; ++k)
        for (i = 0; i < length[k]; ++i) {
            char c = field[k][i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '+'))
                return 0;
        }
    if (length[0] == sizeof(SYSTEM_UPDATE_PACKAGE) - 1 &&
            memcmp(field[0], SYSTEM_UPDATE_PACKAGE, length[0]) == 0)
        *system = 1;
    return 1;
}

/* Whether the text names the system package at a line start: a delivery
 * of another kind (a boot generation, an older format) is simply not a
 * candidate, whatever its lines look like. */
static int names_system(const char *text, size_t size)
{
    static const char name[] = SYSTEM_UPDATE_PACKAGE " ";
    size_t i;

    for (i = 0; i + sizeof(name) - 1 <= size; ++i)
        if ((i == 0 || text[i - 1] == '\n') && memcmp(text + i, name, sizeof(name) - 1) == 0)
            return 1;
    return 0;
}

int SystemUpdateIndexKind(const char *text, size_t size)
{
    size_t start = 0, i;
    int system = 0, lines = 0;

    if (size == 0 || !names_system(text, size < SYSTEM_UPDATE_INDEX_MAX ? size : SYSTEM_UPDATE_INDEX_MAX))
        return 0;
    if (size >= SYSTEM_UPDATE_INDEX_MAX)
        return -1;                      /* names it, but not read whole */
    for (i = 0; i <= size; ++i) {
        if (i == size || text[i] == '\n') {
            size_t n = i - start;
            if (n == 0 && i == size)
                break;                  /* the final LF */
            if (n == 0 || !index_line(text + start, n, &system))
                return -1;              /* names it in a malformed index */
            ++lines;
            start = i + 1;
        }
    }
    return lines && system ? 1 : 0;
}
