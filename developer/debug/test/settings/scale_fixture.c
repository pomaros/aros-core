/*
    Copyright (C) 2026 John Knipper.
    Distributed under the AROS Public License, version 1.1.

    Host fixture for the production display scale schema and IFF codec.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "settings_schema.h"
#include "settings_iff.h"

static void *alloc(void *context, size_t size)
{
    (void)context;
    return calloc(1, size);
}

static void release(void *context, void *block, size_t size)
{
    (void)context;
    (void)size;
    free(block);
}

int main(int argc, char **argv)
{
    static const char *bad[] = {"0", "99", "101", "149", "401", "425", "-1", "junk"};
    FILE *file;
    long length;
    char *text, error[256], value[32], output[32];
    struct settings_schema *schema = NULL;
    const struct settings_record *record;
    unsigned char chunk[28], before[28];
    int i, result;

    assert(argc == 2);
    file = fopen(argv[1], "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    assert(length > 0);
    rewind(file);
    text = malloc(length + 1);
    assert(text);
    assert(fread(text, 1, length, file) == (size_t)length);
    fclose(file);
    result = settings_schema_parse(text, length, alloc, release, NULL,
        &schema, error, sizeof(error));
    if (result)
    {
        fprintf(stderr, "%s\n", error);
        free(text);
        return 1;
    }
    record = settings_schema_find(schema, "screenmode/workbench/scale");
    assert(record && record->offset == 0 && record->size == 4);
    assert(record->live && record->choice_count == 13);
    for (i = 100; i <= 400; i += 25)
    {
        int j;
        memset(chunk, 0xa5, sizeof(chunk));
        snprintf(value, sizeof(value), "%d", i);
        assert(settings_value_set(chunk, record, value) == SETTINGS_E_OK);
        assert(settings_field_read(chunk, 0, 4) == (unsigned)i);
        assert(settings_value_get(chunk, record, output, sizeof(output)) == SETTINGS_E_OK);
        assert(strcmp(value, output) == 0);
        for (j = 4; j < 28; j++) assert(chunk[j] == 0xa5);
    }
    for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++)
    {
        memset(chunk, 0xa5, sizeof(chunk));
        memcpy(before, chunk, sizeof(chunk));
        assert(settings_value_set(chunk, record, bad[i]) == SETTINGS_E_RANGE);
        assert(!memcmp(chunk, before, sizeof(chunk)));
    }
    settings_schema_free(schema, release, NULL);
    free(text);
    puts("display-scale-schema PASS 13 values, 8 negatives, BE bytes, live schema, untouched mode fields");
    return 0;
}
