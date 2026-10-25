#ifndef PREFS_ASL_H
#define PREFS_ASL_H

/*
    Copyright © 2022, The AROS Development Team. All rights reserved.

    Desc: Asl prefs definitions
    Lang: english
*/

#ifndef EXEC_TYPES_H
#   include <exec/types.h>
#endif
#ifndef LIBRARIES_IFFPARSE_H
#   include <libraries/iffparse.h>
#endif

#define ID_ASL MAKE_ID('A','S','L',' ')

/* ap_SizePosition: where a requester opens when its program gives no
   position, and whether its first size is relative to the screen. */
#define ASLPREFS_POS_MASK         0x07
#define ASLPREFS_POS_DEFAULT      0    /* centred on the screen */
#define ASLPREFS_POS_CENTERWIN    1    /* centred on the parent window */
#define ASLPREFS_POS_CENTERSCR    2
#define ASLPREFS_POS_TOPLEFTWIN   3    /* parent window corner + offset */
#define ASLPREFS_POS_TOPLEFTSCR   4    /* screen corner + offset */
#define ASLPREFS_POS_POINTER      5    /* centred under the pointer */
#define ASLPREFS_SIZE_RELATIVE    0x10 /* ap_RelativeWidth/Height percent */
#define ASLPREFS_OVERRIDE         0x40

struct AslPrefs
{
    LONG    ap_Reserved[4];
    UBYTE   ap_SortBy;
    UBYTE   ap_SortDrawers;
    UBYTE   ap_SortOrder;
    UBYTE   ap_SizePosition;
    WORD    ap_RelativeLeft;
    WORD    ap_RelativeTop;
    UBYTE   ap_RelativeWidth;
    UBYTE   ap_RelativeHeight;
} __packed;

#endif /* PREFS_ASL_H */
