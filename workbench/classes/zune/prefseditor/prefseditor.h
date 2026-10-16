#ifndef ZUNE_PREFSEDITOR_H
#define ZUNE_PREFSEDITOR_H

/*
    Copyright © 2004-2013, The AROS Development Team. All rights reserved.
    This file is part of the PrefsEditor class, which is distributed under
    the terms of version 2.1 of the GNU Lesser General Public License.
    
    $Id$
*/

#include <dos/dos.h>
#include <libraries/mui.h>

/*** Name *******************************************************************/
#define MUIC_PrefsEditor           "PrefsEditor.mcc"

/*** Identifier base ********************************************************/
#define MUIB_PrefsEditor           (MUIB_AROS | 0x00000500)

/*** Public Methods *********************************************************/
#define MUIM_PrefsEditor_Test      (MUIB_PrefsEditor | 0x00000000)
#define MUIM_PrefsEditor_Revert    (MUIB_PrefsEditor | 0x00000001)
#define MUIM_PrefsEditor_Save      (MUIB_PrefsEditor | 0x00000002)
#define MUIM_PrefsEditor_Use       (MUIB_PrefsEditor | 0x00000003)
#define MUIM_PrefsEditor_Cancel    (MUIB_PrefsEditor | 0x00000004)
#define MUIM_PrefsEditor_Import    (MUIB_PrefsEditor | 0x00000005)
struct  MUIP_PrefsEditor_Import    {STACKED ULONG MethodID; STACKED CONST_STRPTR filename;};
#define MUIM_PrefsEditor_Export    (MUIB_PrefsEditor | 0x00000006)
struct  MUIP_PrefsEditor_Export    {STACKED ULONG MethodID; STACKED CONST_STRPTR filename;};

/*** Public (Abstract) Methods **********************************************/
#define MUIM_PrefsEditor_ImportFH    (MUIB_PrefsEditor | 0x00000007)
struct  MUIP_PrefsEditor_ImportFH    {STACKED ULONG MethodID; STACKED BPTR fh;};
#define MUIM_PrefsEditor_ExportFH    (MUIB_PrefsEditor | 0x00000008)
struct  MUIP_PrefsEditor_ExportFH    {STACKED ULONG MethodID; STACKED BPTR fh;};
#define MUIM_PrefsEditor_SetDefaults (MUIB_PrefsEditor | 0x00000009)

/*** Protected Methods ******************************************************/
/* Import from or export to the editor's path in a prefs directory ("ENV:",
 * "ENVARC:"). An editor that keeps its settings through settings.library
 * overrides both and maps the directory to a layer. */
#define MUIM_PrefsEditor_ExportToDirectory   (MUIB_PrefsEditor | 0x0000000a)
struct  MUIP_PrefsEditor_ExportToDirectory   {STACKED ULONG MethodID; STACKED CONST_STRPTR directory;};
#define MUIM_PrefsEditor_ImportFromDirectory (MUIB_PrefsEditor | 0x0000000b)
struct  MUIP_PrefsEditor_ImportFromDirectory {STACKED ULONG MethodID; STACKED CONST_STRPTR directory;};

/*** Search methods *********************************************************/
/* Default implementations walk the editor's gadget tree; an editor whose
 * layout the walk cannot see overrides them. */
#define MUIM_PrefsEditor_Describe            (MUIB_PrefsEditor | 0x0000000c)
struct  MUIP_PrefsEditor_Describe            {STACKED ULONG MethodID; STACKED struct Hook *hook;};
#define MUIM_PrefsEditor_Filter              (MUIB_PrefsEditor | 0x0000000d)
/* One match per gadget; highlights address its displayed label. The method
 * consumes this array synchronously; Text copies each highlight list. */
struct PrefsEditorMatch
{
    ULONG id;
    struct MUI_TextHighlight highlight;
};
struct  MUIP_PrefsEditor_Filter {STACKED ULONG MethodID; STACKED const struct PrefsEditorMatch *matches; STACKED ULONG count;};
#define MUIM_PrefsEditor_Reveal              (MUIB_PrefsEditor | 0x0000000e)
struct  MUIP_PrefsEditor_Reveal              {STACKED ULONG MethodID; STACKED ULONG id;};

/* One setting, as Describe hands it to the hook:
 * CallHookPkt(hook, editor, struct PrefsEditorRecord *). */
struct PrefsEditorRecord
{
    ULONG        per_ID;      /* MUIA_ObjectID, else PREFSEDITOR_PATH_ID | walk index */
    CONST_STRPTR per_Name;    /* its label, without shortcut marks or escapes */
    CONST_STRPTR per_Help;    /* MUIA_ShortHelp, or NULL */
    CONST_STRPTR per_Section; /* title of the enclosing framed group, or NULL */
    Object      *per_Object;
    CONST_STRPTR per_Choices; /* visible cycle/radio choices, space-separated */
    ULONG        per_Role;    /* PREFSEDITOR_ROLE_* */
};
#define PREFSEDITOR_ROLE_TEXT 6
#define PREFSEDITOR_ROLE_ACTION 7
#define PREFSEDITOR_PATH_ID     0x80000000
#define PREFSEDITOR_ROLE_STRING 1
#define PREFSEDITOR_ROLE_CHOICE 2   /* cycle, radio */
#define PREFSEDITOR_ROLE_NUMBER 3   /* slider, numeric */
#define PREFSEDITOR_ROLE_TOGGLE 4   /* checkmark */
#define PREFSEDITOR_ROLE_LIST   5

/*** Public Attributes ******************************************************/
#define MUIA_PrefsEditor_Name      (MUIB_PrefsEditor | 0x00000000) /* I-G  CONST_STRPTR */
#define MUIA_PrefsEditor_Changed   (MUIB_PrefsEditor | 0x00000001) /* -SG  BOOL */
#define MUIA_PrefsEditor_Testing   (MUIB_PrefsEditor | 0x00000002) /* -SG  BOOL */
#define MUIA_PrefsEditor_CanSave   (MUIB_PrefsEditor | 0x00000004) /* I-G  BOOL */
#define MUIA_PrefsEditor_CanTest   (MUIB_PrefsEditor | 0x00000005) /* I-G  BOOL */
#define MUIA_PrefsEditor_CanUse    (MUIB_PrefsEditor | 0x00000007) /* I-G  BOOL */

/*** Protected Attributes ***************************************************/
#define MUIA_PrefsEditor_Path      (MUIB_PrefsEditor | 0x00000003) /* I-G  CONST_STRPTR */
#define MUIA_PrefsEditor_IconTool  (MUIB_PrefsEditor | 0x00000006) /* I-G  CONST_STRPTR */

/*** Macros *****************************************************************/
#define PrefsEditorObject MUIOBJMACRO_START(MUIC_PrefsEditor)

#endif /* ZUNE_PREFSEDITOR_H */
