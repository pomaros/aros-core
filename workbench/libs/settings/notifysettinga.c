/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <string.h>

#include "settings_private.h"
#include LC_LIBDEFS_FILE

/*****************************************************************************

    NAME */
        AROS_LH3(LONG, NotifySettingA,

/*  SYNOPSIS */
        AROS_LHA(CONST_STRPTR, key, A0),
        AROS_LHA(struct MsgPort *, port, A1),
        AROS_LHA(const struct TagItem *, tags, A2),

/*  LOCATION */
        struct SettingsBase *, SettingsBase, 8, Settings)

/*  FUNCTION
        Subscribes port to changes of one setting, or of every setting under
        a prefix when key ends in "/" ("font/system/", "font/"). The library
        watches the applied file (ENV:) of the key's schema and, when it
        changes, sends one struct SettingNotifyMessage per key whose value
        differs from the last one it saw, to every matching subscription.
        The receiver replies each message; the library frees it.

    INPUTS
        key  - "<file>/<group>/<name>", or a prefix ending in "/".
        port - the subscriber's message port.
        tags - SETTING_Unsubscribe (BOOL): remove the subscription made with
                   the same key and port.

    RESULT
        SETTING_OK, SETTING_ERROR_UNKNOWN (no record under the key, or no
        such subscription), SETTING_ERROR_NO_SCHEMA, SETTING_ERROR_RANGE
        (no port, empty or overlong key), SETTING_ERROR_IO (the watcher or
        the DOS notification could not start, see IoErr()).

    NOTES
        A key is matched by value: an edit that rewrites a file without
        changing the setting sends nothing. The watched layer is the applied
        one; a user or machine layer write recomposes ENV:, so it is seen.

    SEE ALSO
        GetSettingA(), SetSettingA()

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    const struct settings_schema *schema;
    const struct settings_record *record;
    const char *text = (const char *)key;
    LONG result;
    ULONG length;

    if (text == NULL || port == NULL)
        return SETTING_ERROR_RANGE;
    length = strlen(text);
    if (length == 0 || length >= SETTING_KEY_SIZE)
        return SETTING_ERROR_RANGE;
    if (GetTagData(SETTING_Unsubscribe, FALSE, tags))
        return settings_notify_unsubscribe(SettingsBase, key, port);
    if (text[length - 1] == '/') {
        const char *slash = strchr(text, '/');
        ULONG i, hits = 0;
        schema = settings_schema_for_stem(SettingsBase, key, (ULONG)(slash - text), &result);
        if (schema == NULL)
            return result;
        for (i = 0; i < schema->record_count; i++)
            if (strncmp(schema->records[i].key, text, length) == 0)
                hits++;
        if (hits == 0)
            return SETTING_ERROR_UNKNOWN;
    } else {
        schema = settings_schema_for_key(SettingsBase, key, &record, &result);
        if (schema == NULL)
            return result;
    }
    return settings_notify_subscribe(SettingsBase, schema, key, port);

    AROS_LIBFUNC_EXIT
}
