/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <libraries/mui.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "editor.h"
#include "report.h"
#include <filesystems/afsplus_ext_packet.h>
#include <filesystems/afsplus_aros.h>
#include LC_LIBDEFS_FILE
#include <dos/dosextens.h>
#include <exec/memory.h>

static int32_t storage_send(void *context, struct AfsplusExtRequest *request)
{
    if (DoPkt((struct MsgPort *)context, ACTION_AFSPLUS_EXT, (SIPTR)request, 0, 0, 0, 0)) return 0;
    return IoErr() ? IoErr() : ERROR_UNKNOWN;
}

static void snapshot_entry(void *context, uint64_t id, uint64_t generation, uint64_t tx)
{
    Object *list = context;
    char text[128];
    snprintf(text, sizeof(text), "%llu  (checkpoint %llu, transaction %llu)",
        (unsigned long long)id, (unsigned long long)generation, (unsigned long long)tx);
    DoMethod(list, MUIM_List_InsertSingle, (IPTR)text, MUIV_List_Insert_Bottom);
}

IPTR StorageEditor__MUIM_StorageEditor_Snapshots(Class *CLASS, Object *self, Msg message)
{
    struct StorageEditor_DATA *data = INST_DATA(CLASS, self);
    STRPTR volume = NULL;
    BPTR lock;
    uint32_t total = 0;
    int incomplete = 0;
    LONG error = 0;
    char text[160];
    DoMethod(data->snapshots, MUIM_List_Clear);
    DoMethod(data->volumes, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (IPTR)&volume);
    if (!volume || !(lock = Lock(volume, ACCESS_READ)))
    {
        SET(data->snapshot_status, MUIA_Text_Contents, (IPTR)"Select an available volume to list snapshots");
        return FALSE;
    }
    error = storage_snapshots(((struct FileLock *)BADDR(lock))->fl_Task, storage_send,
        snapshot_entry, data->snapshots, &total, &incomplete);
    if (error == -1) error = ERROR_BAD_NUMBER;
    UnLock(lock);
    if (error)
    {
        DoMethod(data->snapshots, MUIM_List_Clear);
        snprintf(text, sizeof(text), "Snapshots unavailable (DOS error %ld); the volume must support snapshots and authorize access", (long)error);
    }
    else snprintf(text, sizeof(text), "%lu snapshot(s)%s", (unsigned long)total,
        incomplete ? "; list limit reached, use AFSPlusSnapshot for the complete inventory" : "");
    SET(data->snapshot_status, MUIA_Text_Contents, (IPTR)text);
    return error == 0;
}

IPTR StorageEditor__MUIM_StorageEditor_DeleteSnapshot(Class *CLASS, Object *self, Msg message)
{
    struct StorageEditor_DATA *data = INST_DATA(CLASS, self);
    STRPTR volume = NULL, entry = NULL;
    uint64_t id;
    BPTR lock;
    LONG error;
    char text[384], volume_name[258];
    IPTR requester_args[1];
    DoMethod(data->volumes, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (IPTR)&volume);
    DoMethod(data->snapshots, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (IPTR)&entry);
    if (!volume || !entry) return FALSE;
    id = strtoull((const char *)entry, NULL, 10);
    snprintf(volume_name, sizeof(volume_name), "%s", volume);
    snprintf(text, sizeof(text), "Delete snapshot %llu from %s?", (unsigned long long)id, volume);
    lock = Lock((CONST_STRPTR)volume_name, ACCESS_READ);
    if (!lock)
    {
        snprintf(text, sizeof(text), "Volume unavailable (DOS error %ld)", (long)IoErr());
        SET(data->snapshot_status, MUIA_Text_Contents, (IPTR)text);
        return FALSE;
    }
    /* Bind the confirmation to this mounted volume, even if another process
       changes a volume label while the requester is open. */
    requester_args[0] = (IPTR)text;
    if (MUI_RequestA(_app(self), _win(self), 0, "Delete snapshot", "_Delete|_Cancel", "%s", requester_args) != 1)
    {
        UnLock(lock);
        return FALSE;
    }
    error = storage_snapshot_delete(((struct FileLock *)BADDR(lock))->fl_Task, storage_send, id);
    if (error == -1) error = ERROR_BAD_NUMBER;
    UnLock(lock);
    if (error)
    {
        snprintf(text, sizeof(text), "Snapshot deletion failed (DOS error %ld)%s", (long)error,
            error == ERROR_OBJECT_IN_USE ? "; close its open readers before deleting" : "");
        SET(data->snapshot_status, MUIA_Text_Contents, (IPTR)text);
        return FALSE;
    }
    return DoMethod(self, MUIM_StorageEditor_Snapshots);
}

IPTR StorageEditor__MUIM_StorageEditor_Refresh(Class *CLASS, Object *self, Msg message)
{
    struct StorageEditor_DATA *data = INST_DATA(CLASS, self);
    struct InfoData info;
    STRPTR volume = NULL;
    BPTR lock;
    char text[1024], diagnostics[768];
    DoMethod(data->snapshots, MUIM_List_Clear);
    DoMethod(data->volumes, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (IPTR)&volume);
    if (!volume)
    {
        SET(data->status, MUIA_Text_Contents, (IPTR)"Select a mounted volume to read its space");
        return 0;
    }
    /* Never hold the DOS list lock across filesystem packets. */
    lock = Lock(volume, ACCESS_READ);
    if (!lock)
    {
        SET(data->status, MUIA_Text_Contents, (IPTR)"Volume unavailable; rescan mounted volumes");
        return 0;
    }
    if (Info(lock, &info) && info.id_BytesPerBlock > 0 &&
#if !__DOS64
        info.id_NumBlocks >= 0 && info.id_NumBlocksUsed >= 0 &&
#endif
        info.id_NumBlocksUsed <= info.id_NumBlocks)
    {
        /* Divide first so large block counts cannot overflow a byte product. */
        UQUAD total = info.id_NumBlocks, free = total - info.id_NumBlocksUsed;
        UQUAD totalMiB = (total / 1048576) * info.id_BytesPerBlock +
            (total % 1048576) * info.id_BytesPerBlock / 1048576;
        UQUAD freeMiB = (free / 1048576) * info.id_BytesPerBlock +
            (free % 1048576) * info.id_BytesPerBlock / 1048576;
        snprintf(text, sizeof(text), "%s\nCapacity: %llu MiB\nAvailable: %llu MiB\n%s",
            volume, (unsigned long long)totalMiB, (unsigned long long)freeMiB,
            info.id_DiskState == ID_WRITE_PROTECTED ? "Read only" :
            info.id_DiskState == ID_VALIDATING ? "Validating" : "Mounted");
    }
    else
        snprintf(text, sizeof(text), "%s\nSpace information unavailable", volume);
    storage_report(((struct FileLock *)BADDR(lock))->fl_Task, storage_send,
        diagnostics, sizeof(diagnostics));
    strncat(text, "\n", sizeof(text) - strlen(text) - 1);
    strncat(text, diagnostics, sizeof(text) - strlen(text) - 1);
    UnLock(lock);
    SET(data->status, MUIA_Text_Contents, (IPTR)text);
    DoMethod(self, MUIM_StorageEditor_Snapshots);
    return 0;
}

IPTR StorageEditor__MUIM_StorageEditor_Rescan(Class *CLASS, Object *self, Msg message)
{
    struct StorageEditor_DATA *data = INST_DATA(CLASS, self);
    struct VolumeName { struct VolumeName *next; char name[258]; };
    struct VolumeName *first = NULL, **tail = &first, *item;
    struct DosList *entry;
    BOOL incomplete = FALSE;
    /* Snapshot names only while locked: even inserting a MUI list entry can
       synchronously notify Active and dispatch a filesystem query. */
    entry = LockDosList(LDF_VOLUMES | LDF_READ);
    if (entry)
    {
        while ((entry = NextDosEntry(entry, LDF_VOLUMES)))
        {
            ULONG length = AROS_BSTR_strlen(entry->dol_Name);
            if (!entry->dol_Task || length == 0 || length > 255) continue;
            item = AllocVec(sizeof(*item), MEMF_ANY);
            if (!item) { incomplete = TRUE; break; }
            item->next = NULL;
            memcpy(item->name, AROS_BSTR_ADDR(entry->dol_Name), length);
            item->name[length] = ':';
            item->name[length + 1] = 0;
            *tail = item;
            tail = &item->next;
        }
        UnLockDosList(LDF_VOLUMES | LDF_READ);
    }
    else incomplete = TRUE;
    DoMethod(data->volumes, MUIM_List_Clear);
    while ((item = first))
    {
        first = item->next;
        DoMethod(data->volumes, MUIM_List_InsertSingle, (IPTR)item->name, MUIV_List_Insert_Bottom);
        FreeVec(item);
    }
    SET(data->status, MUIA_Text_Contents, (IPTR)(incomplete ?
        "Volume list incomplete; rescan to retry" : "Select a mounted volume to read its space"));
    return 0;
}

IPTR StorageEditor__OM_NEW(Class *CLASS, Object *self, struct opSet *message)
{
    Object *status, *volumes, *refresh, *rescan, *snapshots, *snapshot_status, *delete_snapshot;
    self = (Object *)DoSuperNewTags(CLASS, self, NULL,
        MUIA_PrefsEditor_Name, (IPTR)"Storage",
        MUIA_PrefsEditor_CanSave, FALSE,
        MUIA_PrefsEditor_CanUse, FALSE,
        MUIA_PrefsEditor_CanTest, FALSE,
        Child, (IPTR)ListviewObject,
            MUIA_ObjectID, MAKE_ID('S','T','V','L'),
            MUIA_ShortHelp, (IPTR)"Mounted volumes; select one to inspect capacity and available space",
            MUIA_Listview_List, (IPTR)(volumes = ListObject,
                InputListFrame,
                MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                MUIA_List_DestructHook, MUIV_List_DestructHook_String,
            End),
        End,
        Child, (IPTR)(status = TextObject,
            MUIA_ObjectID, MAKE_ID('S','T','S','P'),
            MUIA_ShortHelp, (IPTR)"Capacity, health and volume identity",
            MUIA_Text_Contents, (IPTR)"Select a mounted volume to read its space",
        End),
        Child, (IPTR)HGroup,
            Child, (IPTR)(refresh = SimpleButton("Refresh storage")),
            Child, (IPTR)(rescan = SimpleButton("Rescan volumes")),
        End,
        Child, (IPTR)ListviewObject,
            MUIA_ObjectID, MAKE_ID('S','T','S','L'),
            MUIA_ShortHelp, (IPTR)"Snapshots of the selected volume; select a snapshot to delete it",
            MUIA_Listview_List, (IPTR)(snapshots = ListObject,
                InputListFrame,
                MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                MUIA_List_DestructHook, MUIV_List_DestructHook_String,
            End),
        End,
        Child, (IPTR)(snapshot_status = TextObject,
            MUIA_ObjectID, MAKE_ID('S','T','S','T'),
            MUIA_Text_Contents, (IPTR)"Select a volume to list snapshots",
        End),
        Child, (IPTR)(delete_snapshot = SimpleButton("Delete selected snapshot")),
        TAG_MORE, (IPTR)message->ops_AttrList);
    if (self)
    {
        struct StorageEditor_DATA *data = INST_DATA(CLASS, self);
        data->status = status;
        data->volumes = volumes;
        data->snapshots = snapshots;
        data->snapshot_status = snapshot_status;
        SET(delete_snapshot, MUIA_ObjectID, MAKE_ID('S','T','S','D'));
        SET(delete_snapshot, MUIA_ShortHelp, (IPTR)"Confirm deletion of the selected snapshot; open readers prevent deletion");
        DoMethod(delete_snapshot, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)self, 1, MUIM_StorageEditor_DeleteSnapshot);
        SET(refresh, MUIA_ObjectID, MAKE_ID('S','T','R','F'));
        SET(refresh, MUIA_ShortHelp, (IPTR)"Read capacity and health from the selected volume");
        SET(rescan, MUIA_ObjectID, MAKE_ID('S','T','R','S'));
        SET(rescan, MUIA_ShortHelp, (IPTR)"Reload the list of mounted volumes");
        DoMethod(refresh, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)self, 1, MUIM_StorageEditor_Refresh);
        DoMethod(rescan, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)self, 1, MUIM_StorageEditor_Rescan);
        DoMethod(volumes, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, (IPTR)self, 1, MUIM_StorageEditor_Refresh);
        DoMethod(self, MUIM_StorageEditor_Rescan);
    }
    return (IPTR)self;
}
IPTR StorageEditor__MUIM_PrefsEditor_ImportFromDirectory(Class *CLASS, Object *self, Msg message)
{
    return TRUE;
}
IPTR StorageEditor__MUIM_PrefsEditor_ExportToDirectory(Class *CLASS, Object *self, Msg message)
{
    return TRUE;
}

IPTR StorageEditor__OM_GET(Class *CLASS, Object *self, struct opGet *message)
{
    if (message->opg_AttrID == MUIA_PrefsEditor_CanSave)
    {
        *message->opg_Storage = FALSE;
        return TRUE;
    }
    return DoSuperMethodA(CLASS, self, (Msg)message);
}
