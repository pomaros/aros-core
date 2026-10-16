/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <string.h>

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
        AROS_LH3(LONG, SetSettingA,

/*  SYNOPSIS */
        AROS_LHA(CONST_STRPTR, key, A0),
        AROS_LHA(CONST_STRPTR, value, A1),
        AROS_LHA(const struct TagItem *, tags, A2),

/*  LOCATION */
        struct SettingsBase *, SettingsBase, 6, Settings)

/*  FUNCTION
        Validates value against the setting's type, range or choices,
        rewrites the field in the prefs file of the chosen layer and
        replaces that file atomically. Writing the user or machine layer
        also recomposes the applied view in ENV:, so IPrefs and muimaster
        see the change through their file notification.

    INPUTS
        key   - "<file>/<group>/<name>".
        value - text form: decimal, 0x hexadecimal, a choice name, a
                comma-separated list of flag names, on/off/true/false/
                yes/no or 0/1 for a bool, a string.
        tags  - SETTING_Layer (ULONG, default SETTING_LAYER_APPLIED):
                    SETTING_LAYER_APPLIED is an editor's Use, the user
                    layer its Save; the machine layer is explicit.

    RESULT
        SETTING_OK, or SETTING_ERROR_UNKNOWN, SETTING_ERROR_NO_SCHEMA,
        SETTING_ERROR_NO_FILE (the layer has no file to start from; the
        user layer starts from the machine's copy), SETTING_ERROR_FORMAT,
        SETTING_ERROR_RANGE (the value is refused and nothing is written),
        SETTING_ERROR_IO (see IoErr(); the old file is intact).

    NOTES
        The replace writes <file>.new beside the file and takes the old
        name in one rename; ram-handler refuses a rename over an existing
        name, so on ENV: the old file is deleted in the packet before.

    SEE ALSO
        GetSettingA(), ReplaceSettingsA()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    const struct settings_schema *schema;
    const struct settings_record *record;
    struct SettingsImage image;
    char path[SETTINGS_FULL_PATH];
    ULONG layer, source;
    LONG result;

    if (value == NULL)
        return SETTING_ERROR_RANGE;
    schema = settings_schema_for_key(SettingsBase, key, &record, &result);
    if (schema == NULL)
        return result;
    layer = GetTagData(SETTING_Layer, SETTING_LAYER_APPLIED, tags);
    if (!settings_layer_path(schema, layer, (STRPTR)path, sizeof(path)))
        return SETTING_ERROR_RANGE;

    result = settings_read_layer(SettingsBase, schema, layer, &image, &source);
    if (result != SETTING_OK)
        return result;
    {
        /* An item can change length: the record then comes back as a new
         * image, which replaces the one read. */
        uint8_t *rebuilt = NULL;
        size_t rebuilt_length = 0;
        result = settings_record_set(image.si_Data, image.si_Length, schema, record,
            (const char *)value, settings_lib_alloc, settings_lib_free, NULL,
            &rebuilt, &rebuilt_length);
        if (result == SETTING_OK && rebuilt != NULL) {
            settings_image_free(&image);
            image.si_Data = rebuilt;
            image.si_Length = rebuilt_length;
        }
    }
    if (result == SETTING_OK)
        result = settings_image_write_atomic(SettingsBase, (CONST_STRPTR)path, &image);
    if (result == SETTING_OK && layer != SETTING_LAYER_APPLIED) {
        /* The applied view follows the winning layer: the user's when it
         * has a copy, the machine's otherwise. */
        BOOL applies = layer == SETTING_LAYER_USER ||
            !layer_file_exists(SettingsBase, schema, SETTING_LAYER_USER);
        if (applies && settings_layer_path(schema, SETTING_LAYER_APPLIED, (STRPTR)path, sizeof(path)))
            result = settings_image_write_atomic(SettingsBase, (CONST_STRPTR)path, &image);
    }
    settings_image_free(&image);
    return result;

    AROS_LIBFUNC_EXIT
}
