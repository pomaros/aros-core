/* Copyright (C) 2026 John KNIPPER. AROS Public License 1.1. */
#ifndef AFSCOMMANDS_PARSE_H
#define AFSCOMMANDS_PARSE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Entire nonzero decimal/explicit-hex token, no signs, octal ambiguity or
 * overflow. CLI output is decimal; VIEW also exercises explicit hex input. */
static int afscommands_id(const char *text, size_t length, uint64_t *result)
{
    uint64_t value = 0;
    unsigned base = 10, digit;
    size_t at = 0;
    if (length > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    { base = 16; at = 2; }
    if (at == length) return 0;
    for (; at < length; at++) {
        unsigned char c = (unsigned char)text[at];
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return 0;
        if (digit >= base || value > (UINT64_MAX - digit) / base) return 0;
        value = value * base + digit;
    }
    if (!value) return 0;
    *result = value;
    return 1;
}

static int afscommands_capture(const char *text, size_t length, uint64_t *id)
{
    static const char prefix[] = "AFSPlusBackup: captured snapshot ";
    size_t start = sizeof(prefix) - 1, end;
    if (length <= start || memcmp(text, prefix, start)) return 0;
    end = length;
    if (end && text[end-1] == '\n') end--;
    if (end && text[end-1] == '\r') end--;
    return afscommands_id(text + start, end - start, id);
}

/* Recognize a whole list record rather than an ID substring. Backup
 * LIST emits "snapshot ID"; Snapshot LIST emits "ID generation GEN". */
static int afscommands_list_has(const char *text, size_t length, uint64_t wanted, int backup)
{
    size_t at = 0;
    int found = 0;
    while (at < length) {
        size_t end = at, first, split;
        uint64_t id, generation;
        while (end < length && text[end] != '\n') end++;
        first = at;
        split = end;
        if (split > first && text[split-1] == '\r') split--;
        if (backup) {
            if (split - first < 9 || memcmp(text+first, "snapshot ", 9)) return -1;
            first += 9;
        } else {
            size_t separator = first;
            while (separator < split && text[separator] != ' ') separator++;
            if (split - separator < 12 || memcmp(text+separator, " generation ", 12)) return -1;
            if (!(split-separator-12 == 1 && text[separator+12] == '0') &&
                !afscommands_id(text+separator+12, split-separator-12, &generation)) return -1;
            split = separator;
        }
        if (!afscommands_id(text+first, split-first, &id)) return -1;
        if (id == wanted) found++;
        if (found > 1) return -1;
        at = end + (end < length);
    }
    return found;
}
#endif
