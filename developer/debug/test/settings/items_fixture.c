/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Host fixture for items chunks: the real Zune schema over a
    zune/global.prefs image built here the way muimaster writes it.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings_schema.h"
#include "settings_iff.h"

static unsigned failures;
static size_t live;
#define CHECK(c, label) do { if (c) printf("ok   %s\n", label); \
    else { printf("FAIL %s (line %d)\n", label, __LINE__); failures++; } } while (0)

static void *host_alloc(void *ctx, size_t size) { (void)ctx; live++; return calloc(1, size); }
static void host_free(void *ctx, void *block, size_t size) { (void)ctx; (void)size; live--; free(block); }

static void put32(unsigned char *p, unsigned v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }

/* FORM PREF, then MUIC with items: 0x01=4, 0x1e="XEN/8", 0x0a=1. */
static size_t build(unsigned char *out)
{
    unsigned char items[64];
    size_t n = 0, total;
    put32(items + n, 0x01); put32(items + n + 4, 4); put32(items + n + 8, 4); n += 12;
    put32(items + n, 0x1e); put32(items + n + 4, 6); memcpy(items + n + 8, "XEN/8", 6); n += 14;
    put32(items + n, 0x0a); put32(items + n + 4, 4); put32(items + n + 8, 1); n += 12;
    memcpy(out, "FORM", 4); memcpy(out + 8, "PREF", 4);
    memcpy(out + 12, "MUIC", 4); put32(out + 16, (unsigned)n); memcpy(out + 20, items, n);
    total = 20 + n + (n & 1);
    put32(out + 4, (unsigned)(total - 8));
    return total;
}

static char *slurp(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb"); char *d; long n;
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc((size_t)n + 1);
    if (fread(d, 1, (size_t)n, f) != (size_t)n) exit(2);
    fclose(f); d[n] = 0; *length = (size_t)n; return d;
}

int main(int argc, char **argv)
{
    struct settings_schema *schema = NULL;
    unsigned char image[256], *out = NULL, *out2 = NULL;
    size_t length, schema_length, out_length = 0, out2_length = 0;
    char err[160], value[256], *text;
    const struct settings_record *left, *font, *refresh, *top, *leading, *title;

    if (argc != 2) { fprintf(stderr, "usage: items_fixture zune.schema\n"); return 2; }
    text = slurp(argv[1], &schema_length);
    CHECK(settings_schema_parse(text, schema_length, host_alloc, host_free, NULL, &schema,
          err, sizeof(err)) == SETTINGS_E_OK, "zune.schema parses");
    if (schema == NULL) { printf("%s\n", err); return 1; }
    CHECK(schema->chunk_count == 1 && schema->chunks[0].kind == SETTINGS_CHUNK_ITEMS,
          "one items chunk");
    CHECK(strcmp(schema->stem, "zune") == 0 && strcmp(schema->file, "zune/global.prefs") == 0,
          "stem zune names keys of zune/global.prefs");
    left = settings_schema_find(schema, "zune/window/spacing-left");
    font = settings_schema_find(schema, "zune/fonts/normal");
    refresh = settings_schema_find(schema, "zune/listview/refresh");
    top = settings_schema_find(schema, "zune/window/spacing-top");
    leading = settings_schema_find(schema, "zune/listview/font-leading");
    title = settings_schema_find(schema, "zune/fonts/title");
    CHECK(left && font && refresh && top && leading && title, "records found");
    if (!(left && font && refresh && top && leading && title)) return 1;
    CHECK(left->offset == 0x01 && left->size == 4 && font->offset == 0x1e, "ids and sizes");

    length = build(image);
    CHECK(settings_record_get(image, length, schema, left, value, sizeof(value)) == SETTINGS_E_OK &&
          strcmp(value, "4") == 0, "number item reads 4");
    CHECK(settings_record_get(image, length, schema, font, value, sizeof(value)) == SETTINGS_E_OK &&
          strcmp(value, "XEN/8") == 0, "text item reads XEN/8");
    CHECK(settings_record_get(image, length, schema, refresh, value, sizeof(value)) == SETTINGS_E_OK &&
          strcmp(value, "mixed") == 0, "enum item reads its choice name");
    CHECK(settings_record_get(image, length, schema, top, value, sizeof(value)) == SETTINGS_E_OK &&
          strcmp(value, "3") == 0, "absent item reads its default");
    CHECK(settings_record_get(image, length, schema, title, value, sizeof(value)) == SETTINGS_E_NO_FILE,
          "absent item without default is NO_FILE");

    /* A number in place of the same length: a new image, the rest kept. */
    CHECK(settings_record_set(image, length, schema, left, "9", host_alloc, host_free, NULL,
          &out, &out_length) == SETTINGS_E_OK && out != NULL && out_length == length,
          "number item rewritten, same length");
    CHECK(out && settings_record_get(out, out_length, schema, left, value, sizeof(value)) == SETTINGS_E_OK &&
          strcmp(value, "9") == 0, "number reads back 9");
    CHECK(out && settings_record_get(out, out_length, schema, font, value, sizeof(value)) == SETTINGS_E_OK &&
          strcmp(value, "XEN/8") == 0, "neighbour item unchanged");
    CHECK(settings_record_set(image, length, schema, left, "99", host_alloc, host_free, NULL,
          &out2, &out2_length) == SETTINGS_E_RANGE && out2 == NULL, "out of range refused, nothing built");
    if (out) host_free(NULL, out, out_length);

    /* A longer text grows the chunk and the FORM. */
    out = NULL;
    CHECK(settings_record_set(image, length, schema, font, "DejaVuSans/13", host_alloc, host_free,
          NULL, &out, &out_length) == SETTINGS_E_OK && out_length == length + 8,
          "longer text grows the image");
    CHECK(out && settings_record_get(out, out_length, schema, font, value, sizeof(value)) == SETTINGS_E_OK &&
          strcmp(value, "DejaVuSans/13") == 0, "longer text reads back");
    CHECK(out && settings_record_get(out, out_length, schema, refresh, value, sizeof(value)) == SETTINGS_E_OK &&
          strcmp(value, "mixed") == 0, "item after the grown one intact");
    if (out) host_free(NULL, out, out_length);

    /* An absent item is appended. */
    out = NULL;
    CHECK(settings_record_set(image, length, schema, leading, "2", host_alloc, host_free, NULL,
          &out, &out_length) == SETTINGS_E_OK && out_length == length + 12,
          "absent item appended");
    CHECK(out && settings_record_get(out, out_length, schema, leading, value, sizeof(value)) == SETTINGS_E_OK &&
          strcmp(value, "2") == 0, "appended item reads back");
    if (out) host_free(NULL, out, out_length);

    /* Malformed images. */
    {
        unsigned char bad[256];
        memcpy(bad, image, length);
        put32(bad + 24, 400);       /* first item's length runs past the chunk */
        CHECK(settings_record_get(bad, length, schema, font, value, sizeof(value)) == SETTINGS_E_FORMAT,
              "item length past the chunk refused");
        memcpy(bad, image, length);
        put32(bad + 24, 3);         /* a number of three bytes */
        CHECK(settings_record_get(bad, length, schema, left, value, sizeof(value)) == SETTINGS_E_FORMAT,
              "number of the wrong size refused");
        memcpy(bad, image, length);
        bad[20 + 12 + 8 + 5] = 'x'; /* text without its zero */
        CHECK(settings_record_get(bad, length, schema, font, value, sizeof(value)) == SETTINGS_E_FORMAT,
              "text without its zero refused");
        memcpy(bad, "FORM\0\0\0\4PREF", 12);
        CHECK(settings_record_set(bad, 12, schema, left, "5", host_alloc, host_free, NULL, &out, &out_length)
              == SETTINGS_E_FORMAT, "file without the chunk refused");
    }
    {
        static const char refused[] = "schema 1\nfile x\nchunk MUIC kind items size 8\n[z/a]\n  b X@1:4 type int\n";
        struct settings_schema *s2 = NULL;
        CHECK(settings_schema_parse(refused, strlen(refused), host_alloc, host_free, NULL, &s2, err, sizeof(err))
              != SETTINGS_E_OK, "items chunk with a size refused");
        static const char refused2[] = "schema 1\nfile x\nchunk MUIC kind items\n[z/a]\n  b X@1:2 type int\n";
        CHECK(settings_schema_parse(refused2, strlen(refused2), host_alloc, host_free, NULL, &s2, err, sizeof(err))
              != SETTINGS_E_OK, "two-byte number item refused");
    }
    settings_schema_free(schema, host_free, NULL);
    free(text);
    CHECK(live == 0, "every block freed");
    printf("settings-items-host result=%s failures=%u\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
