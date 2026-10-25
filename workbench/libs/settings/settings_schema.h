#ifndef SETTINGS_SCHEMA_H
#define SETTINGS_SCHEMA_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Schema records and the parser of SYS:Prefs/Schemas/<file>.schema.
    Plain C over <stdint.h>: the host fixture compiles this file as is.
*/
#include <stddef.h>
#include <stdint.h>

/* Same numbers as SETTING_ERROR_* in <libraries/settings.h>. */
#define SETTINGS_E_OK          0
#define SETTINGS_E_UNKNOWN     1
#define SETTINGS_E_NO_SCHEMA   2
#define SETTINGS_E_NO_FILE     3
#define SETTINGS_E_FORMAT      4
#define SETTINGS_E_RANGE       5
#define SETTINGS_E_BUFFER      6
#define SETTINGS_E_IO          7
#define SETTINGS_E_UNSUPPORTED 8
#define SETTINGS_E_MEMORY      9

/* Same numbers as SETTING_TYPE_* / SETTING_LEVEL_* / SETTING_APPLIER_*. */
enum settings_type {
    SETTINGS_T_BOOL = 1, SETTINGS_T_INT, SETTINGS_T_ENUM, SETTINGS_T_FLAGS,
    SETTINGS_T_STRING, SETTINGS_T_FONT, SETTINGS_T_PEN, SETTINGS_T_RGB,
    SETTINGS_T_MODEID, SETTINGS_T_PATH, SETTINGS_T_KEYMAP, SETTINGS_T_COUNTRY
};
enum settings_level { SETTINGS_L_NOVICE = 0, SETTINGS_L_AVERAGE, SETTINGS_L_EXPERT };
enum settings_applier { SETTINGS_A_IPREFS = 0, SETTINGS_A_MUIMASTER, SETTINGS_A_SELF };

#define SETTINGS_KEY_MAX     64
#define SETTINGS_FIELD_MAX   48
#define SETTINGS_NAME_MAX    32
#define SETTINGS_TEXT_MAX    64
#define SETTINGS_ITEM_MAX    256   /* largest value of an items record   */
#define SETTINGS_PATH_MAX    96

struct settings_choice {
    char    name[SETTINGS_NAME_MAX];
    int32_t value;
};

struct settings_record {
    char     key[SETTINGS_KEY_MAX];       /* "font/system/size"              */
    char     field[SETTINGS_FIELD_MAX];   /* on-disk member, documentation   */
    uint32_t offset;                      /* byte offset in the chunk        */
    uint32_t size;                        /* field size in bytes             */
    uint8_t  type;                        /* enum settings_type              */
    uint8_t  level;                       /* enum settings_level             */
    uint8_t  applier;                     /* enum settings_applier           */
    uint8_t  live;                        /* 1: applied on Use               */
    uint8_t  has_range;
    uint8_t  has_default;
    uint8_t  has_select;                  /* chunk selector applies          */
    uint8_t  reserved;
    int32_t  min;
    int32_t  max;
    uint32_t select;                      /* selector value of the group     */
    uint32_t mask;                        /* bits of the field, 0: all of it */
    uint32_t chunk;                       /* index into schema->chunks       */
    char     unit[SETTINGS_NAME_MAX];
    char     dflt[SETTINGS_TEXT_MAX];
    struct settings_choice *choices;
    uint32_t choice_count;
    const char **choice_names;            /* choice_count entries into choices */
    int32_t     *choice_values;
};

#define SETTINGS_CHUNKS_MAX 8

/* A chunk is a structure (fields at fixed offsets) or a list of items
 * (id, length, data records, as Zune's dataspace writes them). In an items
 * chunk a record's name@offset:size reads as define@id:size: the item id,
 * the name of its C define (checked by the schema gate), and the value's
 * size, four bytes for a number, the maximum with the terminating zero for
 * a string. */
#define SETTINGS_CHUNK_STRUCT 0
#define SETTINGS_CHUNK_ITEMS  1

struct settings_chunk {
    uint32_t id;                           /* IFF id, SETTINGS_MAKE_ID       */
    uint32_t size;                         /* 0: unchecked                   */
    uint8_t  has_select;
    uint8_t  kind;                         /* SETTINGS_CHUNK_*               */
    uint8_t  reserved[2];
    uint32_t select_offset;
    uint32_t select_size;
    char     layout_struct[SETTINGS_FIELD_MAX]; /* overrides the schema's    */
};

struct settings_schema {
    uint32_t version;
    char     file[SETTINGS_TEXT_MAX];      /* "SYS/font.prefs"               */
    char     stem[SETTINGS_NAME_MAX];      /* "font": first key component    */
    char     layout_file[SETTINGS_PATH_MAX];
    char     layout_struct[SETTINGS_FIELD_MAX];
    struct settings_chunk chunks[SETTINGS_CHUNKS_MAX];
    uint32_t chunk_count;
    struct settings_record *records;
    uint32_t record_count;
};

typedef void *(*settings_alloc_fn)(void *ctx, size_t size);
typedef void  (*settings_free_fn)(void *ctx, void *block, size_t size);

/* Parses text (not required to be NUL-terminated). On success *out is the
 * schema; on failure err holds one line naming the schema line number. */
int settings_schema_parse(const char *text, size_t length,
    settings_alloc_fn alloc, settings_free_fn release, void *ctx,
    struct settings_schema **out, char *err, size_t err_size);

void settings_schema_free(struct settings_schema *schema,
    settings_free_fn release, void *ctx);

/* Exact key match, "stem/group/name". NULL when absent. */
const struct settings_record *settings_schema_find(
    const struct settings_schema *schema, const char *key);

/* Builds "FONT" style ids from four characters. */
#define SETTINGS_MAKE_ID(a, b, c, d) \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | \
     ((uint32_t)(uint8_t)(c) << 8) | (uint32_t)(uint8_t)(d))

#endif /* SETTINGS_SCHEMA_H */
