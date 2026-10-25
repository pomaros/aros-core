/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    ZuneProbe: the Zune page in the guest. Every value gadget of its
    panels carries a distinct id; setting the Windows panel's sliders to
    7 raises Changed, Use writes ENV:Zune/global.prefs where
    settings.library reads zune/window/spacing-left as 7, and a new
    instance imports 7 into the same gadget.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <string.h>
#include <libraries/mui.h>
#include <libraries/settings.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/settings.h>

#define PROBE_TAG "ZUNEPROBE"
#include "probe.h"

struct Library *SettingsBase;

#define IDS_MAX 1024
static ULONG ids[IDS_MAX];
static ULONG id_count, duplicates;

static BOOL is_numeric(Object *object)
{
    IPTR max;
    return GetAttr(MUIA_Numeric_Max, object, &max) != 0;
}

/* Collects the page's 'Z' ids; with value set, sets every Windows panel
   ('Z','B') numeric gadget to it and returns the first one's id. */
static void walk(Object *object, LONG value, ULONG *first)
{
    struct List *children = NULL;
    Object *child;
    APTR state;
    ULONG id = XGET(object, MUIA_ObjectID), i;

    if ((id >> 24) == 'Z')
    {
        for (i = 0; i < id_count; i++)
            if (ids[i] == id)
                duplicates++;
        if (id_count < IDS_MAX)
            ids[id_count++] = id;
        if (value >= 0 && ((id >> 16) & 0xff) == 'B' && is_numeric(object))
        {
            SET(object, MUIA_Numeric_Value, value);
            if (!*first)
                *first = id;
        }
    }
    if (GetAttr(MUIA_Group_ChildList, object, (IPTR *)&children) && children)
    {
        state = children->lh_Head;
        while ((child = NextObject(&state)))
            walk(child, value, first);
    }
}

static Object *holder;
static ULONG described;

static IPTR count_record(struct Hook *hook, Object *editor, struct PrefsEditorRecord *record)
{
    described++;
    return 0;
}

static Object *open_editor(Object **app, Object **window)
{
    Object *editor;
    *app = ApplicationObject,
        MUIA_Application_Title, (IPTR)"ZuneProbe",
        SubWindow, (IPTR)(*window = WindowObject,
            MUIA_Window_Title, (IPTR)"ZuneProbe",
            WindowContents, (IPTR)(holder = VGroup,
                Child, (IPTR)(editor = MUI_NewObject("ZuneEditor.mcc", TAG_DONE)),
            End),
        End),
    End;
    if (!*app)
        return NULL;
    SET(*window, MUIA_Window_Open, TRUE);
    return editor;
}

int main(void)
{
    Object *app, *window, *editor, *gadget;
    ULONG first = 0;
    char value[SETTING_VALUE_SIZE];
    LONG result;
    BPTR lock;

    SettingsBase = OpenLibrary("settings.library", 1);
    CHECK(SettingsBase != NULL, "settings.library");
    editor = open_editor(&app, &window);
    CHECK(editor != NULL, "ZuneEditor.mcc instantiated");
    if (!editor || !SettingsBase)
        return PROBE_RESULT();

    walk(editor, -1, &first);
    CHECK(id_count > 50 && duplicates == 0, "%lu value gadgets with ids, %lu duplicates",
        (unsigned long)id_count, (unsigned long)duplicates);
    SET(editor, MUIA_PrefsEditor_Changed, FALSE);
    id_count = 0;
    walk(editor, 7, &first);
    CHECK(first != 0, "Windows panel sliders set to 7, first id 0x%08lx", (unsigned long)first);
    CHECK(XGET(editor, MUIA_PrefsEditor_Changed), "the edit raises Changed");
    CHECK(DoMethod(editor, MUIM_PrefsEditor_Use), "Use");
    CHECK(!XGET(editor, MUIA_PrefsEditor_Changed), "Use clears Changed");
    lock = Lock("ENV:Zune/global.prefs", SHARED_LOCK);
    if (lock)
        UnLock(lock);
    CHECK(lock != BNULL, "ENV:Zune/global.prefs written");
    value[0] = 0;
    result = GetSettingA("zune/window/spacing-left", value, sizeof(value), NULL);
    CHECK(result == SETTING_OK && strcmp(value, "7") == 0,
        "settings.library reads zune/window/spacing-left %s (%ld)", value, (long)result);
    say("step closing the first window");
    SET(window, MUIA_Window_Open, FALSE);
    say("step disposing the first instance");
    MUI_DisposeObject(app);

    say("step creating the second instance");
    editor = open_editor(&app, &window);
    CHECK(editor != NULL, "a second instance");
    if (editor)
    {
        gadget = find_id(editor, first);
        CHECK(gadget && XGET(gadget, MUIA_Numeric_Value) == 7, "it imported 7 into 0x%08lx",
            (unsigned long)first);
        CHECK(!XGET(editor, MUIA_PrefsEditor_Changed), "an import is not a change");

        /* Settings detaches a page and attaches it again: every panel's
           Setup and Cleanup must balance. Once per tab made active. */
        {
            Object *tabs = NULL;
            struct List *children = NULL;
            LONG tab;
            if (GetAttr(MUIA_Group_ChildList, editor, (IPTR *)&children) && children)
            {
                APTR state = children->lh_Head;
                tabs = NextObject(&state);
            }
            for (tab = 0; tabs && tab < 11; tab++)
            {
                SET(tabs, MUIA_Group_ActivePage, tab);
                say("step detach with tab %ld active", (long)tab);
                DoMethod(holder, MUIM_Group_InitChange);
                DoMethod(holder, OM_REMMEMBER, (IPTR)editor);
                DoMethod(holder, MUIM_Group_ExitChange);
                say("step attach with tab %ld active", (long)tab);
                DoMethod(holder, MUIM_Group_InitChange);
                DoMethod(holder, OM_ADDMEMBER, (IPTR)editor);
                DoMethod(holder, MUIM_Group_ExitChange);
            }
            CHECK(tabs != NULL, "detached and attached again with each of 11 tabs active");
        }

        /* What Settings search does to a page, each followed by a detach
           and an attach. */
        {
            struct PrefsEditorMatch match;
            static const char *steps[] = { "describe", "filter", "restore", "reveal", NULL };
            struct Hook hook;
            LONG k;
            memset(&hook, 0, sizeof(hook));
            hook.h_Entry = (HOOKFUNC)HookEntry;
            hook.h_SubEntry = (HOOKFUNC)count_record;
            memset(&match, 0, sizeof(match));
            match.id = first;
            for (k = 0; steps[k]; k++)
            {
                say("step %s", steps[k]);
                if (k == 0) DoMethod(editor, MUIM_PrefsEditor_Describe, (IPTR)&hook);
                if (k == 1) DoMethod(editor, MUIM_PrefsEditor_Filter, (IPTR)&match, 1);
                if (k == 2) DoMethod(editor, MUIM_PrefsEditor_Filter, (IPTR)NULL, 0);
                if (k == 3) DoMethod(editor, MUIM_PrefsEditor_Reveal, first);
                say("step %s detach", steps[k]);
                DoMethod(holder, MUIM_Group_InitChange);
                DoMethod(holder, OM_REMMEMBER, (IPTR)editor);
                DoMethod(holder, MUIM_Group_ExitChange);
                say("step %s attach", steps[k]);
                DoMethod(holder, MUIM_Group_InitChange);
                DoMethod(holder, OM_ADDMEMBER, (IPTR)editor);
                DoMethod(holder, MUIM_Group_ExitChange);
            }
            CHECK(described > 0, "described %lu records; search steps survived detach and attach",
                (unsigned long)described);
        }
        SET(window, MUIA_Window_Open, FALSE);
        MUI_DisposeObject(app);
    }
    CloseLibrary(SettingsBase);
    return PROBE_RESULT();
}
