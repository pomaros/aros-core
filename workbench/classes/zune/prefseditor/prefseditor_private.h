#ifndef _PREFSEDITOR_PRIVATE_H_
#define _PREFSEDITOR_PRIVATE_H_

/*
    Copyright © 2004-2013, The AROS Development Team. All rights reserved.
    This file is part of the PrefsEditor class, which is distributed under
    the terms of version 2.1 of the GNU Lesser General Public License.
    
    $Id$
*/

#include <exec/types.h>
#include <dos/bptr.h>

/*** Instance data **********************************************************/
struct PrefsEditor_DATA
{
    BOOL   ped_Changed,
           ped_Testing,
           ped_CanUse,
           ped_CanSave,
           ped_CanTest,
           ped_Loaded;
    STRPTR ped_Name,
           ped_Path,
           ped_IconTool;
    BPTR   ped_BackupFH;
    TEXT   ped_BackupPath[15];
           /* Large enough for BACKUP_PREFIX, 8 digits and NULL byte */
    /* Filter state: sections hidden and labels marked, restored by an
     * empty filter. */
    Object *ped_Hidden[32];
    ULONG   ped_HiddenCount;
    Object *ped_Marked[64];
    Object *ped_MarkedGadget[64];
    BOOL    ped_PreviousMark[64];
    ULONG   ped_MarkedCount;
    Object *ped_Registers[8];   /* registers given tab counts */
    ULONG   ped_RegisterCount;
};

#define BACKUP_PREFIX "T:PT"


#endif /* _PREFSEDITOR_PRIVATE_H_ */
