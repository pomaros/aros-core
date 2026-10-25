#ifndef SETTINGS_CATALOGUE_H
#define SETTINGS_CATALOGUE_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    The settings window's catalogue: one file per group in
    SYS:Prefs/Index/Groups and one per row in SYS:Prefs/Index/Rows,
    so a package that installs an editor adds its row by adding a file.
    Plain C over a text buffer: the host test parses the same files.

    A group file:                 A row file:
        id appearance                 group appearance
        order 10                      order 10
        name Appearance               class FontEditor.mcc
        description How the ...       name Fonts
                                      description The fonts of ...
                                      keywords text size type face
                                      level average
*/
#include <stddef.h>

#define CAT_ID      32
#define CAT_NAME    64
#define CAT_TEXT    160
#define CAT_GROUPS  16
#define CAT_ROWS    64

struct cat_group {
    char id[CAT_ID];
    char name[CAT_NAME];
    char description[CAT_TEXT];
    long order;
};

#define CAT_NOVICE  0
#define CAT_AVERAGE 1
#define CAT_EXPERT  2

struct cat_row {
    char id[CAT_ID];              /* the row file's name: SHOW PAGE <id> */
    int  level;                   /* CAT_NOVICE..CAT_EXPERT, default average */
    char group[CAT_ID];
    char class_name[CAT_NAME];
    char name[CAT_NAME];
    char description[CAT_TEXT];
    char keywords[CAT_TEXT];
    long order;
};

struct catalogue {
    struct cat_group groups[CAT_GROUPS];
    size_t group_count;
    struct cat_row rows[CAT_ROWS];
    size_t row_count;
    size_t orphan_rows;           /* rows naming a group that does not exist */
};

/* Parse one file's text into a group or a row. Returns 0, or -1 when a
 * required field (id/name for a group; group/class/name for a row) is
 * missing or a line is not "<key> <value>". */
int cat_parse_group(const char *text, size_t length, struct cat_group *group);
int cat_parse_row(const char *text, size_t length, struct cat_row *row);

/* Sort groups and rows by order, then name, and count the rows whose group
 * is unknown; those stay searchable but appear in no group. */
void cat_finish(struct catalogue *catalogue);

/* Index of the group with this id, or -1. */
int cat_group_index(const struct catalogue *catalogue, const char *id);

/* Case-insensitive substring search over a row's name, description and
 * keywords, and its group's name. An empty query matches everything. */
/* The row a page request names: its id, its class with or without
 * ".mcc" or "Editor.mcc", or its name, ignoring case; -1 when none. */
int cat_find_row(const struct catalogue *catalogue, const char *what);

int cat_row_matches(const struct catalogue *catalogue, const struct cat_row *row,
    const char *query);

#endif /* SETTINGS_CATALOGUE_H */
