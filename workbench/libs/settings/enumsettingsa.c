/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <string.h>

#include "settings_private.h"
#include LC_LIBDEFS_FILE

/* Calls the hook for every record of one schema under prefix; returns
 * TRUE when the hook asked to stop. */
static BOOL enum_schema(struct SettingsBase *SettingsBase,
    const struct settings_schema *schema, const char *prefix, struct Hook *hook)
{
    struct SettingDescription description;
    ULONG prefix_length = strlen(prefix), i;
    for (i = 0; i < schema->record_count; i++) {
        const struct settings_record *record = &schema->records[i];
        if (strncmp(record->key, prefix, prefix_length) != 0)
            continue;
        settings_describe(schema, record, &description);
        if (CallHookPkt(hook, &description, NULL) != 0)
            return TRUE;
    }
    return FALSE;
}

/*****************************************************************************

    NAME */
        AROS_LH3(LONG, EnumSettingsA,

/*  SYNOPSIS */
        AROS_LHA(CONST_STRPTR, prefix, A0),
        AROS_LHA(struct Hook *, hook, A1),
        AROS_LHA(const struct TagItem *, tags, A2),

/*  LOCATION */
        struct SettingsBase *, SettingsBase, 9, Settings)

/*  FUNCTION
        Calls hook once per schema record whose key starts with prefix,
        with a struct SettingDescription as the hook's object. An empty
        prefix walks every schema in SYS:Prefs/Schemas. A nonzero hook
        result stops the walk.

    INPUTS
        prefix - "" or NULL for everything, "font/" for one file,
                 "font/system/" for one group.
        hook   - h_Entry(hook, struct SettingDescription *, NULL).
        tags   - none defined.

    RESULT
        SETTING_OK, SETTING_ERROR_NO_SCHEMA when prefix names a file with
        no schema, SETTING_ERROR_IO when the schema directory cannot be
        read.

    SEE ALSO
        DescribeSettingA()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    const struct settings_schema *schema;
    LONG result = SETTING_OK;

    (void)tags;
    if (hook == NULL)
        return SETTING_ERROR_RANGE;
    if (prefix != NULL && prefix[0] != '\0') {
        const char *text = (const char *)prefix;
        const char *slash = strchr(text, '/');
        ULONG stem_length = slash != NULL ? (ULONG)(slash - text) : strlen(text);
        schema = settings_schema_for_stem(SettingsBase, prefix, stem_length, &result);
        if (schema == NULL)
            return result;
        enum_schema(SettingsBase, schema, slash != NULL ? text : "", hook);
        return SETTING_OK;
    }
    {
        BPTR lock = Lock((CONST_STRPTR)SETTINGS_SCHEMA_DIR, SHARED_LOCK);
        struct FileInfoBlock *fib;
        if (lock == BNULL)
            return settings_code_from_dos(IoErr());
        fib = AllocDosObject(DOS_FIB, NULL);
        if (fib == NULL) {
            UnLock(lock);
            return SETTING_ERROR_MEMORY;
        }
        if (Examine(lock, fib)) {
            while (ExNext(lock, fib)) {
                ULONG name_length = strlen(fib->fib_FileName);
                if (fib->fib_DirEntryType > 0 || name_length <= 7 ||
                    strcmp(fib->fib_FileName + name_length - 7, ".schema") != 0)
                    continue;
                schema = settings_schema_for_stem(SettingsBase,
                    (CONST_STRPTR)fib->fib_FileName, name_length - 7, &result);
                if (schema != NULL && enum_schema(SettingsBase, schema, "", hook))
                    break;
            }
            result = SETTING_OK;
        } else {
            result = settings_code_from_dos(IoErr());
        }
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
    }
    return result;

    AROS_LIBFUNC_EXIT
}
