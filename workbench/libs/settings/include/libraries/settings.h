#ifndef LIBRARIES_SETTINGS_H
#define LIBRARIES_SETTINGS_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    settings.library: settings by name over the IFF PREF files, described
    by one schema per file (SYS:Prefs/Schemas/<file>.schema).
*/

#include <exec/types.h>
#include <exec/ports.h>
#include <utility/tagitem.h>

/* Results of every function. */
#define SETTING_OK                  0
#define SETTING_ERROR_UNKNOWN       1   /* no schema record for the key      */
#define SETTING_ERROR_NO_SCHEMA     2   /* the file has no schema            */
#define SETTING_ERROR_NO_FILE       3   /* the prefs file is absent          */
#define SETTING_ERROR_FORMAT        4   /* the file is not the schema's form */
#define SETTING_ERROR_RANGE         5   /* value outside type, range, choice */
#define SETTING_ERROR_BUFFER        6   /* caller buffer too small           */
#define SETTING_ERROR_IO            7   /* DOS error, see IoErr()            */
#define SETTING_ERROR_UNSUPPORTED   8   /* not in this library version       */
#define SETTING_ERROR_MEMORY        9
#define SETTING_ERROR_CANCELLED     10  /* ReplaceSettingsA: hook refused    */

/* Tags. */
#define SETTING_TAGBASE             (TAG_USER | 0x00530000)
#define SETTING_Layer               (SETTING_TAGBASE + 1)  /* ULONG, SETTING_LAYER_*         */
#define SETTING_Binary              (SETTING_TAGBASE + 2)  /* BOOL: field bytes, no text     */
#define SETTING_SourceLayer         (SETTING_TAGBASE + 3)  /* ULONG *: layer that answered   */
#define SETTING_SchemaDir           (SETTING_TAGBASE + 4)  /* CONST_STRPTR: schema directory */
#define SETTING_Unsubscribe         (SETTING_TAGBASE + 5)  /* BOOL: NotifySettingA removes      */
#define SETTING_ReadOnly            (SETTING_TAGBASE + 6)  /* BOOL: ReplaceSettingsA reads only */

#define SETTING_LAYER_APPLIED       0   /* ENV:                               */
#define SETTING_LAYER_USER          1   /* HOME:Settings/                     */
#define SETTING_LAYER_MACHINE       2   /* ENVARC:                            */

/* Types a schema record may declare. */
#define SETTING_TYPE_BOOL           1
#define SETTING_TYPE_INT            2
#define SETTING_TYPE_ENUM           3
#define SETTING_TYPE_FLAGS          4
#define SETTING_TYPE_STRING         5
#define SETTING_TYPE_FONT           6
#define SETTING_TYPE_PEN            7
#define SETTING_TYPE_RGB            8
#define SETTING_TYPE_MODEID         9
#define SETTING_TYPE_PATH           10
#define SETTING_TYPE_KEYMAP         11
#define SETTING_TYPE_COUNTRY        12

#define SETTING_LEVEL_NOVICE        0
#define SETTING_LEVEL_AVERAGE       1
#define SETTING_LEVEL_EXPERT        2

#define SETTING_APPLIER_IPREFS      0
#define SETTING_APPLIER_MUIMASTER   1
#define SETTING_APPLIER_SELF        2

#define SETTING_KEY_SIZE            64
#define SETTING_VALUE_SIZE          256

/* Filled by DescribeSettingA() and passed to EnumSettingsA() hooks. */
struct SettingDescription
{
    ULONG         sd_Size;              /* sizeof(struct SettingDescription) */
    CONST_STRPTR  sd_Key;
    CONST_STRPTR  sd_File;              /* path under ENV:/ENVARC:           */
    ULONG         sd_Type;              /* SETTING_TYPE_*                    */
    ULONG         sd_Level;             /* SETTING_LEVEL_*                   */
    ULONG         sd_Applier;           /* SETTING_APPLIER_*                 */
    BOOL          sd_Live;              /* applied on Use, no reboot         */
    LONG          sd_Min;               /* int range                         */
    LONG          sd_Max;
    CONST_STRPTR  sd_Unit;              /* may be NULL                       */
    CONST_STRPTR  sd_Default;           /* may be NULL                       */
    ULONG         sd_ChoiceCount;       /* enum/flags choices                */
    CONST_STRPTR *sd_ChoiceNames;
    const LONG   *sd_ChoiceValues;
    ULONG         sd_Offset;            /* byte offset in the chunk          */
    ULONG         sd_FieldSize;
};

/* Sent by NotifySettingA() subscriptions, one per key whose value changed;
 * the receiver replies it like any message and the library frees it. */
struct SettingNotifyMessage
{
    struct Message snm_Message;
    char           snm_Key[SETTING_KEY_SIZE];
    ULONG          snm_Layer;           /* SETTING_LAYER_APPLIED              */
};

/* ReplaceSettingsA(): the hook is called once as
 * CallHookPkt(hook, struct SettingReplaceMessage *, NULL) with the file's
 * chunks in order (PRHD included). It may edit a chunk's bytes in place,
 * point sc_Data at its own buffer and set sc_Size, set sc_Size to 0 to drop
 * a chunk, or append chunks while srm_Count < srm_Capacity. It returns
 * nonzero to write, zero to cancel (nothing is written). The library
 * checks every schema chunk's size before writing. An absent file is an
 * empty list, so a hook can write a whole new file. With SETTING_ReadOnly
 * the hook only reads the chunks: nothing is written and its return value
 * is ignored; this is how an editor loads a layer. */
struct SettingChunk
{
    ULONG sc_ID;
    ULONG sc_Size;
    APTR  sc_Data;
};

struct SettingReplaceMessage
{
    CONST_STRPTR         srm_File;      /* schema stem, "font"             */
    ULONG                srm_Layer;     /* layer being rewritten           */
    struct SettingChunk *srm_Chunks;
    ULONG                srm_Count;
    ULONG                srm_Capacity;
};

#define SETTING_REPLACE_CHUNKS      32

#endif /* LIBRARIES_SETTINGS_H */
