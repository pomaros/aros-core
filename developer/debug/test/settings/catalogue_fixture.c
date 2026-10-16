/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Host fixture for the settings window's catalogue: the real group and
    row files shipped in workbench/prefs/settings, plus the refusals.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "catalogue.h"

static unsigned failures;
#define CHECK(c, label) do { if (c) printf("ok   %s\n", label); \
    else { printf("FAIL %s (line %d)\n", label, __LINE__); failures++; } } while (0)

static char *slurp(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long n;
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    data = malloc((size_t)n + 1);
    if (fread(data, 1, (size_t)n, f) != (size_t)n) { perror(path); exit(2); }
    fclose(f); data[n] = '\0'; *length = (size_t)n;
    return data;
}

static size_t load_dir(struct catalogue *c, const char *dir, int rows)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t loaded = 0;
    char path[1024];
    if (!d) { perror(dir); exit(2); }
    while ((e = readdir(d)) != NULL) {
        size_t length;
        char *text;
        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        text = slurp(path, &length);
        if (rows) {
            if (c->row_count < CAT_ROWS && cat_parse_row(text, length, &c->rows[c->row_count]) == 0)
                { c->row_count++; loaded++; }
            else
                printf("FAIL row file %s\n", e->d_name), failures++;
        } else {
            if (c->group_count < CAT_GROUPS && cat_parse_group(text, length, &c->groups[c->group_count]) == 0)
                { c->group_count++; loaded++; }
            else
                printf("FAIL group file %s\n", e->d_name), failures++;
        }
        free(text);
    }
    closedir(d);
    return loaded;
}

int main(int argc, char **argv)
{
    static struct catalogue c;
    struct cat_group g;
    struct cat_row r;
    size_t i;
    char groups[1024], rows[1024];

    if (argc != 2) { fprintf(stderr, "usage: catalogue_fixture SETTINGS_DIR\n"); return 2; }
    snprintf(groups, sizeof(groups), "%s/Groups", argv[1]);
    snprintf(rows, sizeof(rows), "%s/Rows", argv[1]);
    CHECK(load_dir(&c, groups, 0) >= 3, "shipped groups parse");
    CHECK(load_dir(&c, rows, 1) >= 1, "shipped rows parse");
    cat_finish(&c);
    CHECK(c.orphan_rows == 0, "every shipped row names a shipped group");
    for (i = 1; i < c.group_count; i++)
        CHECK(c.groups[i - 1].order <= c.groups[i].order, "groups sorted by order");
    i = 0;
    while (i < c.row_count && strcmp(c.rows[i].class_name, "FontEditor.mcc") != 0) i++;
    CHECK(i < c.row_count, "the Font row is present");
    if (i < c.row_count) {
        CHECK(cat_row_matches(&c, &c.rows[i], "font"), "query 'font' finds Fonts");
        CHECK(cat_row_matches(&c, &c.rows[i], "TYPEFACE") || cat_row_matches(&c, &c.rows[i], "TYPE"),
              "a keyword finds Fonts, case-insensitive");
        CHECK(cat_row_matches(&c, &c.rows[i], ""), "empty query matches");
        CHECK(!cat_row_matches(&c, &c.rows[i], "zzzz"), "unrelated query misses");
    }
    /* Refusals and edge cases. */
    CHECK(cat_parse_group("name X\n", strlen("name X\n"), &g) != 0, "group without id refused");
    CHECK(cat_parse_row("group a\nname B\n", strlen("group a\nname B\n"), &r) != 0, "row without class refused");
    CHECK(cat_parse_row("group a\nclass\nname B\n", strlen("group a\nclass\nname B\n"), &r) != 0, "key without value refused");
    CHECK(cat_parse_row("group a\norder x1\nclass C.mcc\nname B\n", strlen("group a\norder x1\nclass C.mcc\nname B\n"), &r) != 0, "bad order refused");
    CHECK(cat_parse_row("# c\r\ngroup  a \r\n\nclass C.mcc\nname  B  b\n", strlen("# c\r\ngroup  a \r\n\nclass C.mcc\nname  B  b\n"), &r) == 0 &&
          strcmp(r.group, "a") == 0 && strcmp(r.name, "B  b") == 0, "comments, CRLF and spacing");
    {
        static struct catalogue o;
        o.group_count = 1; strcpy(o.groups[0].id, "a"); strcpy(o.groups[0].name, "A");
        o.row_count = 1; strcpy(o.rows[0].group, "missing"); strcpy(o.rows[0].class_name, "X.mcc");
        strcpy(o.rows[0].name, "Lost");
        cat_finish(&o);
        CHECK(o.orphan_rows == 1, "a row with an unknown group is counted as orphan");
        CHECK(cat_row_matches(&o, &o.rows[0], "lost"), "an orphan row stays searchable");
    }
    printf("settings-catalogue-host result=%s failures=%u groups=%zu rows=%zu\n",
           failures ? "FAIL" : "PASS", failures, c.group_count, c.row_count);
    return failures ? 1 : 0;
}
