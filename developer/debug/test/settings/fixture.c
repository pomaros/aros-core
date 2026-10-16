/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Host fixture for the schema parser and the IFF codec of
    settings.library: the real Font schema over the real default
    font.prefs, round trips, and the refusals the contract names.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings_schema.h"
#include "settings_iff.h"

static size_t live_blocks;

static void *host_alloc(void *ctx, size_t size)
{
    (void)ctx;
    live_blocks++;
    return calloc(1, size);
}

static void host_free(void *ctx, void *block, size_t size)
{
    (void)ctx; (void)size;
    live_blocks--;
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

int main(int argc, char **argv)
{
    size_t schema_len, image_len, offset = 0;
    char *schema_text, *image_text;
    uint8_t *image;
    struct settings_schema *schema = NULL;
    const struct settings_record *record;
    char err[128], value[SETTINGS_TEXT_MAX];
    int result;

    if (argc != 3) {
        fprintf(stderr, "usage: fixture font.schema font.prefs\n");
        return 2;
    }
    schema_text = read_file(argv[1], &schema_len);
    image_text = read_file(argv[2], &image_len);
    image = (uint8_t *)image_text;

    result = settings_schema_parse(schema_text, schema_len, host_alloc, host_free,
        NULL, &schema, err, sizeof(err));
    CHECK(result == SETTINGS_E_OK && schema != NULL, "font.schema parses");
    if (schema == NULL) { printf("%s\n", err); return 1; }
    CHECK(strcmp(schema->file, "SYS/font.prefs") == 0 && strcmp(schema->stem, "font") == 0,
        "file and stem");
    CHECK(schema->chunk_count == 1 && schema->chunks[0].id == SETTINGS_MAKE_ID('F', 'O', 'N', 'T') &&
        schema->chunks[0].size == 156, "chunk id and size");
    CHECK(schema->chunks[0].has_select && schema->chunks[0].select_offset == 14 &&
        schema->chunks[0].select_size == 2, "selector fp_Type@14:2");
    CHECK(schema->record_count == 12, "twelve records");
    CHECK(strcmp(schema->layout_struct, "FileFontPrefs") == 0, "layout structure");

    record = settings_schema_find(schema, "font/system/size");
    CHECK(record != NULL && record->type == SETTINGS_T_INT && record->offset == 24 &&
        record->size == 2 && record->has_range && record->min == 4 && record->max == 64 &&
        record->select == 1 && record->level == SETTINGS_L_AVERAGE && record->live,
        "font/system/size record");
    CHECK(settings_schema_find(schema, "font/system/colour") == NULL, "unknown key is NULL");

    /* Locate and read the system font. */
    result = settings_iff_locate(image, image_len, schema, record, &offset);
    CHECK(result == SETTINGS_E_OK, "system chunk located");
    CHECK(settings_value_get(image + offset, record, value, sizeof(value)) == SETTINGS_E_OK &&
        strcmp(value, "14") == 0, "system size reads 14");
    record = settings_schema_find(schema, "font/system/name");
    CHECK(record != NULL && settings_iff_locate(image, image_len, schema, record, &offset) == SETTINGS_E_OK &&
        settings_value_get(image + offset, record, value, sizeof(value)) == SETTINGS_E_OK &&
        strcmp(value, "ttcourier.font") == 0, "system name reads ttcourier.font");
    record = settings_schema_find(schema, "font/workbench/size");
    CHECK(record != NULL && settings_iff_locate(image, image_len, schema, record, &offset) == SETTINGS_E_OK &&
        settings_value_get(image + offset, record, value, sizeof(value)) == SETTINGS_E_OK &&
        strcmp(value, "13") == 0, "workbench size reads 13");
    record = settings_schema_find(schema, "font/screen/name");
    CHECK(record != NULL && settings_iff_locate(image, image_len, schema, record, &offset) == SETTINGS_E_OK &&
        settings_value_get(image + offset, record, value, sizeof(value)) == SETTINGS_E_OK &&
        strcmp(value, "arial.font") == 0, "screen name reads arial.font");

    /* Split, rebuild and check: the chunk list codec of ReplaceSettingsA. */
    {
        struct settings_iff_chunk chunks[8], one[1];
        size_t count = 0, size;
        uint8_t *rebuilt, odd[3] = { 1, 2, 3 };
        CHECK(settings_iff_split(image, image_len, chunks, 8, &count) == SETTINGS_E_OK && count == 4,
            "font.prefs splits into PRHD and three FONT chunks");
        CHECK(count == 4 && chunks[0].id == SETTINGS_MAKE_ID('P', 'R', 'H', 'D') &&
            chunks[1].id == SETTINGS_MAKE_ID('F', 'O', 'N', 'T') && chunks[1].size == 156 &&
            chunks[3].data + chunks[3].size == image + image_len, "chunk ids, sizes and positions");
        CHECK(settings_iff_split(image, image_len, chunks, 2, &count) == SETTINGS_E_BUFFER,
            "capacity 2 refused");
        CHECK(settings_iff_split(image, 11, chunks, 8, &count) == SETTINGS_E_FORMAT, "short image refused");
        CHECK(settings_iff_split(image, image_len, chunks, 8, &count) == SETTINGS_E_OK &&
            settings_iff_check(schema, chunks, count) == SETTINGS_E_OK, "sizes pass the schema check");
        size = settings_iff_join_size(chunks, count);
        CHECK(size == image_len, "join size equals the original length");
        rebuilt = malloc(size);
        settings_iff_join(rebuilt, chunks, count);
        CHECK(memcmp(rebuilt, image, image_len) == 0, "split then join is byte-identical");
        free(rebuilt);
        chunks[1].size = 155;
        CHECK(settings_iff_check(schema, chunks, count) == SETTINGS_E_FORMAT, "FONT of 155 bytes refused");
        chunks[1].size = 156;
        one[0].id = SETTINGS_MAKE_ID('T', 'E', 'S', 'T'); one[0].size = 3; one[0].data = odd;
        CHECK(settings_iff_check(schema, one, 1) == SETTINGS_E_OK, "foreign chunk passes");
        size = settings_iff_join_size(one, 1);
        CHECK(size == 12 + 8 + 4, "odd chunk is padded");
        rebuilt = calloc(1, size);
        settings_iff_join(rebuilt, one, 1);
        CHECK(rebuilt[7] == 16 && rebuilt[19] == 3 && rebuilt[20] == 1 && rebuilt[22] == 3 && rebuilt[23] == 0 &&
            settings_iff_split(rebuilt, size, chunks, 8, &count) == SETTINGS_E_OK && count == 1 &&
            chunks[0].size == 3, "padded form splits back to one chunk of 3");
        free(rebuilt);
    }

    /* Round trip on a copy. */
    {
        uint8_t *copy = malloc(image_len);
        size_t sys_offset = 0, wb_offset = 0;
        const struct settings_record *size_record = settings_schema_find(schema, "font/system/size");
        const struct settings_record *wb_size = settings_schema_find(schema, "font/workbench/size");
        const struct settings_record *style = settings_schema_find(schema, "font/workbench/style");
        const struct settings_record *mode = settings_schema_find(schema, "font/workbench/drawmode");
        memcpy(copy, image, image_len);
        CHECK(settings_iff_locate(copy, image_len, schema, size_record, &sys_offset) == SETTINGS_E_OK,
            "copy located");
        CHECK(settings_value_set(copy + sys_offset, size_record, "11") == SETTINGS_E_OK, "set size 11");
        CHECK(settings_value_get(copy + sys_offset, size_record, value, sizeof(value)) == SETTINGS_E_OK &&
            strcmp(value, "11") == 0, "size reads back 11");
        CHECK(copy[sys_offset + 24] == 0 && copy[sys_offset + 25] == 11, "bytes 24..25 hold 11 big-endian");
        CHECK(memcmp(copy, image, sys_offset + 24) == 0 &&
            memcmp(copy + sys_offset + 26, image + sys_offset + 26, image_len - sys_offset - 26) == 0,
            "only the two field bytes changed");
        CHECK(settings_value_set(copy + sys_offset, size_record, "99") == SETTINGS_E_RANGE, "99 refused by range");
        CHECK(settings_value_set(copy + sys_offset, size_record, "x") == SETTINGS_E_RANGE, "text refused");
        CHECK(settings_value_get(copy + sys_offset, size_record, value, sizeof(value)) == SETTINGS_E_OK &&
            strcmp(value, "11") == 0, "refusals leave 11");
        CHECK(settings_iff_locate(copy, image_len, schema, wb_size, &wb_offset) == SETTINGS_E_OK &&
            wb_offset != sys_offset, "workbench chunk differs from system chunk");
        CHECK(settings_value_set(copy + wb_offset, style, "bold,italic") == SETTINGS_E_OK &&
            copy[wb_offset + 26] == 6, "flags bold,italic encode 6");
        CHECK(settings_value_get(copy + wb_offset, style, value, sizeof(value)) == SETTINGS_E_OK &&
            strcmp(value, "bold,italic") == 0, "flags read back by name");
        CHECK(settings_value_set(copy + wb_offset, style, "") == SETTINGS_E_OK && copy[wb_offset + 26] == 0,
            "empty flags clear");
        CHECK(settings_value_set(copy + wb_offset, style, "shadow") == SETTINGS_E_RANGE, "unknown flag refused");
        CHECK(settings_value_set(copy + wb_offset, mode, "complement") == SETTINGS_E_OK &&
            copy[wb_offset + 18] == 2, "enum by name");
        CHECK(settings_value_get(copy + wb_offset, mode, value, sizeof(value)) == SETTINGS_E_OK &&
            strcmp(value, "complement") == 0, "enum reads back by name");
        CHECK(settings_value_set(copy + wb_offset, mode, "7") == SETTINGS_E_RANGE, "enum value outside choices refused");
        {
            const struct settings_record *name = settings_schema_find(schema, "font/workbench/name");
            char longname[140];
            memset(longname, 'a', sizeof(longname) - 1);
            longname[sizeof(longname) - 1] = '\0';
            CHECK(settings_value_set(copy + wb_offset, name, longname) == SETTINGS_E_RANGE, "overlong name refused");
            CHECK(settings_value_set(copy + wb_offset, name, "helvetica.font") == SETTINGS_E_OK &&
                settings_value_get(copy + wb_offset, name, value, sizeof(value)) == SETTINGS_E_OK &&
                strcmp(value, "helvetica.font") == 0 && copy[wb_offset + 28 + 14] == 0,
                "name round trip, NUL padded");
        }
        CHECK(settings_value_get(copy + sys_offset, size_record, value, 2) == SETTINGS_E_BUFFER,
            "short buffer refused");
        free(copy);
    }

    /* Format refusals. */
    {
        uint8_t *bad = malloc(image_len);
        const struct settings_record *size_record = settings_schema_find(schema, "font/system/size");
        memcpy(bad, image, image_len);
        bad[8] = 'X';
        CHECK(settings_iff_locate(bad, image_len, schema, size_record, &offset) == SETTINGS_E_FORMAT,
            "wrong form type refused");
        memcpy(bad, image, image_len);
        CHECK(settings_iff_locate(bad, 100, schema, size_record, &offset) == SETTINGS_E_FORMAT,
            "truncated image refused");
        memcpy(bad, image, image_len);
        bad[0x21] = 0x9b; /* first FONT chunk size 155 */
        CHECK(settings_iff_locate(bad, image_len, schema, size_record, &offset) == SETTINGS_E_FORMAT,
            "chunk of the wrong size refused");
        memcpy(bad, image, image_len);
        bad[0x22 + 156 + 8 + 15] = 7; /* second chunk fp_Type = 7 */
        CHECK(settings_iff_locate(bad, image_len, schema, size_record, &offset) == SETTINGS_E_FORMAT,
            "selector absent refused");
        free(bad);
    }

    /* Schema refusals. */
    {
        static const char *bad_schemas[] = {
            "schema 1\nfile SYS/x.prefs\nchunk FONT size 10\n[x/a]\n  v f@8:4 type int\n",
            "schema 1\nfile SYS/x.prefs\nchunk FONT\n[x/a]\n  v f@0:2 type enum\n",
            "schema 1\nfile SYS/x.prefs\nchunk FONT\n[x/a]\n  v f@0:2\n",
            "schema 2\nfile SYS/x.prefs\nchunk FONT\n",
            "schema 1\nchunk FONT\n[x/a]\n  v f@0:2 type int\n",
            "schema 1\nfile SYS/x.prefs\nchunk FONT\n[x/a]\n  v f@0:2 type int range 9..1\n",
        };
        static const char *labels[] = {
            "field outside the chunk refused", "enum without choices refused",
            "record without type refused", "schema version 2 refused",
            "file line missing refused", "inverted range refused",
        };
        size_t i;
        for (i = 0; i < sizeof(bad_schemas) / sizeof(bad_schemas[0]); i++) {
            struct settings_schema *s = NULL;
            result = settings_schema_parse(bad_schemas[i], strlen(bad_schemas[i]), host_alloc,
                host_free, NULL, &s, err, sizeof(err));
            CHECK(result == SETTINGS_E_FORMAT && s == NULL && strncmp(err, "schema line ", 12) == 0,
                labels[i]);
        }
    }

    /* Quoted chunk ids and several chunks per file. */
    {
        static const char text[] =
            "schema 1\nfile SYS/two.prefs\nlayout x.h A\n"
            "chunk \"ASL \" size 26\nchunk KMSW size 70 layout B\n"
            "[two/a]\n  v f@16:1 type int\n"
            "[two/b] chunk KMSW\n  w g@0:1 type bool\n";
        struct settings_schema *two = NULL;
        result = settings_schema_parse(text, strlen(text), host_alloc, host_free, NULL, &two,
            err, sizeof(err));
        CHECK(result == SETTINGS_E_OK && two != NULL && two->chunk_count == 2 &&
            two->chunks[0].id == SETTINGS_MAKE_ID('A', 'S', 'L', ' ') &&
            two->chunks[1].id == SETTINGS_MAKE_ID('K', 'M', 'S', 'W') &&
            strcmp(two->chunks[1].layout_struct, "B") == 0 &&
            two->record_count == 2 && two->records[0].chunk == 0 && two->records[1].chunk == 1,
            "quoted id and two chunks");
        if (two != NULL)
            settings_schema_free(two, host_free, NULL);
        {
            static const char bad[] = "schema 1\nfile SYS/x.prefs\nchunk FONT\n[x/a] chunk SCRM\n  v f@0:1 type int\n";
            struct settings_schema *s = NULL;
            result = settings_schema_parse(bad, strlen(bad), host_alloc, host_free, NULL, &s, err, sizeof(err));
            CHECK(result == SETTINGS_E_FORMAT && s == NULL, "undeclared group chunk refused");
        }
    }

    settings_schema_free(schema, host_free, NULL);
    CHECK(live_blocks == 0, "every block freed");
    free(schema_text);
    free(image_text);
    printf("settings-schema-host result=%s failures=%u schema=font records=%u\n",
        failures == 0 ? "PASS" : "FAIL", failures, 12U);
    return failures == 0 ? 0 : 1;
}
