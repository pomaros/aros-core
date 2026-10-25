/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    ScreenMode's Settings store shares the CLI's user layer. The complete
    SCRM record is exchanged atomically; foreign chunks keep their contents.
*/
#ifndef SCREENSETTINGS_H
#define SCREENSETTINGS_H
#include <libraries/settings.h>
#include <proto/settings.h>
#include <aros/asmcall.h>
#include "../../include/scaleresult.h"

struct ScreenSettingsContext
{
    UBYTE header[6];
    struct ScreenModePrefs prefs;
    BOOL found, store;
};

static void ScreenSettings_Encode(struct ScreenModePrefs *disk)
{
    *disk = screenmodeprefs;
    disk->smp_Scale = AROS_LONG2BE(disk->smp_Scale);
    disk->smp_DisplayID = AROS_LONG2BE(disk->smp_DisplayID);
    disk->smp_Width = AROS_WORD2BE(disk->smp_Width);
    disk->smp_Height = AROS_WORD2BE(disk->smp_Height);
    disk->smp_Depth = AROS_WORD2BE(disk->smp_Depth);
    disk->smp_Control = AROS_WORD2BE(disk->smp_Control);
}

static void ScreenSettings_Decode(const struct ScreenModePrefs *disk)
{
    screenmodeprefs = *disk;
    screenmodeprefs.smp_Scale = AROS_BE2LONG(disk->smp_Scale);
    if (screenmodeprefs.smp_Scale < 100 || screenmodeprefs.smp_Scale > 400 || screenmodeprefs.smp_Scale % 25)
        screenmodeprefs.smp_Scale = 100;
    screenmodeprefs.smp_DisplayID = AROS_BE2LONG(disk->smp_DisplayID);
    screenmodeprefs.smp_Width = AROS_BE2WORD(disk->smp_Width);
    screenmodeprefs.smp_Height = AROS_BE2WORD(disk->smp_Height);
    screenmodeprefs.smp_Depth = AROS_BE2WORD(disk->smp_Depth);
    screenmodeprefs.smp_Control = AROS_BE2WORD(disk->smp_Control);
}

AROS_UFH3(IPTR, ScreenSettings_Hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(APTR, unused, A1))
{
    AROS_USERFUNC_INIT
    struct ScreenSettingsContext *context = hook->h_Data;
    struct SettingReplaceMessage *message = object;
    BOOL header = FALSE;
    ULONG i;
    (void)unused;
    for (i = 0; i < message->srm_Count; ++i)
    {
        struct SettingChunk *chunk = &message->srm_Chunks[i];
        if (chunk->sc_ID == ID_PRHD) header = TRUE;
        if (chunk->sc_ID != ID_SCRM) continue;
        if (chunk->sc_Size != sizeof(context->prefs)) return FALSE;
        if (context->store)
            chunk->sc_Data = &context->prefs;
        else
            memcpy(&context->prefs, chunk->sc_Data, sizeof(context->prefs));
        context->found = TRUE;
    }
    if (!context->store) return TRUE;
    if (message->srm_Count + !header + !context->found > message->srm_Capacity)
        return FALSE;
    if (!header)
    {
        struct SettingChunk *chunk = &message->srm_Chunks[message->srm_Count++];
        chunk->sc_ID = ID_PRHD;
        chunk->sc_Size = sizeof(context->header);
        chunk->sc_Data = context->header;
    }
    if (!context->found)
    {
        struct SettingChunk *chunk = &message->srm_Chunks[message->srm_Count++];
        chunk->sc_ID = ID_SCRM;
        chunk->sc_Size = sizeof(context->prefs);
        chunk->sc_Data = &context->prefs;
    }
    return TRUE;
    AROS_USERFUNC_EXIT
}

static LONG ScreenSettings_Transfer(ULONG layer, BOOL store)
{
    struct Library *SettingsBase = OpenLibrary("settings.library", 1);
    struct ScreenSettingsContext context = {0};
    struct Hook hook = {0};
    struct TagItem tags[] = {
        {SETTING_Layer, layer}, {SETTING_ReadOnly, !store}, {TAG_DONE, 0}
    };
    LONG result;
    if (!SettingsBase) return SETTING_ERROR_UNSUPPORTED;
    context.store = store;
    context.header[0] = PHV_CURRENT;
    if (store) ScreenSettings_Encode(&context.prefs);
    hook.h_Entry = (HOOKFUNC)ScreenSettings_Hook;
    hook.h_Data = &context;
    result = ReplaceSettingsA("screenmode", &hook, tags);
    if (!store && result == SETTING_OK)
    {
        if (context.found) ScreenSettings_Decode(&context.prefs);
        else result = SETTING_ERROR_NO_FILE;
    }
    CloseLibrary(SettingsBase);
    return result;
}

/* Live preview is independent of both persistent settings layers. */
static BOOL ScreenSettings_GetLiveScale(IPTR *scale)
{
    struct Screen *screen = LockPubScreen("Workbench");
    BOOL result = FALSE;
    if (screen)
    {
        result = GetAttr(SA_Scale, (Object *)screen, scale);
        UnlockPubScreen(NULL, screen);
    }
    return result && *scale >= 100 && *scale <= 400 && !(*scale % 25);
}

static BOOL ScreenSettings_SetLiveScale(IPTR wanted, struct ScaleAttempt *attempt)
{
    struct Screen *screen = LockPubScreen("Workbench");
    ScaleAttemptApply(screen, wanted, attempt);
    if (screen) UnlockPubScreen(NULL, screen);
    return ScaleAttemptSucceeded(attempt);
}

static ULONG ScreenSettings_DirectoryLayer(CONST_STRPTR directory)
{
    if (!Stricmp(directory, "ENV:")) return SETTING_LAYER_APPLIED;
    if (!Stricmp(directory, "ENVARC:")) return SETTING_LAYER_USER;
    return (ULONG)~0;
}
#endif
