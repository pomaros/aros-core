/*
    Copyright (C) 2026 John Knipper.
    Distributed under the AROS Public License, version 1.1.
*/
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <libraries/settings.h>
#include <prefs/screenmode.h>
#include <prefs/prefhdr.h>
#include <intuition/screens.h>
#include <intuition/intuition.h>
#include <utility/hooks.h>
#include <aros/asmcall.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/settings.h>
#include <proto/utility.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snapshot.h"
#include "../../include/scaleresult.h"

struct Library *SettingsBase;

struct Seed
{
    struct PrefHeader header;
    struct ScreenModePrefs prefs;
};

AROS_UFH3(IPTR, seed_settings,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(APTR, unused, A1))
{
    AROS_USERFUNC_INIT
    struct SettingReplaceMessage *msg = object;
    struct Seed *seed = hook->h_Data;
    ULONG i;
    BOOL header = FALSE, screen = FALSE;
    (void)unused;
    for (i = 0; i < msg->srm_Count; ++i)
    {
        if (msg->srm_Chunks[i].sc_ID == ID_PRHD) header = TRUE;
        if (msg->srm_Chunks[i].sc_ID == ID_SCRM) screen = TRUE;
    }
    if (msg->srm_Count + (!header) + (!screen) > msg->srm_Capacity) return FALSE;
    if (!header)
    {
        struct SettingChunk *chunk = &msg->srm_Chunks[msg->srm_Count++];
        chunk->sc_ID = ID_PRHD; chunk->sc_Size = sizeof(seed->header); chunk->sc_Data = &seed->header;
    }
    if (!screen)
    {
        struct SettingChunk *chunk = &msg->srm_Chunks[msg->srm_Count++];
        chunk->sc_ID = ID_SCRM; chunk->sc_Size = sizeof(seed->prefs); chunk->sc_Data = &seed->prefs;
    }
    return TRUE;
    AROS_USERFUNC_EXIT
}

static LONG save_scale(struct Screen *screen, LONG scale)
{
    struct TagItem tags[] = {{SETTING_Layer, SETTING_LAYER_USER}, {TAG_DONE, 0}};
    char value[16];
    LONG result;
    snprintf(value, sizeof(value), "%d", (int)scale);
    result = SetSettingA("screenmode/workbench/scale", value, tags);
    if (result == SETTING_ERROR_NO_FILE)
    {
        struct Seed seed = {0};
        struct Hook hook = {0};
        seed.header.ph_Version = PHV_CURRENT;
        seed.prefs.smp_Scale = AROS_LONG2BE(scale);
        seed.prefs.smp_DisplayID = AROS_LONG2BE(GetVPModeID(&screen->ViewPort));
        seed.prefs.smp_Width = AROS_WORD2BE(screen->Width);
        seed.prefs.smp_Height = AROS_WORD2BE(screen->Height);
        seed.prefs.smp_Depth = AROS_WORD2BE(GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH));
        seed.prefs.smp_Control = AROS_WORD2BE((screen->Flags & AUTOSCROLL) ? SMF_AUTOSCROLL : 0);
        hook.h_Entry = (HOOKFUNC)seed_settings;
        hook.h_Data = &seed;
        result = ReplaceSettingsA("screenmode", &hook, tags);
        if (result == SETTING_OK)
            result = SetSettingA("screenmode/workbench/scale", value, tags);
    }
    return result;
}

int main(void)
{
    IPTR args[3] = {0};
    struct RDArgs *rdargs;
    struct Screen *screen;
    IPTR old = 100, actual = 100;
    LONG wanted, error;
    struct ScaleAttempt attempt;
    char diagnostic[SCALE_DIAGNOSTIC_SIZE];
    int result = RETURN_FAIL;
    CONST_STRPTR name, action;
    BOOL workbench;
    rdargs = ReadArgs("ACTION,SCREEN/K,TEMP/S", args, NULL);
    if (!rdargs) { PrintFault(IoErr(), "DisplayScale"); return RETURN_ERROR; }
    name = args[1] ? (CONST_STRPTR)args[1] : (CONST_STRPTR)"Workbench";
    action = (CONST_STRPTR)args[0];
    workbench = Stricmp(name, "Workbench") == 0;
    screen = LockPubScreen((STRPTR)name);
    if (!screen) { Printf("DisplayScale: public screen '%s' is unavailable\n", name); goto out; }
    if (action && Stricmp(action, "inspect") == 0)
    {
        struct ScaleSnapshot *sample = AllocVec(sizeof(*sample), MEMF_PUBLIC);
        ULONG i;
        if (!sample || !CaptureScaleSnapshot(screen, sample))
        {
            PutStr("[SCALE-SNAPSHOT] result=FAIL reason=unavailable-or-capacity\n");
            if (sample) FreeVec(sample);
            goto unlock;
        }
        printf("[SCALE-SNAPSHOT] result=CHECK context=%p scale=%lu factor=%lu raster=%ld,%ld logical-font=%lu physical-font=%lu bar-height=%lu windows=%lu\n",
            (void *)sample->context, (unsigned long)sample->scale, (unsigned long)sample->factor,
            (long)sample->rasterWidth, (long)sample->rasterHeight,
            (unsigned long)sample->logicalFont, (unsigned long)sample->physicalFont,
            (unsigned long)sample->barHeight, (unsigned long)sample->count);
        for (i = 0; i < sample->count; ++i)
        {
            struct ScaleWindowSnapshot *row = &sample->windows[i];
            printf("[SCALE-SNAPSHOT-WINDOW] id=%p port=%p xy=%ld,%ld size=%ld,%ld inner=%ld,%ld limits=%ld,%ld,%ld,%ld borders=%u,%u,%u,%u canvas-height=%u\n",
                (void *)row->identity, (void *)row->port, (long)row->x, (long)row->y,
                (long)row->width, (long)row->height, (long)row->innerWidth, (long)row->innerHeight,
                (long)row->minWidth, (long)row->minHeight, (long)row->maxWidth, (long)row->maxHeight,
                row->border[0], row->border[1], row->border[2], row->border[3], row->canvasHeight);
        }
        FreeVec(sample);
        result = RETURN_OK;
        goto unlock;
    }
    if (!GetAttr(SA_Scale, (Object *)screen, &old))
    { PutStr("DisplayScale: screen scale is unavailable\n"); goto unlock; }
    wanted = old;
    if (!action || Stricmp(action, "query") == 0)
    {
        char stored[16];
        struct TagItem tags[] = {{SETTING_Layer, SETTING_LAYER_USER}, {TAG_DONE, 0}};
        Printf("DisplayScale: screen=%s scale=%ld%% raster=%ldx%ld logical=%ldx%ld\n",
            name, (LONG)old, (LONG)screen->Width, (LONG)screen->Height,
            (LONG)screen->Width * 100 / old, (LONG)screen->Height * 100 / old);
        if (workbench && (SettingsBase = OpenLibrary("settings.library", 1)))
            if (GetSettingA("screenmode/workbench/scale", stored, sizeof(stored), tags) == SETTING_OK)
                Printf("DisplayScale: saved-scale=%s%%\n", stored);
        result = RETURN_OK; goto unlock;
    }
    if (Stricmp(action, "larger") == 0 || strcmp(action, "+") == 0) wanted += 25;
    else if (Stricmp(action, "smaller") == 0 || strcmp(action, "-") == 0) wanted -= 25;
    else if (Stricmp(action, "reset") == 0) wanted = 100;
    else
    {
        char *end;
        long parsed = strtol((const char *)action, &end, 10);
        if (*end == '%') ++end;
        if (!*action || *end || parsed < 100 || parsed > 400)
        { PutStr("DisplayScale: use larger, smaller, reset, query, or 100..400 in steps of 25\n"); goto unlock; }
        wanted = (LONG)parsed;
    }
    if (wanted < 100 || wanted > 400 || wanted % 25)
    { PutStr("DisplayScale: range is 100..400%, in steps of 25\n"); goto unlock; }
    if (!workbench && !args[2])
    { PutStr("DisplayScale: named screens require TEMP; persistent settings apply to Workbench\n"); goto unlock; }
    if (!args[2])
    {
        SettingsBase = OpenLibrary("settings.library", 1);
        if (!SettingsBase) { PutStr("DisplayScale: settings.library is unavailable\n"); goto unlock; }
    }
    ScaleAttemptApply(screen, wanted, &attempt);
    if (!ScaleAttemptSucceeded(&attempt))
    {
        ScaleAttemptFormat(&attempt, diagnostic, sizeof(diagnostic));
        Printf("DisplayScale: %s\n", diagnostic);
        goto unlock;
    }
    actual = attempt.actual;
    if (!args[2] && (error = save_scale(screen, wanted)) != SETTING_OK)
    {
        Printf("DisplayScale: save failed (settings error %ld).\n", error);
        ScaleAttemptApply(screen, old, &attempt);
        if (ScaleAttemptSucceeded(&attempt))
            Printf("DisplayScale: live scale restored to %ld%%.\n", (LONG)old);
        else
        {
            ScaleAttemptFormat(&attempt, diagnostic, sizeof(diagnostic));
            Printf("DisplayScale: rollback not confirmed: %s\n", diagnostic);
        }
        goto unlock;
    }
    Printf("DisplayScale: screen=%s scale=%ld%% %s\n", name, (LONG)actual, args[2] ? "temporary" : "saved");
    result = RETURN_OK;
unlock:
    UnlockPubScreen(NULL, screen);
out:
    if (SettingsBase) CloseLibrary(SettingsBase);
    FreeArgs(rdargs);
    return result;
}
