#ifndef _MUI_CLASSES_ICONLIST_H
#define _MUI_CLASSES_ICONLIST_H

/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright  2002-2010, The AROS Development Team. All rights reserved.
    $Id$
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuitionbase.h>
#include <intuition/classusr.h>
#include <clib/alib_protos.h>
#include <utility/utility.h>
#include <dos/dosextens.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/muimaster.h>
#include <proto/intuition.h>

#include "iconlist_attributes.h"

/****************************************************************************/
/*** Name *******************************************************************/
#define MUIC_IconList "IconList.mui"

/*** Methods ****************************************************************/
#define MUIM_IconList_Clear             (MUIB_IconList | 0x00000000)
#define MUIM_IconList_Update            (MUIB_IconList | 0x00000001)
#define MUIM_IconList_RethinkDimensions (MUIB_IconList | 0x00000002)
#define MUIM_IconList_CreateEntry       (MUIB_IconList | 0x00000010) /* returns 0 For Failure or (struct IconEntry *) */
#define MUIM_IconList_UpdateEntry       (MUIB_IconList | 0x00000011) /* returns 0 For Failure or (struct IconEntry *) */
#define MUIM_IconList_DestroyEntry      (MUIB_IconList | 0x00000012)
#define MUIM_IconList_PropagateEntryPos (MUIB_IconList | 0x00000013)
#define MUIM_IconList_DrawEntry         (MUIB_IconList | 0x00000020)
#define MUIM_IconList_DrawEntryLabel    (MUIB_IconList | 0x00000021)
#define MUIM_IconList_MakeEntryVisible  (MUIB_IconList | 0x00000024)
#define MUIM_IconList_SelectAll         (MUIB_IconList | 0x00000030)
#define MUIM_IconList_UnselectAll       (MUIB_IconList | 0x00000031)
#define MUIM_IconList_NextIcon          (MUIB_IconList | 0x00000034)
#define MUIM_IconList_Sort              (MUIB_IconList | 0x00000040)
#define MUIM_IconList_CoordsSort        (MUIB_IconList | 0x00000041)
#define MUIM_IconList_PositionIcons     (MUIB_IconList | 0x00000042)
#define MUIM_IconList_GetIconPrivate    (MUIB_IconList | 0x000000FF)

struct MUIP_IconList_Clear              {STACKED ULONG MethodID;};
struct MUIP_IconList_Update             {STACKED ULONG MethodID;};
struct MUIP_IconList_RethinkDimensions  {STACKED ULONG MethodID; STACKED struct IconEntry *singleicon;};
struct MUIP_IconList_CreateEntry        {STACKED ULONG MethodID; STACKED STRPTR filename; STACKED STRPTR label; STACKED struct FileInfoBlock *fib; STACKED struct DiskObject *entry_dob; STACKED ULONG type; STACKED APTR udata;};
struct MUIP_IconList_UpdateEntry        {STACKED ULONG MethodID; STACKED struct IconEntry *entry; STACKED STRPTR filename; STACKED STRPTR label; STACKED struct FileInfoBlock *fib; STACKED struct DiskObject *entry_dob; STACKED ULONG type;};
struct MUIP_IconList_DestroyEntry       {STACKED ULONG MethodID; STACKED struct IconEntry *entry;};
struct MUIP_IconList_PropagateEntryPos  {STACKED ULONG MethodID; STACKED struct IconEntry *entry;};
struct MUIP_IconList_DrawEntry          {STACKED ULONG MethodID; STACKED struct IconEntry *entry; STACKED IPTR drawmode;};
struct MUIP_IconList_DrawEntryLabel     {STACKED ULONG MethodID; STACKED struct IconEntry *entry; STACKED IPTR drawmode;};
struct MUIP_IconList_NextIcon           {STACKED ULONG MethodID; STACKED IPTR nextflag; STACKED struct IconList_Entry **entry;};
struct MUIP_IconList_Sort               {STACKED ULONG MethodID;};
struct MUIP_IconList_PositionIcons      {STACKED ULONG MethodID;};
struct MUIP_IconList_MakeEntryVisible    {STACKED ULONG MethodID; STACKED struct IconEntry *entry;};
struct MUIP_IconList_GetIconPrivate     {STACKED ULONG MethodID; STACKED struct IconList_Entry *entry;}; /* *entry must be a valid Icon */

/* used by MUIM_IconList_NextIcon */
/* Optional presenter method. The generic class does not link Wanderer code. */
#define MUIM_IconList_Tile (MUIB_IconList | 0x00000025)
#define ICONTILE_SIZE 1
#define ICONTILE_DRAW 2
#define ICONTILE_CLICK 3
#define ICONTILE_HIT 4
#define ICONTILE_VISIBILITY 5
#define ICONTILE_POINTER 6 /* width=inside, x/y=unclipped surface-local coordinates */
#define ICONTILE_DEFINITION 9 /* rp=owned caller buffer, width=capacity */
#define ICONTILE_ANCHOR 10 /* width=group anchor0..4 */
#define ICONTILE_PLACEMENT 8 /* rp carries Screen identity only; width=0 invalidates */
#define ICONTILE_GEOMETRY 7 /* screen-relative full tile bounds; width=0 invalidates */
#define MUIM_IconList_UpdateTileVisibility (MUIB_IconList | 0x00000026)
struct IconTileRequest {
    ULONG action, width, height;
    struct RastPort *rp;
    LONG x, y;
};
struct MUIP_IconList_Tile {
    STACKED ULONG MethodID;
    STACKED struct IconEntry *entry;
    STACKED struct IconTileRequest *request;
};

struct IconList_Entry
{
    struct IconEntry    *ile_IconEntry;
    char                *label;     /* The label which is displayed (often FilePart(filename)) */
    LONG                type;
    void                *udata;
};

#define ILE_TYPE_APPICON 10
#define ILE_TYPE_CUSTOM  (ILE_TYPE_APPICON + 1)

struct IconList_Click
{
    int         shift; /* TRUE for shift click */
    struct IconList_Entry *entry; /* might be NULL */
};

struct IconList_Drop_SourceEntry
{
    struct Node dropse_Node;
};

struct IconList_Drop_Event
{
    struct List drop_SourceList;       /* iconlist obj         */
    Object*     drop_TargetObj;        /* iconlist obj         */
    STRPTR      drop_TargetPath;       /* destination path     */
    Object*     drop_SourceObj;        /* source iconlist obj  */ 
    struct TileDropRequest *drop_Tile; /* owned bounded transaction request */
    ULONG       drop_Mode;             /* iconlist drop mode   */
};

struct IconEntry
{
    struct Node                 ie_IconNode;
    struct Node                 ie_SelectionNode;

    struct IconList_Entry       ie_IconListEntry;

    struct DiskObject           *ie_RenderDiskObj; /* private scaled raster generation */
    ULONG                        ie_RenderScale;
    struct DiskObject           *ie_DiskObj;                     /* The icons disk objects */
    struct FileInfoBlock        *ie_FileInfoBlock;

    LONG                        ie_IconX,                       /* Top Left Co-ords of Icons "AREA" */
                                ie_IconY,
                                ie_ProvidedIconX,               /* Point coordinates of last known "provided" position */
                                ie_ProvidedIconY;

    ULONG                       ie_IconWidth,                    /* Width/Height of Icon "Image" */
                                ie_IconHeight,
                                ie_AreaWidth,                    /* Width/Height of Icon "AREA" ..    */
                                ie_AreaHeight;                   /* if the icons Label Width is larger than
                                                                        ie_IconWidth, AreaWidth = the icons label Width
                                                                        else it will be the same as ie_IconWidth */

    /* Optional multi-cell reservation, excluding the outer spacing. */
    ULONG                       ie_GridWidth, ie_GridHeight;
    ULONG                       ie_Flags;

    UBYTE                       *ie_TxtBuf_DisplayedLabel;
    ULONG                       ie_SplitParts;
    ULONG                       ie_TxtBuf_DisplayedLabelWidth;
    UBYTE                       *ie_TxtBuf_DATE;
    ULONG                       ie_TxtBuf_DATEWidth;
    UBYTE                       *ie_TxtBuf_TIME;
    ULONG                       ie_TxtBuf_TIMEWidth;
    UBYTE                       *ie_TxtBuf_SIZE;
    ULONG                       ie_TxtBuf_SIZEWidth;
    UBYTE                       *ie_TxtBuf_PROT;


    APTR                        *ie_User1;                      /* Pointer to data provided by user */
};

/* Note:
 * ie_Icon[X|Y] vs ie_ProvidedIcon[X|Y]
 *
 * ie_Icon[X|Y] always contains the current position in rendering view
 * ie_ProvidedIcon[X|Y] has following fixed characteristics:
 *   a) is it set to do_Currect[X|Y] at IconEntry creation time
 *   b) if it is different then NO_ICON_POSITION, the auto layouting will not apply to the IconEntry
 */

/****************************************************************************/
/*** Name *******************************************************************/
#define MUIC_IconDrawerList         "IconDrawerList.mui"

/****************************************************************************/
/*** Name *******************************************************************/
#define MUIC_IconVolumeList         "IconVolumeList.mui"

struct VolumeIcon_Private
{
    ULONG                   vip_FLags; /* These flags will set volume attributes */
    struct NotifyRequest    vip_FSNotifyRequest;
    IPTR                    vip_Key1;
};

#define _volpriv(entry)  ((struct VolumeIcon_Private *)entry->ie_IconListEntry.udata)

extern const struct __MUIBuiltinClass _MUI_IconList_desc; /* PRIV */
extern const struct __MUIBuiltinClass _MUI_IconDrawerList_desc; /* PRIV */
extern const struct __MUIBuiltinClass _MUI_IconVolumeList_desc; /* PRIV */

#define IconListObject       MUIOBJMACRO_START(MUIC_IconList)
#define IconVolumeListObject MUIOBJMACRO_START(MUIC_IconVolumeList)
#define IconDrawerListObject MUIOBJMACRO_START(MUIC_IconDrawerList)

#endif /* _MUI_CLASSES_ICONLIST_H */
