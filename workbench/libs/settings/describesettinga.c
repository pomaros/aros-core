/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/

#include "settings_private.h"
#include LC_LIBDEFS_FILE

/*****************************************************************************

    NAME */
        AROS_LH3(LONG, DescribeSettingA,

/*  SYNOPSIS */
        AROS_LHA(CONST_STRPTR, key, A0),
        AROS_LHA(struct SettingDescription *, description, A1),
        AROS_LHA(const struct TagItem *, tags, A2),

/*  LOCATION */
        struct SettingsBase *, SettingsBase, 10, Settings)

/*  FUNCTION
        Fills description with the schema record of a setting: type,
        level, applier, range, unit, default, choices and the field's
        place in the chunk. The strings and arrays belong to the library
        and stay valid while it is open.

    INPUTS
        key         - "<file>/<group>/<name>".
        description - a struct SettingDescription; sd_Size is set by the
                      library.
        tags        - none defined.

    RESULT
        SETTING_OK, SETTING_ERROR_UNKNOWN or SETTING_ERROR_NO_SCHEMA.

    SEE ALSO
        EnumSettingsA(), GetSettingA()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    const struct settings_schema *schema;
    const struct settings_record *record;
    LONG result;

    (void)tags;
    if (description == NULL)
        return SETTING_ERROR_BUFFER;
    schema = settings_schema_for_key(SettingsBase, key, &record, &result);
    if (schema == NULL)
        return result;
    return settings_describe(schema, record, description);

    AROS_LIBFUNC_EXIT
}
