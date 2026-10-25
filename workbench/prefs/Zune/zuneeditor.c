/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    ZuneEditor.mcc: the global Zune preferences as a Settings page. The
    panels are the Zune program's own Settingsgroup classes, one tab each;
    the file is zune/global.prefs, read and written through Configdata's
    PREF/MUIC form on the editor's file handle, so Use, Save, Test and
    Revert work as for every editor. The Zune program stays for the
    per-application preferences.

    The Scrollbars panel is not on this page: selecting it in the Zune
    program froze the M1 (aros-apple-silicon TODO.md, "the reproducible
    freeze in Zune Global Prefs"), and search could reveal it here.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <string.h>
#include <libraries/iffparse.h>
#include <prefs/prefhdr.h>
#include <zune/prefseditor.h>
#include <zune/customclasses.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/iffparse.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <aros/symbolsets.h>
#include "zunestuff.h"
#include "systemp.h"
#include "windowp.h"
#include "groupsp.h"
#include "buttonsp.h"
#include "cyclesp.h"
#include "slidersp.h"
#include "listviewsp.h"
#include "stringsp.h"
#include "navigationp.h"
#include "specialp.h"
#include "framesp.h"
#include "zuneeditor.h"
#include LC_LIBDEFS_FILE

#define ID_PREF MAKE_ID('P','R','E','F')
#define ID_PRHD MAKE_ID('P','R','H','D')
#define ID_MUIC MAKE_ID('M','U','I','C')

static const struct
{
    const struct __MUIBuiltinClass *desc;
    ULONG title;
} panels[ZUNE_PANELS] =
{
    { &_MUIP_System_desc,     MSG_DSC_SYSTEM     },
    { &_MUIP_Windows_desc,    MSG_DSC_WINDOWS    },
    { &_MUIP_Groups_desc,     MSG_DSC_GROUPS     },
    { &_MUIP_Buttons_desc,    MSG_DSC_BUTTONS    },
    { &_MUIP_Cycles_desc,     MSG_DSC_CYCLES     },
    { &_MUIP_Sliders_desc,    MSG_DSC_SLIDERS    },
    { &_MUIP_Listviews_desc,  MSG_DSC_LISTVIEWS  },
    { &_MUIP_Strings_desc,    MSG_DSC_STRINGS    },
    { &_MUIP_Navigation_desc, MSG_DSC_NAVIGATION },
    { &_MUIP_Special_desc,    MSG_DSC_SPECIAL    },
    { &_MUIP_Frames_desc,     MSG_DSC_FRAMES     },
};

static struct MUI_CustomClass *panel_classes[ZUNE_PANELS];
static CONST_STRPTR titles[ZUNE_PANELS + 1];

static int ZuneEditor_Init(LIBBASETYPEPTR base)
{
    ULONG i;
    Locale_Initialize();
    for (i = 0; i < ZUNE_PANELS; i++)
    {
        titles[i] = _(panels[i].title);
        panel_classes[i] = MUI_CreateCustomClass(NULL, MUIC_Settingsgroup, NULL,
            panels[i].desc->datasize, panels[i].desc->dispatcher);
        if (!panel_classes[i])
            return FALSE;
    }
    return TRUE;
}

static int ZuneEditor_Expunge(LIBBASETYPEPTR base)
{
    ULONG i;
    for (i = 0; i < ZUNE_PANELS; i++)
        if (panel_classes[i])
            MUI_DeleteCustomClass(panel_classes[i]);
    Locale_Deinitialize();
    return TRUE;
}

ADD2INITLIB(ZuneEditor_Init, 0);
ADD2EXPUNGELIB(ZuneEditor_Expunge, 0);

/* A value gadget: not a group, not a label or spacer. */
static BOOL is_value(Object *object)
{
    struct List *children = NULL;
    CONST_STRPTR id = OCLASS(object)->cl_ID;
    if (GetAttr(MUIA_Group_ChildList, object, (IPTR *)&children) && children)
        return FALSE;
    return !(id && (strcmp((const char *)id, MUIC_Text) == 0 ||
                    strcmp((const char *)id, MUIC_Rectangle) == 0 ||
                    strcmp((const char *)id, MUIC_Balance) == 0));
}

/* The attributes that hold a panel gadget's value, whatever its class;
   a notification on an attribute a class lacks never fires. */
static const ULONG value_attributes[] =
{
    MUIA_String_Contents, MUIA_Numeric_Value, MUIA_Cycle_Active, MUIA_Radio_Active,
    MUIA_Selected, MUIA_Imagedisplay_Spec, MUIA_Pendisplay_Spec, MUIA_Framedisplay_Spec,
};

/* Every value gadget of a panel gets an id from its panel and its place in
   the panel's fixed layout, 'Z', panel, then a two-byte ordinal, so the
   index and the port name the same gadget in every session; its edits
   raise the editor's Changed. */
static void name_gadgets(Object *editor, Object *object, ULONG panel, ULONG *ordinal)
{
    struct List *children = NULL;
    Object *child;
    APTR state;

    if (GetAttr(MUIA_Group_ChildList, object, (IPTR *)&children) && children)
    {
        state = children->lh_Head;
        while ((child = NextObject(&state)))
            name_gadgets(editor, child, panel, ordinal);
        return;
    }
    if (is_value(object))
    {
        ULONG i;
        if (XGET(object, MUIA_ObjectID) == 0)
        {
            SET(object, MUIA_ObjectID, MAKE_ID('Z', 'A' + panel, (*ordinal >> 8) & 0xff, *ordinal & 0xff));
            (*ordinal)++;
        }
        for (i = 0; i < sizeof(value_attributes) / sizeof(value_attributes[0]); i++)
            DoMethod(object, MUIM_Notify, value_attributes[i], MUIV_EveryTime,
                (IPTR)editor, 3, MUIM_Set, MUIA_PrefsEditor_Changed, TRUE);
    }
}

IPTR ZuneEditor__OM_NEW(Class *CLASS, Object *self, struct opSet *message)
{
    struct ZuneEditor_DATA *data;
    Object *groups[ZUNE_PANELS], *tabs;
    ULONG i;

    for (i = 0; i < ZUNE_PANELS; i++)
        groups[i] = NULL;
    tabs = RegisterObject,
        MUIA_Register_Titles, (IPTR)titles,
        MUIA_Register_Columns, 6,
        MUIA_CycleChain, TRUE,
    End;
    if (!tabs)
        return 0;
    for (i = 0; i < ZUNE_PANELS; i++)
    {
        groups[i] = NewObject(panel_classes[i]->mcc_Class, NULL, TAG_DONE);
        if (!groups[i])
        {
            MUI_DisposeObject(tabs);
            return 0;
        }
        DoMethod(tabs, OM_ADDMEMBER, (IPTR)groups[i]);
    }

    self = (Object *)DoSuperNewTags(CLASS, self, NULL,
        MUIA_PrefsEditor_Name, (IPTR)"Zune",
        MUIA_PrefsEditor_Path, (IPTR)"Zune/global.prefs",
        Child, (IPTR)tabs,
        TAG_MORE, (IPTR)message->ops_AttrList);
    if (!self)
        return 0;

    data = INST_DATA(CLASS, self);
    for (i = 0; i < ZUNE_PANELS; i++)
    {
        ULONG ordinal = 0;
        data->groups[i] = groups[i];
        name_gadgets(self, groups[i], i, &ordinal);
    }
    return (IPTR)self;
}

/* The panels' values, read from a Configdata object. The panels keep
   pointers into its strings, as the Zune program's LastSavedConfigdata
   shows, so it lives until the next import or the editor's end. */
static void config_to_panels(struct ZuneEditor_DATA *data, Object *configdata)
{
    ULONG i;
    for (i = 0; i < ZUNE_PANELS; i++)
        DoMethod(data->groups[i], MUIM_Settingsgroup_ConfigToGadgets, (IPTR)configdata);
    if (data->configdata)
        MUI_DisposeObject(data->configdata);
    data->configdata = configdata;
}

IPTR ZuneEditor__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    struct ZuneEditor_DATA *data = INST_DATA(CLASS, self);
    Object *configdata = data->configdata;
    IPTR result = DoSuperMethodA(CLASS, self, message);
    if (configdata)
        MUI_DisposeObject(configdata);
    return result;
}

static void panels_to_config(struct ZuneEditor_DATA *data, Object *configdata)
{
    ULONG i;
    for (i = 0; i < ZUNE_PANELS; i++)
        DoMethod(data->groups[i], MUIM_Settingsgroup_GadgetsToConfig, (IPTR)configdata);
}

IPTR ZuneEditor__MUIM_PrefsEditor_ImportFH(Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_ImportFH *message)
{
    struct ZuneEditor_DATA *data = INST_DATA(CLASS, self);
    struct IFFHandle *iff = AllocIFF();
    Object *configdata = MUI_NewObject(MUIC_Configdata, TAG_DONE);
    BOOL read = FALSE;

    if (iff && configdata)
    {
        iff->iff_Stream = (IPTR)message->fh;
        InitIFFasDOS(iff);
        if (!OpenIFF(iff, IFFF_READ))
        {
            StopChunk(iff, ID_PREF, ID_MUIC);
            DoMethod(configdata, MUIM_Dataspace_Clear);
            while (!ParseIFF(iff, IFFPARSE_SCAN))
            {
                struct ContextNode *cn = CurrentChunk(iff);
                if (cn && cn->cn_ID == ID_MUIC)
                {
                    DoMethod(configdata, MUIM_Dataspace_ReadIFF, (IPTR)iff);
                    read = TRUE;
                }
            }
            CloseIFF(iff);
        }
    }
    if (read)
    {
        config_to_panels(data, configdata);
        SET(self, MUIA_PrefsEditor_Changed, FALSE);   /* the import itself */
    }
    else if (configdata)
        MUI_DisposeObject(configdata);
    if (iff)
        FreeIFF(iff);
    return read;
}

IPTR ZuneEditor__MUIM_PrefsEditor_ExportFH(Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_ExportFH *message)
{
    struct ZuneEditor_DATA *data = INST_DATA(CLASS, self);
    struct IFFHandle *iff = AllocIFF();
    Object *configdata = MUI_NewObject(MUIC_Configdata, TAG_DONE);
    struct PrefHeader header;
    BOOL written = FALSE;

    if (iff && configdata)
    {
        panels_to_config(data, configdata);
        iff->iff_Stream = (IPTR)message->fh;
        InitIFFasDOS(iff);
        if (!OpenIFF(iff, IFFF_WRITE))
        {
            memset(&header, 0, sizeof(header));
            if (!PushChunk(iff, ID_PREF, ID_FORM, IFFSIZE_UNKNOWN) &&
                !PushChunk(iff, 0, ID_PRHD, IFFSIZE_UNKNOWN) &&
                WriteChunkBytes(iff, &header, sizeof(header)) == sizeof(header) &&
                !PopChunk(iff) &&
                DoMethod(configdata, MUIM_Dataspace_WriteIFF, (IPTR)iff, 0, ID_MUIC) == 0 &&
                !PopChunk(iff))
                written = TRUE;
            CloseIFF(iff);
        }
    }
    if (configdata)
        MUI_DisposeObject(configdata);
    if (iff)
        FreeIFF(iff);
    return written;
}

/* Zune's defaults are those of an empty store. */
IPTR ZuneEditor__MUIM_PrefsEditor_SetDefaults(Class *CLASS, Object *self, Msg message)
{
    struct ZuneEditor_DATA *data = INST_DATA(CLASS, self);
    Object *configdata = MUI_NewObject(MUIC_Configdata, TAG_DONE);
    if (!configdata)
        return FALSE;
    DoMethod(configdata, MUIM_Dataspace_Clear);
    config_to_panels(data, configdata);
    return TRUE;
}
