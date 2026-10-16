/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Per-setting notification. One watcher process, created on the first
    subscription, holds one StartNotify() per watched file on the applied
    layer (ENV:), keeps the last decoded value of every record, and on a
    change sends one SettingNotifyMessage per key whose value differs to
    every subscription whose key or prefix matches.
*/
#include <string.h>
#include <dos/dostags.h>
#include <exec/memory.h>

#include "settings_private.h"
#include <aros/debug.h>
#define NTRACE(text) kprintf("[settings] %s\n", text)

#define WATCHER_STACK 32768

static struct SettingsWatch *find_watch(struct SettingsBase *SettingsBase,
    const struct settings_schema *schema)
{
    struct SettingsWatch *watch;
    ForeachNode(&SettingsBase->sb_Watches, watch)
        if (watch->sw_Schema == schema)
            return watch;
    return NULL;
}

/* Decodes every record of the watch's file into values; absent file or
 * chunk leaves the value empty. values holds record_count entries. */
/* Returns FALSE when the file could not be read; values are then empty. */
static BOOL snapshot(struct SettingsBase *SettingsBase, struct SettingsWatch *watch,
    char *values)
{
    const struct settings_schema *schema = watch->sw_Schema;
    struct SettingsImage image;
    ULONG i;

    for (i = 0; i < schema->record_count; i++)
        values[i * SETTING_VALUE_SIZE] = '\0';
    if (settings_image_read(SettingsBase, (CONST_STRPTR)watch->sw_Path, &image) != SETTING_OK)
        return FALSE;
    for (i = 0; i < schema->record_count; i++)
        settings_record_get(image.si_Data, image.si_Length, schema, &schema->records[i],
            values + i * SETTING_VALUE_SIZE, SETTING_VALUE_SIZE);
    settings_image_free(&image);
    return TRUE;
}

static BOOL subscription_matches(const struct SettingsSubscription *subscription,
    const char *key)
{
    if (subscription->ss_Prefix)
        return strncmp(subscription->ss_Key, key, subscription->ss_Length) == 0;
    return strcmp(subscription->ss_Key, key) == 0;
}

/* Called by the watcher with the lock held: re-reads the file, compares
 * with the last snapshot and sends one message per changed key. */
static void publish_changes(struct SettingsBase *SettingsBase, struct SettingsWatch *watch)
{
    const struct settings_schema *schema = watch->sw_Schema;
    char *fresh = AllocVec(schema->record_count * SETTING_VALUE_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    ULONG i;

    if (fresh == NULL)
        return;
    /* An absent file is the window of an atomic replace (delete, then
     * rename on handlers that refuse a rename over an existing name); the
     * next notification brings the new file. Publish nothing meanwhile. */
    if (!snapshot(SettingsBase, watch, fresh)) {
        FreeVec(fresh);
        return;
    }
    for (i = 0; i < schema->record_count; i++) {
        const char *before = watch->sw_Last + i * SETTING_VALUE_SIZE;
        const char *after = fresh + i * SETTING_VALUE_SIZE;
        struct SettingsSubscription *subscription;
        if (strcmp(before, after) == 0)
            continue;
        ForeachNode(&SettingsBase->sb_Subscriptions, subscription) {
            struct SettingNotifyMessage *message;
            if (subscription->ss_Watch != watch ||
                !subscription_matches(subscription, schema->records[i].key))
                continue;
            message = AllocVec(sizeof(*message), MEMF_PUBLIC | MEMF_CLEAR);
            if (message == NULL)
                continue;
            message->snm_Message.mn_Node.ln_Type = NT_MESSAGE;
            message->snm_Message.mn_ReplyPort = SettingsBase->sb_ReplyPort;
            message->snm_Message.mn_Length = sizeof(*message);
            strncpy(message->snm_Key, schema->records[i].key, SETTING_KEY_SIZE - 1);
            message->snm_Layer = SETTING_LAYER_APPLIED;
            PutMsg(subscription->ss_Port, &message->snm_Message);
        }
    }
    CopyMem(fresh, watch->sw_Last, schema->record_count * SETTING_VALUE_SIZE);
    FreeVec(fresh);
}

static AROS_PROCH(settings_watcher, argptr, argsize, SysBase)
{
    AROS_PROCFUNC_INIT

    struct SettingsBase *SettingsBase =
        (struct SettingsBase *)FindTask(NULL)->tc_UserData;
    struct MsgPort *notify_port = CreateMsgPort();
    struct MsgPort *reply_port = CreateMsgPort();
    ULONG notify_mask, reply_mask;

    (void)argptr; (void)argsize;
    NTRACE("watcher: started");
    if (notify_port == NULL || reply_port == NULL) {
        if (notify_port != NULL) DeleteMsgPort(notify_port);
        if (reply_port != NULL) DeleteMsgPort(reply_port);
        Signal(SettingsBase->sb_Creator, SIGF_SINGLE);
        return 0;
    }
    SettingsBase->sb_NotifyPort = notify_port;
    SettingsBase->sb_ReplyPort = reply_port;
    notify_mask = 1UL << notify_port->mp_SigBit;
    reply_mask = 1UL << reply_port->mp_SigBit;
    NTRACE("watcher: ports ready, signalling creator");
    Signal(SettingsBase->sb_Creator, SIGF_SINGLE);

    for (;;) {
        struct NotifyMessage *notification;
        struct Message *replied;
        Wait(notify_mask | reply_mask);
        while ((replied = GetMsg(reply_port)) != NULL)
            FreeVec(replied);
        while ((notification = (struct NotifyMessage *)GetMsg(notify_port)) != NULL) {
            struct SettingsWatch *watch;
            ObtainSemaphore(&SettingsBase->sb_Lock);
            ForeachNode(&SettingsBase->sb_Watches, watch) {
                if (&watch->sw_Request == notification->nm_NReq) {
                    NTRACE("watcher: file changed, publishing");
                    publish_changes(SettingsBase, watch);
                    break;
                }
            }
            ReleaseSemaphore(&SettingsBase->sb_Lock);
            ReplyMsg(&notification->nm_ExecMessage);
        }
    }
    return 0;

    AROS_PROCFUNC_EXIT
}

/* Lock held. Starts the watcher once; returns FALSE when it did not come up. */
static BOOL ensure_watcher(struct SettingsBase *SettingsBase)
{
    if (SettingsBase->sb_NotifyPort != NULL)
        return TRUE;
    SettingsBase->sb_Creator = FindTask(NULL);
    SetSignal(0, SIGF_SINGLE);
    NTRACE("creating watcher process");
    SettingsBase->sb_Watcher = CreateNewProcTags(
        NP_Entry, (IPTR)settings_watcher,
        NP_Name, (IPTR)"settings.library watcher",
        NP_Priority, 1,
        NP_StackSize, WATCHER_STACK,
        NP_UserData, (IPTR)SettingsBase,
        NP_Input, (IPTR)BNULL,
        NP_Output, (IPTR)BNULL,
        NP_Error, (IPTR)BNULL,
        TAG_DONE);
    if (SettingsBase->sb_Watcher == NULL) {
        NTRACE("CreateNewProc failed");
        return FALSE;
    }
    NTRACE("waiting for the watcher");
    Wait(SIGF_SINGLE);
    NTRACE("watcher answered");
    return SettingsBase->sb_NotifyPort != NULL;
}

/* Lock held. Finds or creates the watch of a schema's applied file. */
static struct SettingsWatch *ensure_watch(struct SettingsBase *SettingsBase,
    const struct settings_schema *schema)
{
    struct SettingsWatch *watch = find_watch(SettingsBase, schema);
    if (watch != NULL)
        return watch;
    watch = AllocVec(sizeof(*watch), MEMF_PUBLIC | MEMF_CLEAR);
    if (watch == NULL)
        return NULL;
    watch->sw_Schema = schema;
    watch->sw_Last = AllocVec(schema->record_count * SETTING_VALUE_SIZE,
        MEMF_PUBLIC | MEMF_CLEAR);
    if (watch->sw_Last == NULL ||
        !settings_layer_path(schema, SETTING_LAYER_APPLIED, (STRPTR)watch->sw_Path,
            sizeof(watch->sw_Path))) {
        if (watch->sw_Last != NULL)
            FreeVec(watch->sw_Last);
        FreeVec(watch);
        return NULL;
    }
    snapshot(SettingsBase, watch, watch->sw_Last);
    watch->sw_Request.nr_Name = (STRPTR)watch->sw_Path;
    watch->sw_Request.nr_Flags = NRF_SEND_MESSAGE;
    watch->sw_Request.nr_stuff.nr_Msg.nr_Port = SettingsBase->sb_NotifyPort;
    NTRACE("StartNotify on the file");
    if (!StartNotify(&watch->sw_Request)) {
        /* A file that has not been saved yet cannot be watched by name on
         * every handler; watch its directory and re-read the file on any
         * change there. The path keeps the file for the re-read. */
        char *slash = strrchr(watch->sw_Path, '/');
        LONG error = IoErr();
        if (error == ERROR_OBJECT_NOT_FOUND && slash != NULL) {
            strcpy(watch->sw_Directory, watch->sw_Path);
            watch->sw_Directory[slash - watch->sw_Path] = '\0';
            watch->sw_Request.nr_Name = (STRPTR)watch->sw_Directory;
        }
        if (error != ERROR_OBJECT_NOT_FOUND || slash == NULL ||
            !StartNotify(&watch->sw_Request)) {
            FreeVec(watch->sw_Last);
            FreeVec(watch);
            return NULL;
        }
    }
    watch->sw_Started = TRUE;
    NTRACE("watch started");
    AddTail((struct List *)&SettingsBase->sb_Watches, (struct Node *)watch);
    return watch;
}

static void drop_watch(struct SettingsBase *SettingsBase, struct SettingsWatch *watch)
{
    (void)SettingsBase;
    Remove((struct Node *)watch);
    if (watch->sw_Started)
        EndNotify(&watch->sw_Request);
    FreeVec(watch->sw_Last);
    FreeVec(watch);
}

LONG settings_notify_subscribe(struct SettingsBase *SettingsBase,
    const struct settings_schema *schema, CONST_STRPTR key, struct MsgPort *port)
{
    struct SettingsSubscription *subscription;
    struct SettingsWatch *watch;
    const char *text = (const char *)key;
    ULONG length = strlen(text);
    LONG result = SETTING_OK;

    if (length == 0 || length >= SETTING_KEY_SIZE || port == NULL)
        return SETTING_ERROR_RANGE;
    subscription = AllocVec(sizeof(*subscription), MEMF_PUBLIC | MEMF_CLEAR);
    if (subscription == NULL)
        return SETTING_ERROR_MEMORY;
    ObtainSemaphore(&SettingsBase->sb_Lock);
    if (!ensure_watcher(SettingsBase)) {
        result = SETTING_ERROR_IO;
    } else {
        watch = ensure_watch(SettingsBase, schema);
        if (watch == NULL) {
            result = SETTING_ERROR_IO;
        } else {
            strcpy(subscription->ss_Key, text);
            subscription->ss_Length = length;
            subscription->ss_Prefix = text[length - 1] == '/';
            subscription->ss_Port = port;
            subscription->ss_Watch = watch;
            watch->sw_Subscribers++;
            AddTail((struct List *)&SettingsBase->sb_Subscriptions, (struct Node *)subscription);
        }
    }
    ReleaseSemaphore(&SettingsBase->sb_Lock);
    if (result != SETTING_OK)
        FreeVec(subscription);
    return result;
}

LONG settings_notify_unsubscribe(struct SettingsBase *SettingsBase,
    CONST_STRPTR key, struct MsgPort *port)
{
    struct SettingsSubscription *subscription, *next;
    LONG result = SETTING_ERROR_UNKNOWN;

    ObtainSemaphore(&SettingsBase->sb_Lock);
    ForeachNodeSafe(&SettingsBase->sb_Subscriptions, subscription, next) {
        if (subscription->ss_Port != port ||
            strcmp(subscription->ss_Key, (const char *)key) != 0)
            continue;
        Remove((struct Node *)subscription);
        if (--subscription->ss_Watch->sw_Subscribers == 0)
            drop_watch(SettingsBase, subscription->ss_Watch);
        FreeVec(subscription);
        result = SETTING_OK;
    }
    ReleaseSemaphore(&SettingsBase->sb_Lock);
    return result;
}
