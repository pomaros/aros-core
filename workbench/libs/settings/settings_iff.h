#ifndef SETTINGS_IFF_H
#define SETTINGS_IFF_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Locating a schema's chunk in a FORM PREF image, and the text form of
    a field. Plain C: the host fixture compiles this file as is.
*/
#include "settings_schema.h"

/* Finds the chunk a record belongs to (its group's selector, when the
 * schema has one). *data_offset is the offset of the chunk's data in the
 * image. Returns SETTINGS_E_FORMAT when the image is not a FORM PREF, the
 * chunk is absent, or its size differs from the schema's. */
int settings_iff_locate(const uint8_t *image, size_t length,
    const struct settings_schema *schema, const struct settings_record *record,
    size_t *data_offset);

/* Writes the field's text form into buffer: integers in decimal, enum
 * choices by name, flags as a comma list, strings as they are. */
int settings_value_get(const uint8_t *chunk, const struct settings_record *record,
    char *buffer, size_t buffer_size);

/* Parses text and stores the field in place. Range, choice and length
 * violations return SETTINGS_E_RANGE without touching the chunk. */
int settings_value_set(uint8_t *chunk, const struct settings_record *record,
    const char *text);

/* A FORM PREF as a list of chunks, for a whole-file rewrite. data points
 * into the caller's image or buffer; the codec never owns it. */
struct settings_iff_chunk {
    uint32_t id;
    uint32_t size;
    const uint8_t *data;
};

/* Splits a FORM PREF into its chunks in order. SETTINGS_E_FORMAT when the
 * image is not a FORM PREF or a chunk overruns it; SETTINGS_E_BUFFER when
 * more than capacity chunks are present. */
int settings_iff_split(const uint8_t *image, size_t length,
    struct settings_iff_chunk *chunks, size_t capacity, size_t *count);

/* Every schema chunk present in the list has the schema's size (0 size
 * means unchecked); otherwise SETTINGS_E_FORMAT. Chunks the schema does
 * not describe (PRHD, foreign chunks) pass. */
int settings_iff_check(const struct settings_schema *schema,
    const struct settings_iff_chunk *chunks, size_t count);

/* Size of the image settings_iff_join writes: FORM header plus each
 * chunk with its header and odd-length pad. */
size_t settings_iff_join_size(const struct settings_iff_chunk *chunks, size_t count);
void settings_iff_join(uint8_t *image, const struct settings_iff_chunk *chunks, size_t count);

/* One setting in a file image, whatever its chunk's kind. get writes the
 * text form; bytes gives the field's raw bytes (struct field or item
 * data). set rewrites the setting: a struct field in place in image (and
 * *out is NULL), an item into a new image allocated with alloc (*out,
 * *out_length), since an item can change length or be added. An absent
 * item reads as the record's default, else SETTINGS_E_NO_FILE. */
int settings_record_get(const uint8_t *image, size_t length,
    const struct settings_schema *schema, const struct settings_record *record,
    char *buffer, size_t size);
int settings_record_bytes(const uint8_t *image, size_t length,
    const struct settings_schema *schema, const struct settings_record *record,
    const uint8_t **data, uint32_t *data_length);
int settings_record_set(uint8_t *image, size_t length,
    const struct settings_schema *schema, const struct settings_record *record,
    const char *text, settings_alloc_fn alloc, settings_free_fn release, void *ctx,
    uint8_t **out, size_t *out_length);

/* Big-endian field access shared with the library. */
uint32_t settings_field_read(const uint8_t *chunk, uint32_t offset, uint32_t size);
void settings_field_write(uint8_t *chunk, uint32_t offset, uint32_t size, uint32_t value);

#endif /* SETTINGS_IFF_H */
