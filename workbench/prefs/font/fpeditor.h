#ifndef _FPEDITOR_H_
#define _FPEDITOR_H_

/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
*/

#include <exec/types.h>
#include <libraries/mui.h>

/*** Name *******************************************************************/
/* The Font editor's PrefsEditor subclass, a shared class: the Font program
 * and the settings window both create it by name. */
#define MUIC_FontEditor "FontEditor.mcc"

/*** Macros *****************************************************************/
#define FontEditorObject MUIOBJMACRO_START(MUIC_FontEditor)

#endif /* _FPEDITOR_H_ */
