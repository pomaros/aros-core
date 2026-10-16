#ifndef SETTINGS_PRIVATE_H
#define SETTINGS_PRIVATE_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <exec/ports.h>
#include <dos/notify.h>
#include <libraries/settings.h>

/* DOS and utility are reached through the bases the library opened at
 * init, like every other library of the tree; no global base is assumed. */
#define __DOS_NOLIBBASE__
#define __UTILITY_NOLIBBASE__
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include "settings_schema.h"
#include "settings_iff.h"

#define SETTINGS_SCHEMA_DIR   "SYS:Prefs/Schemas/"
#define SETTINGS_PATH_APPLIED "ENV:"
#define SETTINGS_PATH_MACHINE "ENVARC:"
#define SETTINGS_PATH_USER    "HOME:Settings/"
#define SETTINGS_FULL_PATH    256

struct SettingsSchemaNode
{
    struct MinNode          ssn_Node;
    struct settings_schema *ssn_Schema;
};

/* One watched prefs file: the applied layer's path, its DOS notification
 * and the last decoded value of every record, for the per-key diff. */
struct SettingsWatch
{
    struct MinNode                 sw_Node;
    const struct settings_schema  *sw_Schema;
    char                           sw_Path[SETTINGS_FULL_PATH];
    char                           sw_Directory[SETTINGS_FULL_PATH]; /* watched instead when the file is absent */
    struct NotifyRequest           sw_Request;
    char                          *sw_Last;         /* record_count * SETTING_VALUE_SIZE */
    ULONG                          sw_Subscribers;
    BOOL                           sw_Started;
};

/* One NotifySettingA() subscription: a port and a key or a prefix ("font/"). */
struct SettingsSubscription
{
    struct MinNode                 ss_Node;
    struct MsgPort                *ss_Port;
    struct SettingsWatch          *ss_Watch;
    char                           ss_Key[SETTING_KEY_SIZE];
    ULONG                          ss_Length;
    BOOL                           ss_Prefix;
};

struct SettingsBase
{
    struct Library          sb_LibNode;
    struct Library         *sb_DOSBase;
    struct Library         *sb_UtilityBase;
    struct SignalSemaphore  sb_Lock;        /* schema cache, watches, subscriptions */
    struct MinList          sb_Schemas;     /* struct SettingsSchemaNode      */
    struct MinList          sb_Watches;     /* struct SettingsWatch           */
    struct MinList          sb_Subscriptions; /* struct SettingsSubscription  */
    struct Process         *sb_Watcher;     /* created on the first subscription */
    struct MsgPort         *sb_NotifyPort;  /* the watcher's, DOS notifications */
    struct MsgPort         *sb_ReplyPort;   /* the watcher's, replied messages */
    struct Task            *sb_Creator;     /* handshake while the watcher starts */
};

#define DOSBase     (SettingsBase->sb_DOSBase)
#define UtilityBase (SettingsBase->sb_UtilityBase)

/* A prefs file image in memory, read through DOS. */
struct SettingsImage
{
    UBYTE *si_Data;
    ULONG  si_Length;
};

/* settings_store.c */
void *settings_lib_alloc(void *ctx, size_t size);
void settings_lib_free(void *ctx, void *block, size_t size);
const struct settings_schema *settings_schema_for_key(struct SettingsBase *base,
    CONST_STRPTR key, const struct settings_record **record, LONG *error);
const struct settings_schema *settings_schema_for_stem(struct SettingsBase *base,
    CONST_STRPTR stem, ULONG stem_length, LONG *error);
BOOL settings_layer_path(const struct settings_schema *schema, ULONG layer,
    STRPTR path, ULONG path_size);
LONG settings_image_read(struct SettingsBase *SettingsBase, CONST_STRPTR path,
    struct SettingsImage *image);
void settings_image_free(struct SettingsImage *image);
LONG settings_image_write_atomic(struct SettingsBase *SettingsBase, CONST_STRPTR path,
    const struct SettingsImage *image);
LONG settings_describe(const struct settings_schema *schema,
    const struct settings_record *record, struct SettingDescription *description);
LONG settings_read_layer(struct SettingsBase *SettingsBase, const struct settings_schema *schema,
    ULONG layer, struct SettingsImage *image, ULONG *source_layer);

/* settings_notify.c */
LONG settings_notify_subscribe(struct SettingsBase *SettingsBase,
    const struct settings_schema *schema, CONST_STRPTR key, struct MsgPort *port);
LONG settings_notify_unsubscribe(struct SettingsBase *SettingsBase,
    CONST_STRPTR key, struct MsgPort *port);
LONG settings_code_from_dos(LONG dos_error);

#endif /* SETTINGS_PRIVATE_H */
