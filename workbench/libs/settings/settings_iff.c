/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <string.h>
#include "settings_iff.h"

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint32_t settings_field_read(const uint8_t *chunk, uint32_t offset, uint32_t size)
{
    uint32_t value = 0, i;
    for (i = 0; i < size && i < 4; i++)
        value = (value << 8) | chunk[offset + i];
    return value;
}

void settings_field_write(uint8_t *chunk, uint32_t offset, uint32_t size, uint32_t value)
{
    uint32_t i;
    for (i = 0; i < size && i < 4; i++)
        chunk[offset + size - 1 - i] = (uint8_t)(value >> (8 * i));
}

int settings_iff_locate(const uint8_t *image, size_t length,
    const struct settings_schema *schema, const struct settings_record *record,
    size_t *data_offset)
{
    size_t position = 12, form_end;
    if (length < 12 || be32(image) != SETTINGS_MAKE_ID('F', 'O', 'R', 'M') ||
        be32(image + 8) != SETTINGS_MAKE_ID('P', 'R', 'E', 'F'))
        return SETTINGS_E_FORMAT;
    form_end = 8 + (size_t)be32(image + 4);
    if (form_end > length)
        return SETTINGS_E_FORMAT;
    const struct settings_chunk *chunk;
    if (record->chunk >= schema->chunk_count)
        return SETTINGS_E_FORMAT;
    chunk = &schema->chunks[record->chunk];
    while (position + 8 <= form_end) {
        uint32_t id = be32(image + position);
        size_t size = be32(image + position + 4);
        size_t data = position + 8;
        if (data + size > form_end)
            return SETTINGS_E_FORMAT;
        if (id == chunk->id) {
            if (chunk->size != 0 && size != chunk->size)
                return SETTINGS_E_FORMAT;
            if (!chunk->has_select || !record->has_select ||
                settings_field_read(image + data, chunk->select_offset,
                    chunk->select_size) == record->select) {
                if (record->offset + record->size > size)
                    return SETTINGS_E_FORMAT;
                *data_offset = data;
                return SETTINGS_E_OK;
            }
        }
        position = data + size + (size & 1);
    }
    return SETTINGS_E_FORMAT;
}

static size_t text_length(const char *text)
{
    size_t n = 0;
    while (text[n] != '\0')
        n++;
    return n;
}

static int put_text(char *buffer, size_t size, size_t *used, const char *text)
{
    size_t i;
    for (i = 0; text[i] != '\0'; i++) {
        if (*used + 1 >= size)
            return SETTINGS_E_BUFFER;
        buffer[(*used)++] = text[i];
    }
    buffer[*used] = '\0';
    return SETTINGS_E_OK;
}

static int put_number(char *buffer, size_t size, size_t *used, int32_t value, int hex)
{
    char digits[16];
    size_t d = 0;
    uint32_t magnitude = value < 0 && !hex ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    if (hex) {
        if (put_text(buffer, size, used, "0x") != SETTINGS_E_OK)
            return SETTINGS_E_BUFFER;
    } else if (value < 0 && put_text(buffer, size, used, "-") != SETTINGS_E_OK) {
        return SETTINGS_E_BUFFER;
    }
    if (magnitude == 0)
        digits[d++] = '0';
    while (magnitude != 0) {
        uint32_t digit = hex ? magnitude % 16 : magnitude % 10;
        digits[d++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        magnitude = hex ? magnitude / 16 : magnitude / 10;
    }
    while (d > 0) {
        char one[2];
        one[0] = digits[--d];
        one[1] = '\0';
        if (put_text(buffer, size, used, one) != SETTINGS_E_OK)
            return SETTINGS_E_BUFFER;
    }
    return SETTINGS_E_OK;
}

/* Sign-extends an integer field of 1, 2 or 4 bytes. */
static int32_t signed_field(uint32_t raw, uint32_t size)
{
    if (size == 1)
        return (int8_t)raw;
    if (size == 2)
        return (int16_t)raw;
    return (int32_t)raw;
}

/* A record's value within its field: the masked bits shifted down, or the
   whole field. A masked write keeps the other bits and refuses a value
   wider than the mask. */
static uint32_t record_read(const uint8_t *chunk, const struct settings_record *record)
{
    uint32_t raw = settings_field_read(chunk, record->offset, record->size), mask = record->mask;
    if (mask == 0)
        return raw;
    raw &= mask;
    while (!(mask & 1)) {
        mask >>= 1;
        raw >>= 1;
    }
    return raw;
}

static int record_write(uint8_t *chunk, const struct settings_record *record, uint32_t value)
{
    uint32_t mask = record->mask, shift = 0, raw;
    if (mask == 0) {
        settings_field_write(chunk, record->offset, record->size, value);
        return SETTINGS_E_OK;
    }
    while (!((mask >> shift) & 1))
        shift++;
    if (value > (mask >> shift) || ((value << shift) & ~mask) != 0)
        return SETTINGS_E_RANGE;
    raw = settings_field_read(chunk, record->offset, record->size);
    settings_field_write(chunk, record->offset, record->size, (raw & ~mask) | (value << shift));
    return SETTINGS_E_OK;
}

int settings_value_get(const uint8_t *chunk, const struct settings_record *record,
    char *buffer, size_t buffer_size)
{
    size_t used = 0;
    uint32_t raw, i;
    if (buffer_size == 0)
        return SETTINGS_E_BUFFER;
    buffer[0] = '\0';
    switch (record->type) {
    case SETTINGS_T_STRING:
    case SETTINGS_T_FONT:
    case SETTINGS_T_PATH:
    case SETTINGS_T_KEYMAP:
    case SETTINGS_T_COUNTRY:
        for (i = 0; i < record->size && chunk[record->offset + i] != '\0'; i++) {
            if (used + 1 >= buffer_size)
                return SETTINGS_E_BUFFER;
            buffer[used++] = (char)chunk[record->offset + i];
        }
        buffer[used] = '\0';
        return SETTINGS_E_OK;
    case SETTINGS_T_BOOL:
        raw = record_read(chunk, record);
        return put_text(buffer, buffer_size, &used, raw != 0 ? "1" : "0");
    case SETTINGS_T_INT:
    case SETTINGS_T_PEN:
        raw = record_read(chunk, record);
        return put_number(buffer, buffer_size, &used, signed_field(raw, record->size), 0);
    case SETTINGS_T_RGB:
    case SETTINGS_T_MODEID:
        raw = record_read(chunk, record);
        return put_number(buffer, buffer_size, &used, (int32_t)raw, 1);
    case SETTINGS_T_ENUM:
        raw = record_read(chunk, record);
        for (i = 0; i < record->choice_count; i++)
            if ((uint32_t)record->choices[i].value == raw)
                return put_text(buffer, buffer_size, &used, record->choices[i].name);
        return put_number(buffer, buffer_size, &used, signed_field(raw, record->size), 0);
    case SETTINGS_T_FLAGS:
        raw = record_read(chunk, record);
        for (i = 0; i < record->choice_count; i++) {
            uint32_t bit = (uint32_t)record->choices[i].value;
            if ((raw & bit) == bit && bit != 0) {
                if (used > 0 && put_text(buffer, buffer_size, &used, ",") != SETTINGS_E_OK)
                    return SETTINGS_E_BUFFER;
                if (put_text(buffer, buffer_size, &used, record->choices[i].name) != SETTINGS_E_OK)
                    return SETTINGS_E_BUFFER;
            }
        }
        return SETTINGS_E_OK;
    }
    return SETTINGS_E_UNKNOWN;
}

static int parse_number(const char *text, size_t n, int32_t *out)
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
            int digit;
            if (text[i] >= '0' && text[i] <= '9') digit = text[i] - '0';
            else if (text[i] >= 'a' && text[i] <= 'f') digit = text[i] - 'a' + 10;
            else if (text[i] >= 'A' && text[i] <= 'F') digit = text[i] - 'A' + 10;
            else return 0;
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

static int same(const char *a, size_t n, const char *literal)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (literal[i] == '\0' || literal[i] != a[i])
            return 0;
    return literal[n] == '\0';
}

static int fits(const struct settings_record *record, int32_t value)
{
    int64_t lo, hi;
    if (record->has_range)
        return value >= record->min && value <= record->max;
    switch (record->size) {
    case 1: lo = -128; hi = 255; break;
    case 2: lo = -32768; hi = 65535; break;
    default: return 1;
    }
    return value >= lo && value <= hi;
}

int settings_value_set(uint8_t *chunk, const struct settings_record *record,
    const char *text)
{
    size_t n = text_length(text), i;
    int32_t value;
    uint32_t bits = 0;
    switch (record->type) {
    case SETTINGS_T_STRING:
    case SETTINGS_T_FONT:
    case SETTINGS_T_PATH:
    case SETTINGS_T_KEYMAP:
    case SETTINGS_T_COUNTRY:
        if (n + 1 > record->size)
            return SETTINGS_E_RANGE;
        for (i = 0; i < record->size; i++)
            chunk[record->offset + i] = i < n ? (uint8_t)text[i] : 0;
        return SETTINGS_E_OK;
    case SETTINGS_T_BOOL:
        if (same(text, n, "1") || same(text, n, "on") || same(text, n, "true") || same(text, n, "yes"))
            value = 1;
        else if (same(text, n, "0") || same(text, n, "off") || same(text, n, "false") || same(text, n, "no"))
            value = 0;
        else
            return SETTINGS_E_RANGE;
        return record_write(chunk, record, (uint32_t)value);
    case SETTINGS_T_INT:
    case SETTINGS_T_PEN:
    case SETTINGS_T_RGB:
    case SETTINGS_T_MODEID:
        if (!parse_number(text, n, &value) || !fits(record, value))
            return SETTINGS_E_RANGE;
        return record_write(chunk, record, (uint32_t)value);
    case SETTINGS_T_ENUM:
        for (i = 0; i < record->choice_count; i++)
            if (same(text, n, record->choices[i].name)) {
                return record_write(chunk, record, (uint32_t)record->choices[i].value);
            }
        if (parse_number(text, n, &value))
            for (i = 0; i < record->choice_count; i++)
                if (record->choices[i].value == value) {
                    return record_write(chunk, record, (uint32_t)value);
                }
        return SETTINGS_E_RANGE;
    case SETTINGS_T_FLAGS:
        if (n == 0) {
            bits = 0;
        } else if (parse_number(text, n, &value)) {
            bits = (uint32_t)value;
        } else {
            size_t start = 0;
            for (;;) {
                size_t end = start, c;
                int found = 0;
                while (end < n && text[end] != ',')
                    end++;
                if (end == start)
                    return SETTINGS_E_RANGE;
                for (c = 0; c < record->choice_count; c++)
                    if (same(text + start, end - start, record->choices[c].name)) {
                        bits |= (uint32_t)record->choices[c].value;
                        found = 1;
                    }
                if (!found)
                    return SETTINGS_E_RANGE;
                if (end >= n)
                    break;
                start = end + 1;
            }
        }
        return record_write(chunk, record, bits);
    }
    return SETTINGS_E_UNKNOWN;
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);  p[3] = (uint8_t)value;
}

int settings_iff_split(const uint8_t *image, size_t length,
    struct settings_iff_chunk *chunks, size_t capacity, size_t *count)
{
    size_t position = 12, form_end, n = 0;
    *count = 0;
    if (length < 12 || be32(image) != SETTINGS_MAKE_ID('F', 'O', 'R', 'M') ||
        be32(image + 8) != SETTINGS_MAKE_ID('P', 'R', 'E', 'F'))
        return SETTINGS_E_FORMAT;
    form_end = 8 + (size_t)be32(image + 4);
    if (form_end > length)
        return SETTINGS_E_FORMAT;
    while (position + 8 <= form_end) {
        size_t size = be32(image + position + 4);
        size_t data = position + 8;
        if (data + size > form_end)
            return SETTINGS_E_FORMAT;
        if (n == capacity)
            return SETTINGS_E_BUFFER;
        chunks[n].id = be32(image + position);
        chunks[n].size = (uint32_t)size;
        chunks[n].data = image + data;
        n++;
        position = data + size + (size & 1);
    }
    *count = n;
    return SETTINGS_E_OK;
}

int settings_iff_check(const struct settings_schema *schema,
    const struct settings_iff_chunk *chunks, size_t count)
{
    size_t i, j;
    for (i = 0; i < count; i++) {
        if (chunks[i].size != 0 && chunks[i].data == NULL)
            return SETTINGS_E_FORMAT;
        for (j = 0; j < schema->chunk_count; j++)
            if (chunks[i].id == schema->chunks[j].id && schema->chunks[j].size != 0 &&
                chunks[i].size != schema->chunks[j].size)
                return SETTINGS_E_FORMAT;
    }
    return SETTINGS_E_OK;
}

size_t settings_iff_join_size(const struct settings_iff_chunk *chunks, size_t count)
{
    size_t i, total = 12;
    for (i = 0; i < count; i++)
        total += 8 + chunks[i].size + (chunks[i].size & 1);
    return total;
}

void settings_iff_join(uint8_t *image, const struct settings_iff_chunk *chunks, size_t count)
{
    size_t i, position = 12;
    put32(image, SETTINGS_MAKE_ID('F', 'O', 'R', 'M'));
    put32(image + 8, SETTINGS_MAKE_ID('P', 'R', 'E', 'F'));
    for (i = 0; i < count; i++) {
        put32(image + position, chunks[i].id);
        put32(image + position + 4, chunks[i].size);
        if (chunks[i].size)
            memcpy(image + position + 8, chunks[i].data, chunks[i].size);
        position += 8 + chunks[i].size;
        if (chunks[i].size & 1)
            image[position++] = 0;
    }
    put32(image + 4, (uint32_t)(position - 8));
}

/*** Items chunks ***********************************************************/

/* The image's chunk for this record (no selector for items). */
static int find_chunk(const uint8_t *image, size_t length, uint32_t id,
    size_t *data, size_t *size)
{
    size_t position = 12, form_end;
    if (length < 12 || be32(image) != SETTINGS_MAKE_ID('F', 'O', 'R', 'M') ||
        be32(image + 8) != SETTINGS_MAKE_ID('P', 'R', 'E', 'F'))
        return SETTINGS_E_FORMAT;
    form_end = 8 + (size_t)be32(image + 4);
    if (form_end > length)
        return SETTINGS_E_FORMAT;
    while (position + 8 <= form_end) {
        size_t chunk_size = be32(image + position + 4);
        if (position + 8 + chunk_size > form_end)
            return SETTINGS_E_FORMAT;
        if (be32(image + position) == id) {
            *data = position + 8;
            *size = chunk_size;
            return SETTINGS_E_OK;
        }
        position += 8 + chunk_size + (chunk_size & 1);
    }
    return SETTINGS_E_FORMAT;
}

/* Walks a chunk's id/length/data records; SETTINGS_E_NO_FILE if absent. */
static int find_item(const uint8_t *chunk, size_t size, uint32_t id,
    size_t *data, uint32_t *item_length)
{
    size_t position = 0;
    while (position + 8 <= size) {
        uint32_t item = be32(chunk + position), n = be32(chunk + position + 4);
        if (position + 8 + n > size)
            return SETTINGS_E_FORMAT;
        if (item == id) {
            *data = position + 8;
            *item_length = n;
            return SETTINGS_E_OK;
        }
        position += 8 + n;
    }
    return position == size ? SETTINGS_E_NO_FILE : SETTINGS_E_FORMAT;
}

static int is_text(const struct settings_record *record)
{
    return record->type == SETTINGS_T_STRING || record->type == SETTINGS_T_FONT ||
           record->type == SETTINGS_T_PATH;
}

/* The record seen at offset 0 of a scratch buffer, so the struct codec
 * encodes and decodes an item's value. */
static struct settings_record scratch_record(const struct settings_record *record)
{
    struct settings_record r = *record;
    r.offset = 0;
    return r;
}

static int item_view(const uint8_t *image, size_t length,
    const struct settings_schema *schema, const struct settings_record *record,
    const uint8_t **data, uint32_t *data_length)
{
    size_t chunk_data, chunk_size, item_data;
    int result = find_chunk(image, length, schema->chunks[record->chunk].id, &chunk_data, &chunk_size);
    if (result != SETTINGS_E_OK)
        return result;
    result = find_item(image + chunk_data, chunk_size, record->offset, &item_data, data_length);
    if (result != SETTINGS_E_OK)
        return result;
    *data = image + chunk_data + item_data;
    if (is_text(record)) {
        if (*data_length == 0 || *data_length > record->size || (*data)[*data_length - 1] != 0)
            return SETTINGS_E_FORMAT;
    } else if (*data_length != record->size) {
        return SETTINGS_E_FORMAT;
    }
    return SETTINGS_E_OK;
}

int settings_record_bytes(const uint8_t *image, size_t length,
    const struct settings_schema *schema, const struct settings_record *record,
    const uint8_t **data, uint32_t *data_length)
{
    size_t offset;
    int result;
    if (schema->chunks[record->chunk].kind == SETTINGS_CHUNK_ITEMS)
        return item_view(image, length, schema, record, data, data_length);
    result = settings_iff_locate(image, length, schema, record, &offset);
    if (result != SETTINGS_E_OK)
        return result;
    *data = image + offset + record->offset;
    *data_length = record->size;
    return SETTINGS_E_OK;
}

int settings_record_get(const uint8_t *image, size_t length,
    const struct settings_schema *schema, const struct settings_record *record,
    char *buffer, size_t size)
{
    uint8_t scratch[SETTINGS_ITEM_MAX];
    struct settings_record r;
    const uint8_t *data;
    uint32_t data_length;
    size_t offset;
    int result;

    if (schema->chunks[record->chunk].kind != SETTINGS_CHUNK_ITEMS) {
        result = settings_iff_locate(image, length, schema, record, &offset);
        return result == SETTINGS_E_OK ?
            settings_value_get(image + offset, record, buffer, size) : result;
    }
    result = item_view(image, length, schema, record, &data, &data_length);
    if (result == SETTINGS_E_NO_FILE && record->has_default) {
        if (text_length(record->dflt) + 1 > size)
            return SETTINGS_E_BUFFER;
        memcpy(buffer, record->dflt, text_length(record->dflt) + 1);
        return SETTINGS_E_OK;
    }
    if (result != SETTINGS_E_OK)
        return result;
    if (record->size > sizeof(scratch))
        return SETTINGS_E_FORMAT;
    memset(scratch, 0, sizeof(scratch));
    memcpy(scratch, data, data_length);
    r = scratch_record(record);
    return settings_value_get(scratch, &r, buffer, size);
}

int settings_record_set(uint8_t *image, size_t length,
    const struct settings_schema *schema, const struct settings_record *record,
    const char *text, settings_alloc_fn alloc, settings_free_fn release, void *ctx,
    uint8_t **out, size_t *out_length)
{
    uint8_t value[SETTINGS_ITEM_MAX];
    struct settings_iff_chunk chunks[32];
    struct settings_record r;
    size_t count = 0, offset, i, chunk_index = (size_t)-1, value_length, position, new_size;
    uint8_t *new_chunk;
    int result, placed = 0;

    *out = NULL;
    *out_length = 0;
    if (schema->chunks[record->chunk].kind != SETTINGS_CHUNK_ITEMS) {
        result = settings_iff_locate(image, length, schema, record, &offset);
        return result == SETTINGS_E_OK ? settings_value_set(image + offset, record, text) : result;
    }
    if (record->size > sizeof(value))
        return SETTINGS_E_FORMAT;
    memset(value, 0, sizeof(value));
    r = scratch_record(record);
    result = settings_value_set(value, &r, text);
    if (result != SETTINGS_E_OK)
        return result;
    value_length = is_text(record) ? text_length((const char *)value) + 1 : record->size;

    result = settings_iff_split(image, length, chunks, 32, &count);
    if (result != SETTINGS_E_OK)
        return result;
    for (i = 0; i < count; i++)
        if (chunks[i].id == schema->chunks[record->chunk].id) {
            chunk_index = i;
            break;
        }
    if (chunk_index == (size_t)-1)
        return SETTINGS_E_FORMAT;

    /* The chunk again, with the item replaced where it stood or appended. */
    new_size = chunks[chunk_index].size + 8 + value_length;
    new_chunk = alloc(ctx, new_size);
    if (new_chunk == NULL)
        return SETTINGS_E_MEMORY;
    {
        const uint8_t *old = chunks[chunk_index].data;
        size_t old_size = chunks[chunk_index].size, at = 0;
        position = 0;
        while (at + 8 <= old_size) {
            uint32_t id = be32(old + at), n = be32(old + at + 4);
            if (at + 8 + n > old_size) {
                release(ctx, new_chunk, new_size);
                return SETTINGS_E_FORMAT;
            }
            if (id == record->offset && !placed) {
                settings_field_write(new_chunk + position, 0, 4, id);
                settings_field_write(new_chunk + position, 4, 4, (uint32_t)value_length);
                memcpy(new_chunk + position + 8, value, value_length);
                position += 8 + value_length;
                placed = 1;
            } else if (id != record->offset) {
                memcpy(new_chunk + position, old + at, 8 + n);
                position += 8 + n;
            }
            at += 8 + n;
        }
        if (at != old_size) {
            release(ctx, new_chunk, new_size);
            return SETTINGS_E_FORMAT;
        }
        if (!placed) {
            settings_field_write(new_chunk + position, 0, 4, record->offset);
            settings_field_write(new_chunk + position, 4, 4, (uint32_t)value_length);
            memcpy(new_chunk + position + 8, value, value_length);
            position += 8 + value_length;
        }
    }
    chunks[chunk_index].data = new_chunk;
    chunks[chunk_index].size = (uint32_t)position;
    *out_length = settings_iff_join_size(chunks, count);
    *out = alloc(ctx, *out_length);
    if (*out == NULL) {
        release(ctx, new_chunk, new_size);
        *out_length = 0;
        return SETTINGS_E_MEMORY;
    }
    settings_iff_join(*out, chunks, count);
    release(ctx, new_chunk, new_size);
    return SETTINGS_E_OK;
}
