/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Host fixture for masked records: the real Asl schema describes the
    position and the size of ap_SizePosition as two settings of one byte.
    Each reads and writes its own bits and leaves the others alone.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings_schema.h"
#include "settings_iff.h"

static void *host_alloc(void *ctx, size_t size)
{
    (void)ctx;
    return calloc(1, size);
}

static void host_free(void *ctx, void *block, size_t size)
{
    (void)ctx; (void)size;
    free(block);
}

static unsigned failures;

#define CHECK(condition, label) do { \
    if (condition) printf("ok   %s\n", label); \
    else { printf("FAIL %s (line %d)\n", label, __LINE__); failures++; } \
} while (0)

static char *read_file(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long n;
    if (f == NULL) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)n + 1);
    if (fread(data, 1, (size_t)n, f) != (size_t)n) { perror(path); exit(2); }
    fclose(f);
    data[n] = '\0';
    *length = (size_t)n;
    return data;
}

static int reads(const uint8_t *chunk, const struct settings_record *record, const char *expected)
{
    char value[SETTINGS_TEXT_MAX];
    return settings_value_get(chunk, record, value, sizeof(value)) == SETTINGS_E_OK &&
        strcmp(value, expected) == 0;
}

static int refuses(const char *text)
{
    struct settings_schema *schema = NULL;
    char err[128];
    int result = settings_schema_parse(text, strlen(text), host_alloc, host_free,
        NULL, &schema, err, sizeof(err));
    if (schema != NULL)
        settings_schema_free(schema, host_free, NULL);
    return result != SETTINGS_E_OK;
}

int main(int argc, char **argv)
{
    size_t length;
    char *text, err[128];
    struct settings_schema *schema = NULL;
    const struct settings_record *position, *size;
    uint8_t chunk[26];

    if (argc != 2) {
        fprintf(stderr, "usage: mask_fixture asl.schema\n");
        return 2;
    }
    text = read_file(argv[1], &length);
    CHECK(settings_schema_parse(text, length, host_alloc, host_free, NULL, &schema,
        err, sizeof(err)) == SETTINGS_E_OK && schema != NULL, "asl.schema parses");
    if (schema == NULL) { printf("%s\n", err); return 1; }

    position = settings_schema_find(schema, "asl/requester/position");
    size = settings_schema_find(schema, "asl/requester/size");
    CHECK(position != NULL && position->offset == 19 && position->size == 1 &&
        position->mask == 0x07, "position is bits 0x07 of byte 19");
    CHECK(size != NULL && size->offset == 19 && size->size == 1 && size->mask == 0x10,
        "size is bit 0x10 of byte 19");
    if (position == NULL || size == NULL)
        return 1;

    /* The editor's default: override, relative, centred on the screen. */
    memset(chunk, 0, sizeof(chunk));
    chunk[19] = 0x40 | 0x10 | 0x02;
    CHECK(reads(chunk, position, "centerscreen"), "0x52 reads position centerscreen");
    CHECK(reads(chunk, size, "relative"), "0x52 reads size relative");

    CHECK(settings_value_set(chunk, position, "pointer") == SETTINGS_E_OK && chunk[19] == 0x55,
        "position pointer keeps the size and override bits");
    CHECK(settings_value_set(chunk, size, "default") == SETTINGS_E_OK && chunk[19] == 0x45,
        "size default keeps the position and override bits");
    CHECK(reads(chunk, position, "pointer") && reads(chunk, size, "default"), "both read back");
    CHECK(settings_value_set(chunk, position, "3") == SETTINGS_E_OK && chunk[19] == 0x43 &&
        reads(chunk, position, "topleftwindow"), "position by number");

    chunk[19] = 0x43;
    CHECK(settings_value_set(chunk, position, "7") == SETTINGS_E_RANGE && chunk[19] == 0x43,
        "a value outside the choices is refused untouched");
    CHECK(settings_value_set(chunk, position, "sideways") == SETTINGS_E_RANGE && chunk[19] == 0x43,
        "an unknown choice is refused untouched");

    CHECK(refuses("schema 1\nfile SYS/x.prefs\nchunk \"TEST\" size 2\n[x/y]\n"
        "  a  f@0:1  mask 0x100  type int\n"), "a mask wider than its field is refused");
    CHECK(refuses("schema 1\nfile SYS/x.prefs\nchunk \"TEST\" size 2\n[x/y]\n"
        "  a  f@0:1  mask 0  type int\n"), "a zero mask is refused");

    settings_schema_free(schema, host_free, NULL);
    free(text);
    printf("settings-mask-host result=%s failures=%u\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
