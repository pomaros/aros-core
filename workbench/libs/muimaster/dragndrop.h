/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
/*
    Copyright (C) 2002-2011, The AROS Development Team.
    All rights reserved.
    
*/

#ifndef _DRAGNDROP_H
#define _DRAGNDROP_H

#ifndef _MUIMASTER_SUPPORT_H
#include "support.h"
#endif

/* Tags for GUI_CreateBitMapNodeA() */
#define GUI_BitMap      (TAG_USER+1)    /* struct BitMap * */
#define GUI_Mask        (TAG_USER+2)    /* APTR */
#define GUI_LeftOffset  (TAG_USER+3)    /* LONG */
#define GUI_TopOffset   (TAG_USER+4)    /* LONG */
#define GUI_Width       (TAG_USER+5)    /* LONG */
#define GUI_Height      (TAG_USER+6)    /* LONG */
#define GUI_IntegerScale (TAG_USER+8) /* ULONG1..4; owns a scaled ARGB snapshot. Masks are refused. */
#define GUI_SourceAlpha (TAG_USER+7)    /* BOOL */

struct DragNDrop *CreateDragNDropA( struct TagItem *tlist );
VOID DeleteDragNDrop( struct DragNDrop *dnd );

BOOL PrepareDragNDrop(struct DragNDrop *dnd,struct Screen *scr);
VOID FinishDragNDrop(struct DragNDrop *dnd);

BOOL DrawDragNDrop(struct DragNDrop *dnd, LONG x, LONG y);
VOID UndrawDragNDrop(struct DragNDrop *dnd);

struct BitMapNode *CreateBitMapNodeA( struct TagItem *tagList );
VOID DeleteBitMapNode(struct BitMapNode *bmn );

VOID AttachBitMapNode( struct DragNDrop *dnd, struct BitMapNode *bmn );
VOID DetachBitMapNode( struct BitMapNode *bmn );

#endif
