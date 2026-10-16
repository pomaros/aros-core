/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <string.h>

#include "settings_private.h"
#include LC_LIBDEFS_FILE

/*****************************************************************************

    NAME */
        AROS_LH4(LONG, GetSettingA,

/*  SYNOPSIS */
        AROS_LHA(CONST_STRPTR, key, A0),
        AROS_LHA(STRPTR, buffer, A1),
        AROS_LHA(ULONG, size, D0),
        AROS_LHA(const struct TagItem *, tags, A2),

/*  LOCATION */
        struct SettingsBase *, SettingsBase, 5, Settings)

/*  FUNCTION
        Reads one setting by its schema key ("font/system/size") from the
        prefs file of a layer and writes its text form into buffer.

    INPUTS
        key    - "<file>/<group>/<name>" as the file's schema names it.
        buffer - receives the value, NUL-terminated.
        size   - buffer size in bytes.
        tags   - SETTING_Layer (ULONG, default SETTING_LAYER_APPLIED):
                     the layer to read; SETTING_LAYER_USER falls back to
                     the machine layer when the user has no copy.
                 SETTING_SourceLayer (ULONG *): receives the layer that
                     answered.
                 SETTING_Binary (BOOL): copy the field's bytes as stored
                     instead of its text form; size must hold them.

    RESULT
        SETTING_OK, or one of SETTING_ERROR_UNKNOWN (no record for the
        key), SETTING_ERROR_NO_SCHEMA, SETTING_ERROR_NO_FILE,
        SETTING_ERROR_FORMAT (the file is not the schema's form),
        SETTING_ERROR_BUFFER, SETTING_ERROR_IO (see IoErr()).

    NOTES
        Integers come back in decimal, enum choices by name, flags as a
        comma-separated list of the names set, modeids and rgb values in
        hexadecimal, strings as stored.

    SEE ALSO
        SetSettingA(), DescribeSettingA()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    const struct settings_schema *schema;
    const struct settings_record *record;
    struct SettingsImage image;
    ULONG layer, source = SETTING_LAYER_APPLIED;
    ULONG *source_out;
    BOOL binary;
    LONG result;

    if (buffer == NULL || size == 0)
        return SETTING_ERROR_BUFFER;
    buffer[0] = '\0';
    schema = settings_schema_for_key(SettingsBase, key, &record, &result);
    if (schema == NULL)
        return result;
    layer = GetTagData(SETTING_Layer, SETTING_LAYER_APPLIED, tags);
    source_out = (ULONG *)GetTagData(SETTING_SourceLayer, 0, tags);
    binary = GetTagData(SETTING_Binary, FALSE, tags) != FALSE;

    result = settings_read_layer(SettingsBase, schema, layer, &image, &source);
    if (result != SETTING_OK)
        return result;
    if (binary) {
        const uint8_t *data;
        uint32_t data_length;
        result = settings_record_bytes(image.si_Data, image.si_Length, schema, record,
            &data, &data_length);
        if (result == SETTING_OK) {
            if (size < data_length)
                result = SETTING_ERROR_BUFFER;
            else
                memcpy(buffer, data, data_length);
        }
    } else {
        result = settings_record_get(image.si_Data, image.si_Length, schema, record,
            (char *)buffer, size);
    }
    settings_image_free(&image);
    if (result == SETTING_OK && source_out != NULL)
        *source_out = source;
    return result;

    AROS_LIBFUNC_EXIT
}
