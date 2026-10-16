/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Schema parser. One header block, then groups:

        schema   1
        file     SYS/font.prefs
        layout   workbench/prefs/font/prefs.h FileFontPrefs
        chunk    FONT  size 156  select fp_Type@14:2

        [font/system]  select 1  level average  applier iprefs live
          size  fp_TextAttr_ta_YSize@24:2  type int  range 4..64  unit points

    A file with several chunks repeats the chunk line, each with its own
    size, selector and, when the on-disk structure differs, "layout
    Structure"; a group then names its chunk with "chunk XXXX" (the first
    chunk line is the default). A group line sets the defaults of its
    records; a record line names the setting, its field with @offset:size,
    then keyword pairs in any order.
*/
#include "settings_schema.h"

struct parse_state {
    const char *cursor;
    const char *end;
    unsigned    line;
    char       *err;
    size_t      err_size;
    int         explicit_stem;   /* a stem line came before the file line */
};

static size_t text_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0')
        length++;
    return length;
}

static void copy_text(char *dst, size_t dst_size, const char *src, size_t n)
{
    size_t i;
    if (n >= dst_size)
        n = dst_size - 1;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
    dst[n] = '\0';
}

static int same(const char *a, size_t n, const char *literal)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (literal[i] == '\0' || literal[i] != a[i])
            return 0;
    return literal[n] == '\0';
}

static int fail(struct parse_state *state, const char *message)
{
    if (state->err != NULL && state->err_size > 0) {
        char prefix[32];
        unsigned value = state->line;
        size_t n = 0, i;
        char digits[12];
        size_t d = 0;
        const char *text = "schema line ";
        while (text[n] != '\0' && n < sizeof(prefix) - 1) {
            prefix[n] = text[n];
            n++;
        }
        if (value == 0)
            digits[d++] = '0';
        while (value != 0 && d < sizeof(digits)) {
            digits[d++] = (char)('0' + value % 10);
            value /= 10;
        }
        while (d > 0 && n < sizeof(prefix) - 1)
            prefix[n++] = digits[--d];
        if (n < sizeof(prefix) - 2) {
            prefix[n++] = ':';
            prefix[n++] = ' ';
        }
        prefix[n] = '\0';
        copy_text(state->err, state->err_size, prefix, n);
        for (i = n; i < state->err_size - 1 && message[i - n] != '\0'; i++)
            state->err[i] = message[i - n];
        state->err[i] = '\0';
    }
    return SETTINGS_E_FORMAT;
}

/* Tokens are whitespace-separated words of one line. */
struct line {
    const char *word[24];
    size_t      length[24];
    unsigned    count;
};

static int next_line(struct parse_state *state, struct line *out)
{
    for (;;) {
        const char *p = state->cursor;
        const char *line_end;
        unsigned count = 0;
        if (p >= state->end)
            return 0;
        line_end = p;
        while (line_end < state->end && *line_end != '\n')
            line_end++;
        state->cursor = line_end < state->end ? line_end + 1 : line_end;
        state->line++;
        while (p < line_end) {
            const char *start;
            while (p < line_end && (*p == ' ' || *p == '\t' || *p == '\r'))
                p++;
            if (p >= line_end || *p == ';' || *p == '#')
                break;
            if (*p == '"') {
                /* A quoted word keeps its spaces: chunk "ASL " */
                start = p + 1;
                p = start;
                while (p < line_end && *p != '"')
                    p++;
                if (count < 24) {
                    out->word[count] = start;
                    out->length[count] = (size_t)(p - start);
                    count++;
                }
                if (p < line_end)
                    p++;
                continue;
            }
            start = p;
            while (p < line_end && *p != ' ' && *p != '\t' && *p != '\r')
                p++;
            if (count < 24) {
                out->word[count] = start;
                out->length[count] = (size_t)(p - start);
                count++;
            }
        }
        if (count > 0) {
            out->count = count;
            return 1;
        }
    }
}

static int parse_int(const char *text, size_t n, int32_t *out)
{
    size_t i = 0;
    int negative = 0;
    int64_t value = 0;
    if (n == 0)
        return 0;
    if (text[0] == '-') {
        negative = 1;
        i = 1;
    }
    if (i >= n)
        return 0;
    if (n - i > 2 && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
        for (i += 2; i < n; i++) {
            char c = text[i];
            int digit;
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            else
                return 0;
            value = value * 16 + digit;
            if (value > 0xffffffffLL)
                return 0;
        }
    } else {
        for (; i < n; i++) {
            if (text[i] < '0' || text[i] > '9')
                return 0;
            value = value * 10 + (text[i] - '0');
            if (value > 0xffffffffLL)
                return 0;
        }
    }
    *out = (int32_t)(negative ? -value : value);
    return 1;
}

/* "name@offset:size" */
static int parse_field(const char *text, size_t n, char *field,
    uint32_t *offset, uint32_t *size)
{
    size_t at = 0, colon;
    int32_t a, b;
    while (at < n && text[at] != '@')
        at++;
    if (at == 0 || at >= n)
        return 0;
    colon = at + 1;
    while (colon < n && text[colon] != ':')
        colon++;
    if (colon >= n)
        return 0;
    if (!parse_int(text + at + 1, colon - at - 1, &a) ||
        !parse_int(text + colon + 1, n - colon - 1, &b) || a < 0 || b <= 0)
        return 0;
    copy_text(field, SETTINGS_FIELD_MAX, text, at);
    *offset = (uint32_t)a;
    *size = (uint32_t)b;
    return 1;
}

static int type_by_name(const char *text, size_t n)
{
    static const struct { const char *name; int type; } table[] = {
        {"bool", SETTINGS_T_BOOL}, {"int", SETTINGS_T_INT},
        {"enum", SETTINGS_T_ENUM}, {"flags", SETTINGS_T_FLAGS},
        {"string", SETTINGS_T_STRING}, {"font", SETTINGS_T_FONT},
        {"pen", SETTINGS_T_PEN}, {"rgb", SETTINGS_T_RGB},
        {"modeid", SETTINGS_T_MODEID}, {"path", SETTINGS_T_PATH},
        {"keymap", SETTINGS_T_KEYMAP}, {"country", SETTINGS_T_COUNTRY},
    };
    size_t i;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (same(text, n, table[i].name))
            return table[i].type;
    return 0;
}

static int level_by_name(const char *text, size_t n)
{
    if (same(text, n, "novice")) return SETTINGS_L_NOVICE;
    if (same(text, n, "average")) return SETTINGS_L_AVERAGE;
    if (same(text, n, "expert")) return SETTINGS_L_EXPERT;
    return -1;
}

static int applier_by_name(const char *text, size_t n)
{
    if (same(text, n, "iprefs")) return SETTINGS_A_IPREFS;
    if (same(text, n, "muimaster")) return SETTINGS_A_MUIMASTER;
    if (same(text, n, "self")) return SETTINGS_A_SELF;
    return -1;
}

static unsigned count_choices(const char *text, size_t n)
{
    unsigned count = n > 0 ? 1 : 0;
    size_t i;
    for (i = 0; i < n; i++)
        if (text[i] == ',')
            count++;
    return count;
}

/* "a=1,b=2" into a preallocated array; without "=value" the index counts. */
static int parse_choices(const char *text, size_t n, struct settings_choice *out,
    unsigned count)
{
    size_t i = 0;
    unsigned c;
    for (c = 0; c < count; c++) {
        size_t start = i, eq;
        int32_t value;
        while (i < n && text[i] != ',')
            i++;
        eq = start;
        while (eq < i && text[eq] != '=')
            eq++;
        if (eq == start)
            return 0;
        copy_text(out[c].name, SETTINGS_NAME_MAX, text + start, eq - start);
        if (eq < i) {
            if (!parse_int(text + eq + 1, i - eq - 1, &value))
                return 0;
        } else {
            value = (int32_t)c;
        }
        out[c].value = value;
        i++;
    }
    return 1;
}

/* Keyword pairs shared by group and record lines. "live"/"reboot" are
 * single words. Returns 0 on an unknown keyword or a bad value. */
static int apply_keywords(struct parse_state *state, const struct line *line,
    unsigned from, struct settings_record *record,
    const struct settings_schema *schema,
    settings_alloc_fn alloc, void *ctx)
{
    unsigned i = from;
    while (i < line->count) {
        const char *w = line->word[i];
        size_t n = line->length[i];
        if (same(w, n, "live")) {
            record->live = 1;
            i++;
        } else if (same(w, n, "reboot")) {
            record->live = 0;
            i++;
        } else if (i + 1 >= line->count) {
            return fail(state, "keyword without a value");
        } else {
            const char *v = line->word[i + 1];
            size_t vn = line->length[i + 1];
            if (same(w, n, "type")) {
                int type = type_by_name(v, vn);
                if (type == 0)
                    return fail(state, "unknown type");
                record->type = (uint8_t)type;
            } else if (same(w, n, "level")) {
                int level = level_by_name(v, vn);
                if (level < 0)
                    return fail(state, "unknown level");
                record->level = (uint8_t)level;
            } else if (same(w, n, "applier")) {
                int applier = applier_by_name(v, vn);
                if (applier < 0)
                    return fail(state, "unknown applier");
                record->applier = (uint8_t)applier;
            } else if (same(w, n, "range")) {
                size_t dots = 0;
                int32_t lo, hi;
                while (dots + 1 < vn && !(v[dots] == '.' && v[dots + 1] == '.'))
                    dots++;
                if (dots + 1 >= vn || !parse_int(v, dots, &lo) ||
                    !parse_int(v + dots + 2, vn - dots - 2, &hi) || lo > hi)
                    return fail(state, "range is lo..hi");
                record->min = lo;
                record->max = hi;
                record->has_range = 1;
            } else if (same(w, n, "unit")) {
                copy_text(record->unit, SETTINGS_NAME_MAX, v, vn);
            } else if (same(w, n, "default")) {
                copy_text(record->dflt, SETTINGS_TEXT_MAX, v, vn);
                record->has_default = 1;
            } else if (same(w, n, "select")) {
                int32_t value;
                if (!parse_int(v, vn, &value) || value < 0)
                    return fail(state, "select is a number");
                record->select = (uint32_t)value;
                record->has_select = 1;
            } else if (same(w, n, "chunk")) {
                uint32_t c, id;
                if (vn != 4)
                    return fail(state, "chunk takes a four-character id");
                id = SETTINGS_MAKE_ID(v[0], v[1], v[2], v[3]);
                for (c = 0; c < schema->chunk_count; c++)
                    if (schema->chunks[c].id == id)
                        break;
                if (c == schema->chunk_count)
                    return fail(state, "group names a chunk the header does not declare");
                record->chunk = c;
            } else if (same(w, n, "choices")) {
                unsigned count = count_choices(v, vn);
                struct settings_choice *choices;
                if (count == 0)
                    return fail(state, "choices is a list");
                choices = alloc(ctx, sizeof(*choices) * count);
                if (choices == NULL)
                    return SETTINGS_E_MEMORY;
                if (!parse_choices(v, vn, choices, count))
                    return fail(state, "choices is name=value,...");
                record->choices = choices;
                record->choice_count = count;
                record->choice_names = alloc(ctx, sizeof(const char *) * count);
                record->choice_values = alloc(ctx, sizeof(int32_t) * count);
                if (record->choice_names == NULL || record->choice_values == NULL)
                    return SETTINGS_E_MEMORY;
                {
                    unsigned c;
                    for (c = 0; c < count; c++) {
                        record->choice_names[c] = choices[c].name;
                        record->choice_values[c] = choices[c].value;
                    }
                }
            } else {
                return fail(state, "unknown keyword");
            }
            i += 2;
        }
    }
    return SETTINGS_E_OK;
}

static int parse_header_line(struct parse_state *state, const struct line *line,
    struct settings_schema *schema)
{
    const char *w = line->word[0];
    size_t n = line->length[0];
    if (same(w, n, "schema")) {
        int32_t version;
        if (line->count != 2 || !parse_int(line->word[1], line->length[1], &version) ||
            version != 1)
            return fail(state, "schema 1 expected");
        schema->version = 1;
    } else if (same(w, n, "stem")) {
        /* A file whose name is not the keys' first component
         * (zune/global.prefs holds zune/...) names its stem. */
        if (line->count != 2 || line->length[1] >= SETTINGS_NAME_MAX)
            return fail(state, "stem takes one name");
        copy_text(schema->stem, SETTINGS_NAME_MAX, line->word[1], line->length[1]);
        state->explicit_stem = 1;
    } else if (same(w, n, "file")) {
        size_t slash, dot, stem_len;
        const char *name;
        if (line->count != 2)
            return fail(state, "file takes one path");
        copy_text(schema->file, SETTINGS_TEXT_MAX, line->word[1], line->length[1]);
        slash = line->length[1];
        while (slash > 0 && line->word[1][slash - 1] != '/')
            slash--;
        name = line->word[1] + slash;
        stem_len = line->length[1] - slash;
        dot = 0;
        while (dot < stem_len && name[dot] != '.')
            dot++;
        if (!state->explicit_stem)
            copy_text(schema->stem, SETTINGS_NAME_MAX, name, dot);
    } else if (same(w, n, "layout")) {
        if (line->count != 3)
            return fail(state, "layout takes a header path and a structure");
        copy_text(schema->layout_file, SETTINGS_PATH_MAX, line->word[1], line->length[1]);
        copy_text(schema->layout_struct, SETTINGS_FIELD_MAX, line->word[2], line->length[2]);
    } else if (same(w, n, "chunk")) {
        unsigned i = 2;
        struct settings_chunk *chunk;
        if (line->count < 2 || line->length[1] != 4)
            return fail(state, "chunk takes a four-character id");
        if (schema->chunk_count >= SETTINGS_CHUNKS_MAX)
            return fail(state, "too many chunk lines");
        chunk = &schema->chunks[schema->chunk_count++];
        chunk->id = SETTINGS_MAKE_ID(line->word[1][0], line->word[1][1],
            line->word[1][2], line->word[1][3]);
        while (i + 1 < line->count) {
            if (same(line->word[i], line->length[i], "size")) {
                int32_t size;
                if (!parse_int(line->word[i + 1], line->length[i + 1], &size) || size <= 0)
                    return fail(state, "chunk size is a number");
                chunk->size = (uint32_t)size;
            } else if (same(line->word[i], line->length[i], "select")) {
                char field[SETTINGS_FIELD_MAX];
                if (!parse_field(line->word[i + 1], line->length[i + 1], field,
                        &chunk->select_offset, &chunk->select_size) ||
                    chunk->select_size > 4)
                    return fail(state, "select is name@offset:size");
                chunk->has_select = 1;
            } else if (same(line->word[i], line->length[i], "layout")) {
                copy_text(chunk->layout_struct, SETTINGS_FIELD_MAX, line->word[i + 1],
                    line->length[i + 1]);
            } else if (same(line->word[i], line->length[i], "kind")) {
                if (same(line->word[i + 1], line->length[i + 1], "items"))
                    chunk->kind = SETTINGS_CHUNK_ITEMS;
                else if (same(line->word[i + 1], line->length[i + 1], "struct"))
                    chunk->kind = SETTINGS_CHUNK_STRUCT;
                else
                    return fail(state, "chunk kind is struct or items");
            } else {
                return fail(state, "unknown chunk keyword");
            }
            i += 2;
        }
        if (i != line->count)
            return fail(state, "chunk keyword without a value");
    } else {
        return fail(state, "unknown header line");
    }
    return SETTINGS_E_OK;
}

int settings_schema_parse(const char *text, size_t length,
    settings_alloc_fn alloc, settings_free_fn release, void *ctx,
    struct settings_schema **out, char *err, size_t err_size)
{
    struct parse_state state;
    struct line line;
    struct settings_schema *schema;
    struct settings_record group;
    unsigned record_count = 0, index = 0;
    int have_group = 0;
    int result;

    if (err != NULL && err_size > 0)
        err[0] = '\0';
    *out = NULL;

    /* First pass: count record lines. */
    state.cursor = text;
    state.end = text + length;
    state.line = 0;
    state.err = err;
    state.err_size = err_size;
    state.explicit_stem = 0;
    while (next_line(&state, &line)) {
        if (line.word[0][0] == '[')
            have_group = 1;
        else if (have_group)
            record_count++;
    }

    schema = alloc(ctx, sizeof(*schema));
    if (schema == NULL)
        return SETTINGS_E_MEMORY;
    {
        size_t i;
        unsigned char *bytes = (unsigned char *)schema;
        for (i = 0; i < sizeof(*schema); i++)
            bytes[i] = 0;
    }
    if (record_count > 0) {
        size_t i;
        unsigned char *bytes;
        schema->records = alloc(ctx, sizeof(struct settings_record) * record_count);
        if (schema->records == NULL) {
            release(ctx, schema, sizeof(*schema));
            return SETTINGS_E_MEMORY;
        }
        bytes = (unsigned char *)schema->records;
        for (i = 0; i < sizeof(struct settings_record) * record_count; i++)
            bytes[i] = 0;
    }
    schema->record_count = record_count;

    /* Second pass. */
    state.cursor = text;
    state.line = 0;
    have_group = 0;
    result = SETTINGS_E_OK;
    while (result == SETTINGS_E_OK && next_line(&state, &line)) {
        if (line.word[0][0] == '[') {
            size_t n = line.length[0];
            size_t i;
            unsigned char *bytes = (unsigned char *)&group;
            for (i = 0; i < sizeof(group); i++)
                bytes[i] = 0;
            if (n < 3 || line.word[0][n - 1] != ']')
                result = fail(&state, "group is [stem/name]");
            else {
                copy_text(group.key, SETTINGS_KEY_MAX, line.word[0] + 1, n - 2);
                group.level = SETTINGS_L_AVERAGE;
                group.applier = SETTINGS_A_IPREFS;
                group.live = 1;
                result = apply_keywords(&state, &line, 1, &group, schema, alloc, ctx);
                have_group = 1;
            }
        } else if (!have_group) {
            result = parse_header_line(&state, &line, schema);
        } else {
            struct settings_record *record = &schema->records[index++];
            size_t key_len = text_length(group.key);
            size_t i;
            if (line.count < 2)
                result = fail(&state, "record needs a name and a field");
            else {
                *record = group;
                record->choices = NULL;
                record->choice_count = 0;
                record->choice_names = NULL;
                record->choice_values = NULL;
                record->has_default = 0;
                record->dflt[0] = '\0';
                record->unit[0] = '\0';
                record->has_range = 0;
                record->type = 0;
                if (key_len + 1 + line.length[0] >= SETTINGS_KEY_MAX)
                    result = fail(&state, "key too long");
                else {
                    record->key[key_len] = '/';
                    for (i = 0; i < line.length[0]; i++)
                        record->key[key_len + 1 + i] = line.word[0][i];
                    record->key[key_len + 1 + line.length[0]] = '\0';
                    if (!parse_field(line.word[1], line.length[1], record->field,
                            &record->offset, &record->size))
                        result = fail(&state, "field is name@offset:size");
                    else
                        result = apply_keywords(&state, &line, 2, record, schema, alloc, ctx);
                    if (result == SETTINGS_E_OK && record->type == 0)
                        result = fail(&state, "record has no type");
                    if (result == SETTINGS_E_OK &&
                        schema->chunks[record->chunk].kind == SETTINGS_CHUNK_ITEMS) {
                        if (record->size > SETTINGS_ITEM_MAX)
                            result = fail(&state, "an item value is at most 256 bytes");
                        else if (record->type != SETTINGS_T_STRING && record->type != SETTINGS_T_FONT &&
                                 record->type != SETTINGS_T_PATH && record->size != 4)
                            result = fail(&state, "a numeric item is four bytes");
                    } else if (result == SETTINGS_E_OK && schema->chunks[record->chunk].size != 0 &&
                        record->offset + record->size > schema->chunks[record->chunk].size)
                        result = fail(&state, "field outside the chunk");
                    if (result == SETTINGS_E_OK &&
                        (record->type == SETTINGS_T_ENUM || record->type == SETTINGS_T_FLAGS) &&
                        record->choice_count == 0)
                        result = fail(&state, "enum and flags need choices");
                }
            }
        }
    }
    if (result == SETTINGS_E_OK) {
        uint32_t c;
        if (schema->version != 1)
            result = fail(&state, "schema version line missing");
        else if (schema->file[0] == '\0')
            result = fail(&state, "file line missing");
        else if (schema->chunk_count == 0)
            result = fail(&state, "chunk line missing");
        for (c = 0; result == SETTINGS_E_OK && c < schema->chunk_count; c++) {
            const struct settings_chunk *chunk = &schema->chunks[c];
            if (chunk->has_select && chunk->size != 0 &&
                chunk->select_offset + chunk->select_size > chunk->size)
                result = fail(&state, "select outside the chunk");
            else if (chunk->kind == SETTINGS_CHUNK_ITEMS && (chunk->has_select || chunk->size != 0))
                result = fail(&state, "an items chunk has no size and no select");
        }
    }
    if (result != SETTINGS_E_OK) {
        settings_schema_free(schema, release, ctx);
        return result;
    }
    *out = schema;
    return SETTINGS_E_OK;
}

void settings_schema_free(struct settings_schema *schema,
    settings_free_fn release, void *ctx)
{
    uint32_t i;
    if (schema == NULL)
        return;
    for (i = 0; i < schema->record_count; i++) {
        struct settings_record *record = &schema->records[i];
        if (record->choices != NULL)
            release(ctx, record->choices,
                sizeof(struct settings_choice) * record->choice_count);
        if (record->choice_names != NULL)
            release(ctx, record->choice_names,
                sizeof(const char *) * record->choice_count);
        if (record->choice_values != NULL)
            release(ctx, record->choice_values,
                sizeof(int32_t) * record->choice_count);
    }
    if (schema->records != NULL)
        release(ctx, schema->records,
            sizeof(struct settings_record) * schema->record_count);
    release(ctx, schema, sizeof(*schema));
}

const struct settings_record *settings_schema_find(
    const struct settings_schema *schema, const char *key)
{
    uint32_t i;
    size_t n = text_length(key);
    for (i = 0; i < schema->record_count; i++)
        if (same(key, n, schema->records[i].key))
            return &schema->records[i];
    return NULL;
}
