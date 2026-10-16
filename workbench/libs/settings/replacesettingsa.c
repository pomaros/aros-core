/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/

#include "settings_private.h"
#include LC_LIBDEFS_FILE

static BOOL layer_file_exists(struct SettingsBase *SettingsBase,
    const struct settings_schema *schema, ULONG layer)
{
    char path[SETTINGS_FULL_PATH];
    BPTR lock;
    if (!settings_layer_path(schema, layer, (STRPTR)path, sizeof(path)))
        return FALSE;
    lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
    if (lock == BNULL)
        return FALSE;
    UnLock(lock);
    return TRUE;
}

/*****************************************************************************

    NAME */
        AROS_LH3(LONG, ReplaceSettingsA,

/*  SYNOPSIS */
        AROS_LHA(CONST_STRPTR, file, A0),
        AROS_LHA(struct Hook *, hook, A1),
        AROS_LHA(const struct TagItem *, tags, A2),

/*  LOCATION */
        struct SettingsBase *, SettingsBase, 7, Settings)

/*  FUNCTION
        Rewrites a whole prefs file under the library's atomic replace.
        The file of the chosen layer is read and split into its chunks;
        the hook is called once with a struct SettingReplaceMessage and
        returns the chunks changed; the library checks every schema
        chunk's size, joins a new FORM PREF and replaces the file.
        Writing the user or machine layer also recomposes ENV: like
        SetSettingA().

    INPUTS
        file - the schema stem ("font") or the key prefix "font/".
        hook - called as CallHookPkt(hook, struct SettingReplaceMessage *,
               NULL); nonzero to write, zero to cancel.
        tags - SETTING_Layer (ULONG, default SETTING_LAYER_APPLIED).
               SETTING_ReadOnly (BOOL): call the hook to read the chunks,
                   write nothing.
               SETTING_SourceLayer (ULONG *): the layer whose file was
                   read (the user layer falls back to the machine's).

    RESULT
        SETTING_OK, or SETTING_ERROR_NO_SCHEMA, SETTING_ERROR_NO_FILE,
        SETTING_ERROR_FORMAT (the file or the hook's result is not the
        schema's form; nothing is written), SETTING_ERROR_CANCELLED,
        SETTING_ERROR_RANGE (bad layer), SETTING_ERROR_BUFFER (more than
        SETTING_REPLACE_CHUNKS chunks), SETTING_ERROR_MEMORY,
        SETTING_ERROR_IO (see IoErr(); the old file is intact).

    NOTES
        Buffers the hook points sc_Data at stay the caller's; the library
        copies them while joining and references none of them after
        returning.

    SEE ALSO
        SetSettingA()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    const struct settings_schema *schema;
    struct SettingsImage image, joined;
    struct settings_iff_chunk chunks[SETTING_REPLACE_CHUNKS];
    struct SettingChunk exposed[SETTING_REPLACE_CHUNKS];
    struct SettingReplaceMessage message;
    char path[SETTINGS_FULL_PATH];
    ULONG layer, source, stem_length, i, *source_out;
    BOOL read_only;
    size_t count = 0;
    LONG result;

    if (file == NULL || hook == NULL)
        return SETTING_ERROR_RANGE;
    stem_length = 0;
    while (file[stem_length] != '\0' && file[stem_length] != '/')
        stem_length++;
    schema = settings_schema_for_stem(SettingsBase, file, stem_length, &result);
    if (schema == NULL)
        return result;
    layer = GetTagData(SETTING_Layer, SETTING_LAYER_APPLIED, tags);
    if (!settings_layer_path(schema, layer, (STRPTR)path, sizeof(path)))
        return SETTING_ERROR_RANGE;

    read_only = GetTagData(SETTING_ReadOnly, FALSE, tags) != 0;
    source_out = (ULONG *)GetTagData(SETTING_SourceLayer, 0, tags);
    result = settings_read_layer(SettingsBase, schema, layer, &image, &source);
    if (result == SETTING_ERROR_NO_FILE) {
        /* No file in this layer (or below it): the hook starts from an
         * empty list and may write a whole new file. */
        image.si_Data = NULL;
        image.si_Length = 0;
        source = layer;
        count = 0;
        result = SETTING_OK;
    } else if (result != SETTING_OK) {
        return result;
    } else {
        result = settings_iff_split(image.si_Data, image.si_Length, chunks,
            SETTING_REPLACE_CHUNKS, &count);
        if (result != SETTING_OK) {
            settings_image_free(&image);
            return result;
        }
    }
    if (source_out != NULL)
        *source_out = source;
    for (i = 0; i < count; i++) {
        exposed[i].sc_ID = chunks[i].id;
        exposed[i].sc_Size = chunks[i].size;
        exposed[i].sc_Data = (APTR)chunks[i].data;
    }
    message.srm_File = (CONST_STRPTR)schema->stem;
    message.srm_Layer = layer;
    message.srm_Chunks = exposed;
    message.srm_Count = count;
    message.srm_Capacity = SETTING_REPLACE_CHUNKS;
    if (read_only) {
        CallHookPkt(hook, &message, NULL);
        settings_image_free(&image);
        return SETTING_OK;
    }
    if (CallHookPkt(hook, &message, NULL) == 0) {
        settings_image_free(&image);
        return SETTING_ERROR_CANCELLED;
    }
    if (message.srm_Count > SETTING_REPLACE_CHUNKS) {
        settings_image_free(&image);
        return SETTING_ERROR_BUFFER;
    }
    /* Dropped chunks (size 0) leave the list; the rest is checked. */
    count = 0;
    for (i = 0; i < message.srm_Count; i++) {
        if (exposed[i].sc_Size == 0)
            continue;
        chunks[count].id = exposed[i].sc_ID;
        chunks[count].size = exposed[i].sc_Size;
        chunks[count].data = (const uint8_t *)exposed[i].sc_Data;
        count++;
    }
    result = settings_iff_check(schema, chunks, count);
    if (result != SETTING_OK) {
        settings_image_free(&image);
        return result;
    }
    joined.si_Length = settings_iff_join_size(chunks, count);
    joined.si_Data = AllocVec(joined.si_Length, MEMF_ANY);
    if (joined.si_Data == NULL) {
        settings_image_free(&image);
        return SETTING_ERROR_MEMORY;
    }
    settings_iff_join(joined.si_Data, chunks, count);
    settings_image_free(&image);

    result = settings_image_write_atomic(SettingsBase, (CONST_STRPTR)path, &joined);
    if (result == SETTING_OK && layer != SETTING_LAYER_APPLIED) {
        BOOL applies = layer == SETTING_LAYER_USER ||
            !layer_file_exists(SettingsBase, schema, SETTING_LAYER_USER);
        if (applies && settings_layer_path(schema, SETTING_LAYER_APPLIED, (STRPTR)path, sizeof(path)))
            result = settings_image_write_atomic(SettingsBase, (CONST_STRPTR)path, &joined);
    }
    settings_image_free(&joined);
    return result;

    AROS_LIBFUNC_EXIT
}
