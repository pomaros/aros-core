/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Copyright (C) 2002-2024, The AROS Development Team. All rights reserved. */
#include <exec/memory.h>
#include <graphics/layers.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <clib/arossupport_protos.h>
#include "muimaster_intern.h"
#include "dragndrop.h"
#include "support.h"

/* Every card is an ordinary hidden-initially smart-refresh Window/Layer.
 * Layers owns backing/exposure and includes it in prepared screen transactions.
 * We never save or restore pixels from the screen raster. */
struct DragNDrop
{
    struct MinList dnd_List;
    struct Screen *dnd_Screen; /* Owner retains the screen while prepared. */
    const struct ScreenRenderContext *dnd_Context;
};

struct BitMapNode
{
    struct MinNode bmn_Node;
    struct BitMap *bmn_BitMap; /* External source, never mutated/freed. */
    APTR bmn_Mask;
    LONG bmn_Left, bmn_Top, bmn_Width, bmn_Height;
    struct DragNDrop *bmn_DnD;
    ULONG bmn_IntegerScale;
    APTR bmn_BitMapBuffer; /* Immutable physical ARGB source snapshot. */
    APTR bmn_CardPixels; /* Owned opaque ARGB, flattened onto theme background. */
    struct Window *bmn_Window;
    BOOL bmn_Shown;
};
#define bmn_Succ bmn_Node.mln_Succ
#define bmn_Pred bmn_Node.mln_Pred
#define DRAG_CARD_BORDER 3

#include "dragpixels.h"
#include "dragcard.h"

struct BitMapNode *CreateBitMapNodeA(struct TagItem *tagList)
{
    struct BitMapNode *bmn =
        (struct BitMapNode *)AllocMem(sizeof(struct BitMapNode),
        MEMF_CLEAR);
    if (bmn)
    {
        BOOL sourcealpha = FALSE;
        ULONG scale = 1;
        struct TagItem *tl = tagList;
        struct TagItem *tag;

        while ((tag = NextTagItem(&tl)))
        {
            ULONG id = tag->ti_Tag;
            IPTR data = tag->ti_Data;

            switch (id)
            {
            case GUI_BitMap:
                bmn->bmn_BitMap = (struct BitMap *)data;
                break;

            case GUI_Mask:
                bmn->bmn_Mask = (APTR) data;
                break;

            case GUI_LeftOffset:
                bmn->bmn_Left = data;
                break;

            case GUI_TopOffset:
                bmn->bmn_Top = data;
                break;

            case GUI_Width:
                bmn->bmn_Width = data;
                break;

            case GUI_Height:
                bmn->bmn_Height = data;
                break;

            case GUI_IntegerScale:
                scale = data;
                break;
            case GUI_SourceAlpha:
                sourcealpha = data;
                break;
            }
        }

        if (!bmn->bmn_BitMap)
        {
            FreeMem(bmn, sizeof(struct BitMapNode));
            bmn = NULL;
        }

        if (bmn)
        {
            bmn->bmn_IntegerScale = scale;
            if (!PrepareDragPixels(bmn, sourcealpha, scale))
            {
                FreeMem(bmn, sizeof(struct BitMapNode));
                bmn = NULL;
            }
        }
    }
    return bmn;
}

VOID DeleteBitMapNode(struct BitMapNode *bmn)
{
    if (!bmn) return;
    if (bmn->bmn_Window) CloseWindow(bmn->bmn_Window);
    FreeVec(bmn->bmn_CardPixels);
    FreeVec(bmn->bmn_BitMapBuffer);
    FreeMem(bmn, sizeof(*bmn));
}

struct BitMap *GetBitMap(struct BitMapNode *bmn)
{
    return bmn ? bmn->bmn_BitMap : NULL;
}

VOID AttachBitMapNode(struct DragNDrop *dnd, struct BitMapNode *bmn)
{
    /* Attaching/replacing resources while prepared would orphan a live card. */
    if (!dnd || !bmn || dnd->dnd_Screen || bmn->bmn_DnD) return;
    AddTail((struct List *)&dnd->dnd_List, (struct Node *)&bmn->bmn_Node);
    bmn->bmn_DnD = dnd;
}

VOID DetachBitMapNode(struct BitMapNode *bmn)
{
    if (!bmn || (bmn->bmn_DnD && bmn->bmn_DnD->dnd_Screen)) return;
    if (bmn->bmn_Succ && bmn->bmn_Pred)
    {
        Remove((struct Node *)&bmn->bmn_Node);
        bmn->bmn_Succ = bmn->bmn_Pred = NULL;
    }
    bmn->bmn_DnD = NULL;
}

struct DragNDrop *CreateDragNDropA(struct TagItem *tags)
{
    struct DragNDrop *dnd = AllocMem(sizeof(*dnd), MEMF_CLEAR);
    (void)tags;
    if (dnd) NewList((struct List *)&dnd->dnd_List);
    return dnd;
}

VOID FinishDragNDrop(struct DragNDrop *dnd)
{
    struct BitMapNode *node;
    if (!dnd) return;
    for (node = List_First(&dnd->dnd_List); node; node = Node_Next(node))
    {
        struct Window *window = node->bmn_Window;
        node->bmn_Window = NULL;
        node->bmn_Shown = FALSE;
        if (window) CloseWindow(window);
        FreeVec(node->bmn_CardPixels);
        node->bmn_CardPixels = NULL;
    }
    dnd->dnd_Screen = NULL;
    dnd->dnd_Context = NULL;
}

VOID DeleteDragNDrop(struct DragNDrop *dnd)
{
    struct BitMapNode *node;
    if (!dnd) return;
    FinishDragNDrop(dnd);
    while ((node = (struct BitMapNode *)RemTail((struct List *)&dnd->dnd_List)))
        DeleteBitMapNode(node);
    FreeMem(dnd, sizeof(*dnd));
}

BOOL PrepareDragNDrop(struct DragNDrop *dnd, struct Screen *screen)
{
    const struct ScreenRenderContext *context;
    struct DrawInfo *dri;
    struct BitMapNode *node;
    ULONG rgb[3];
    UBYTE background[3];
    if (!dnd || !screen || dnd->dnd_Screen || !List_First(&dnd->dnd_List)) return FALSE;
    context = zune_screen_context(screen);
    if (!context || !(dri = context->src_PhysicalDrawInfo) || dri->dri_Screen != screen) return FALSE;
    GetRGB32(screen->ViewPort.ColorMap, dri->dri_Pens[BACKGROUNDPEN], 1, rgb);
    background[0] = rgb[0] >> 24; background[1] = rgb[1] >> 24; background[2] = rgb[2] >> 24;
    dnd->dnd_Screen = screen;
    dnd->dnd_Context = context;
    for (node = List_First(&dnd->dnd_List); node; node = Node_Next(node))
    {
        LONG width = node->bmn_Width + DRAG_CARD_BORDER * 2;
        LONG height = node->bmn_Height + DRAG_CARD_BORDER * 2;
        struct RastPort *rp;
        if (node->bmn_Height > 32767 - DRAG_CARD_BORDER * 2 ||
            !PrepareDragCardPixels(node, background)) goto failed;
        node->bmn_Window = OpenWindowTags(NULL,
            WA_CustomScreen, (IPTR)screen, WA_Left, 0, WA_Top, 0,
            WA_Width, width, WA_Height, height,
            WA_Borderless, TRUE, WA_ToolBox, TRUE, WA_SmartRefresh, TRUE,
            WA_ScaleAware, TRUE, WA_VisualOverlay, TRUE,
            WA_Hidden, TRUE, WA_RenderContext, (IPTR)context,
            WA_Activate, FALSE, WA_IDCMP, 0, TAG_DONE);
        if (!node->bmn_Window) goto failed;
        rp = node->bmn_Window->RPort;
        SetAPen(rp, dri->dri_Pens[BACKGROUNDPEN]);
        RectFill(rp, 0, 0, width - 1, height - 1);
        SetAPen(rp, dri->dri_Pens[SHINEPEN]);
        RectFill(rp, 0, 0, width - 1, 0); RectFill(rp, 0, 0, 0, height - 1);
        SetAPen(rp, dri->dri_Pens[SHADOWPEN]);
        RectFill(rp, 0, height - 1, width - 1, height - 1);
        RectFill(rp, width - 1, 0, width - 1, height - 1);
        if (WritePixelArray(node->bmn_CardPixels, 0, 0, node->bmn_Width * 4,
            rp, DRAG_CARD_BORDER, DRAG_CARD_BORDER, node->bmn_Width,
            node->bmn_Height, RECTFMT_ARGB) != (ULONG)node->bmn_Width * (ULONG)node->bmn_Height) goto failed;
    }
    return TRUE;
failed:
    FinishDragNDrop(dnd);
    return FALSE;
}

/* Position in physical screen pixels. The native window is scale-aware; its
 * source bitmap has already been converted from the source canvas domain. */
BOOL DrawDragNDrop(struct DragNDrop *dnd, LONG x, LONG y)
{
    struct BitMapNode *node;
    if (!dnd || !dnd->dnd_Screen ||
        zune_screen_context(dnd->dnd_Screen) != dnd->dnd_Context) return FALSE;
    for (node = List_First(&dnd->dnd_List); node; node = Node_Next(node))
    {
        QUAD left = (QUAD)x - node->bmn_Left - DRAG_CARD_BORDER;
        QUAD top = (QUAD)y - node->bmn_Top - DRAG_CARD_BORDER;
        /* ChangeWindowBox takes signed WORD positions. Clamp before narrowing. */
        if (left < -32768) left = -32768; else if (left > 32767) left = 32767;
        if (top < -32768) top = -32768; else if (top > 32767) top = 32767;
        /* Validate the bound generation even for an already visible card.
         * A stale move cancels at the source; no pixels are redrawn here. */
        if (!ShowWindow(node->bmn_Window, NULL)) return FALSE;
        node->bmn_Shown = TRUE;
        ChangeWindowBox(node->bmn_Window, left, top,
            node->bmn_Width + DRAG_CARD_BORDER * 2,
            node->bmn_Height + DRAG_CARD_BORDER * 2);
    }
    return TRUE;
}

VOID UndrawDragNDrop(struct DragNDrop *dnd)
{
    struct BitMapNode *node;
    if (!dnd) return;
    for (node = List_First(&dnd->dnd_List); node; node = Node_Next(node))
    {
        if (node->bmn_Window && node->bmn_Shown) HideWindow(node->bmn_Window);
        node->bmn_Shown = FALSE;
    }
}
