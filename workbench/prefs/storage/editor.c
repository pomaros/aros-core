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
#include "editor.h"
#include LC_LIBDEFS_FILE
#include <dos/dosextens.h>
#include <exec/memory.h>

IPTR StorageEditor__MUIM_StorageEditor_Refresh(Class *CLASS, Object *self, Msg message)
{
    struct StorageEditor_DATA *data = INST_DATA(CLASS, self);
    struct InfoData info;
    STRPTR volume = NULL;
    BPTR lock;
    char text[512];
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
    UnLock(lock);
    SET(data->status, MUIA_Text_Contents, (IPTR)text);
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
    Object *status, *volumes, *refresh, *rescan;
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
            MUIA_ShortHelp, (IPTR)"Capacity and available space in mebibytes",
            MUIA_Text_Contents, (IPTR)"Select a mounted volume to read its space",
        End),
        Child, (IPTR)HGroup,
            Child, (IPTR)(refresh = SimpleButton("Refresh space")),
            Child, (IPTR)(rescan = SimpleButton("Rescan volumes")),
        End,
        TAG_MORE, (IPTR)message->ops_AttrList);
    if (self)
    {
        struct StorageEditor_DATA *data = INST_DATA(CLASS, self);
        data->status = status;
        data->volumes = volumes;
        SET(refresh, MUIA_ObjectID, MAKE_ID('S','T','R','F'));
        SET(refresh, MUIA_ShortHelp, (IPTR)"Read space information from the selected volume");
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
