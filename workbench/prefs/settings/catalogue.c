/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <string.h>
#include <stdlib.h>
#include "catalogue.h"

static void copy_value(char *dst, size_t size, const char *src, size_t length)
{
    if (length >= size)
        length = size - 1;
    memcpy(dst, src, length);
    dst[length] = '\0';
}

/* Calls field(key, key_length, value, value_length, context) per line;
 * blank lines and lines starting with '#' are skipped. */
static int each_line(const char *text, size_t length,
    int (*field)(const char *, size_t, const char *, size_t, void *), void *context)
{
    size_t position = 0;

    while (position < length) {
        size_t end = position, key_end, value;
        while (end < length && text[end] != '\n')
            end++;
        {
            size_t line_end = end;
            while (line_end > position && (text[line_end - 1] == '\r' ||
                   text[line_end - 1] == ' ' || text[line_end - 1] == '\t'))
                line_end--;
            while (position < line_end && (text[position] == ' ' || text[position] == '\t'))
                position++;
            if (position < line_end && text[position] != '#') {
                key_end = position;
                while (key_end < line_end && text[key_end] != ' ' && text[key_end] != '\t')
                    key_end++;
                value = key_end;
                while (value < line_end && (text[value] == ' ' || text[value] == '\t'))
                    value++;
                if (value >= line_end)
                    return -1;
                if (field(text + position, key_end - position, text + value,
                          line_end - value, context) != 0)
                    return -1;
            }
        }
        position = end + 1;
    }
    return 0;
}

static int key_is(const char *key, size_t length, const char *name)
{
    return strlen(name) == length && memcmp(key, name, length) == 0;
}

static int parse_order(const char *value, size_t length, long *order)
{
    char buffer[16];
    char *end;
    if (length == 0 || length >= sizeof(buffer))
        return -1;
    memcpy(buffer, value, length);
    buffer[length] = '\0';
    *order = strtol(buffer, &end, 10);
    return *end == '\0' ? 0 : -1;
}

static int group_field(const char *key, size_t key_length, const char *value,
    size_t value_length, void *context)
{
    struct cat_group *group = context;
    if (key_is(key, key_length, "id"))
        copy_value(group->id, sizeof(group->id), value, value_length);
    else if (key_is(key, key_length, "name"))
        copy_value(group->name, sizeof(group->name), value, value_length);
    else if (key_is(key, key_length, "description"))
        copy_value(group->description, sizeof(group->description), value, value_length);
    else if (key_is(key, key_length, "order"))
        return parse_order(value, value_length, &group->order);
    /* unknown keys are left for later versions */
    return 0;
}

static int row_field(const char *key, size_t key_length, const char *value,
    size_t value_length, void *context)
{
    struct cat_row *row = context;
    if (key_is(key, key_length, "group"))
        copy_value(row->group, sizeof(row->group), value, value_length);
    else if (key_is(key, key_length, "class"))
        copy_value(row->class_name, sizeof(row->class_name), value, value_length);
    else if (key_is(key, key_length, "name"))
        copy_value(row->name, sizeof(row->name), value, value_length);
    else if (key_is(key, key_length, "description"))
        copy_value(row->description, sizeof(row->description), value, value_length);
    else if (key_is(key, key_length, "keywords"))
        copy_value(row->keywords, sizeof(row->keywords), value, value_length);
    else if (key_is(key, key_length, "order"))
        return parse_order(value, value_length, &row->order);
    else if (key_is(key, key_length, "level")) {
        if (key_is(value, value_length, "novice"))       row->level = CAT_NOVICE;
        else if (key_is(value, value_length, "average")) row->level = CAT_AVERAGE;
        else if (key_is(value, value_length, "expert"))  row->level = CAT_EXPERT;
        else return -1;
    }
    return 0;
}

int cat_parse_group(const char *text, size_t length, struct cat_group *group)
{
    memset(group, 0, sizeof(*group));
    if (each_line(text, length, group_field, group) != 0)
        return -1;
    return group->id[0] != '\0' && group->name[0] != '\0' ? 0 : -1;
}

int cat_parse_row(const char *text, size_t length, struct cat_row *row)
{
    memset(row, 0, sizeof(*row));
    row->level = CAT_AVERAGE;
    if (each_line(text, length, row_field, row) != 0)
        return -1;
    return row->group[0] != '\0' && row->class_name[0] != '\0' && row->name[0] != '\0' ? 0 : -1;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }

static int compare_names(const char *a, const char *b)
{
    while (*a != '\0' && lower(*a) == lower(*b)) { a++; b++; }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

static int compare_groups(const void *x, const void *y)
{
    const struct cat_group *a = x, *b = y;
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return compare_names(a->name, b->name);
}

static int compare_rows(const void *x, const void *y)
{
    const struct cat_row *a = x, *b = y;
    int group = strcmp(a->group, b->group);
    if (group != 0)
        return group;
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return compare_names(a->name, b->name);
}

int cat_group_index(const struct catalogue *catalogue, const char *id)
{
    size_t i;
    for (i = 0; i < catalogue->group_count; i++)
        if (strcmp(catalogue->groups[i].id, id) == 0)
            return (int)i;
    return -1;
}

void cat_finish(struct catalogue *catalogue)
{
    size_t i;
    qsort(catalogue->groups, catalogue->group_count, sizeof(catalogue->groups[0]), compare_groups);
    qsort(catalogue->rows, catalogue->row_count, sizeof(catalogue->rows[0]), compare_rows);
    catalogue->orphan_rows = 0;
    for (i = 0; i < catalogue->row_count; i++)
        if (cat_group_index(catalogue, catalogue->rows[i].group) < 0)
            catalogue->orphan_rows++;
}

static int contains(const char *haystack, const char *needle)
{
    size_t i, j;
    if (needle[0] == '\0')
        return 1;
    for (i = 0; haystack[i] != '\0'; i++) {
        for (j = 0; needle[j] != '\0' && haystack[i + j] != '\0' &&
             lower((unsigned char)haystack[i + j]) == lower((unsigned char)needle[j]); j++)
            ;
        if (needle[j] == '\0')
            return 1;
    }
    return 0;
}

int cat_row_matches(const struct catalogue *catalogue, const struct cat_row *row,
    const char *query)
{
    int group = cat_group_index(catalogue, row->group);
    return contains(row->name, query) || contains(row->description, query) ||
           contains(row->keywords, query) ||
           (group >= 0 && contains(catalogue->groups[group].name, query));
}
