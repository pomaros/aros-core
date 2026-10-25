/*
    Copyright (C) 2002-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright (C) 2001-2003, The MorphOS Development Team. All Rights Reserved.
*/

#include <aros/debug.h>
#include <aros/macros.h>
#include <exec/memory.h>
#include <graphics/rastport.h>
#include <intuition/pointerclass.h>
#include <intuition/imageclass.h>
#include <prefs/pointer.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <diskfont/diskfont.h>
#include <proto/diskfont.h>
#include <proto/intuition.h>

#include "intuition_intern.h"
#include "windowframe.h"
#ifndef SKINS
#include "intuition_customize.h"
#endif
#include "inputhandler.h"
#include "inputhandler_support.h"
#include "monitorclass_private.h"

void SetActiveMonPointerPos(struct IntuitionBase *IntuitionBase)
{
    struct IntIntuitionBase *_intuitionBase = GetPrivIBase(IntuitionBase);
    Object *mon = _intuitionBase->ActiveMonitor;

    if (mon)
    {
        struct msSetPointerPos ppmsg;
        ppmsg.MethodID = MM_SetPointerPos;
        ppmsg.x = IntuitionBase->MouseX;
        ppmsg.y = IntuitionBase->MouseY;
        DoMethodA(mon, &ppmsg);
    }
}

BOOL ResetPointer(struct IntuitionBase *IntuitionBase)
{
    ULONG lock = LockIBase(0);
    struct IntIntuitionBase *_intuitionBase = GetPrivIBase(IntuitionBase);
    struct msSetPointerShape pmsg;
    Object *mon;
    Object *obj = _intuitionBase->DefaultPointer;
    BOOL res = TRUE;

    pmsg.pointer = NULL;

    if (obj)
        GetAttr(POINTERA_SharedPointer, obj, (IPTR *)&pmsg.pointer);
    D(bug("[ResetPointer] Default pointer is 0x%p\n", pmsg.pointer));
    if (!pmsg.pointer)
    {
        UnlockIBase(lock);
        return TRUE;
    }

    ObtainSemaphoreShared(&_intuitionBase->MonitorListSem);

    ForeachNode(&_intuitionBase->MonitorList, mon) {
        struct Screen *screen = FindFirstScreen(mon, IntuitionBase);
        pmsg.MethodID = MM_SetPointerShape;
        pmsg.screen = screen;
        if (screen && GetPrivScreen(screen)->Pointer) pmsg.pointer = GetPrivScreen(screen)->Pointer;
        else GetAttr(POINTERA_SharedPointer, obj, (IPTR *)&pmsg.pointer);
        if (!DoMethodA(mon, &pmsg)) res = FALSE;
    }

    ReleaseSemaphore(&_intuitionBase->MonitorListSem);

    D(bug("[ResetPointer] Returning %d\n", res));
    UnlockIBase(lock);
    return res;
}

void ActivateMonitor(Object *newmonitor, WORD x, WORD y, struct IntuitionBase *IntuitionBase)
{
    struct IntIntuitionBase *_intuitionBase = GetPrivIBase(IntuitionBase);
    Object *oldmonitor = _intuitionBase->ActiveMonitor;

    D(bug("ActivateMonitor(0x%p), old monitor 0x%p\n", newmonitor, oldmonitor));
    /* Do not bother if switching to the same monitor */
    if (newmonitor == oldmonitor)
        return;

    if (oldmonitor)
        SetAttrs(oldmonitor, MA_PointerVisible, FALSE, TAG_DONE);

    _intuitionBase->ActiveMonitor = newmonitor;
    if (newmonitor) {
        struct Screen *scr = FindFirstScreen(newmonitor, IntuitionBase);
        UWORD DWidth, DHeight;

        if (x == -1)
            x = IntuitionBase->MouseX;
        if (y == -1)
            y = IntuitionBase->MouseY;

        /* A crude copy from inputhandler.c. We should really handle this in monitorclass */
        if (scr)
        {
            DWidth = (scr->ViewPort.ColorMap->cm_vpe->DisplayClip.MaxX -
                      scr->ViewPort.ColorMap->cm_vpe->DisplayClip.MinX + 1) *
                     int_MouseScaleX(scr) - 1;
            DHeight = (scr->ViewPort.ColorMap->cm_vpe->DisplayClip.MaxY -
                       scr->ViewPort.ColorMap->cm_vpe->DisplayClip.MinY + 1) *
                      int_MouseScaleY(scr) - 1;
        }
        else
        {
            /* If there's no active screen, we take 160x160 as a limit */
            DWidth = 159;
            DHeight = 159;
        }
        if (x > DWidth)
            x = DWidth;
        if (y > DHeight)
            y = DHeight;

        D(bug("[ActivateMonitor] Mouse pointer coordinates: (%d, %d)\n", x, y));
        IntuitionBase->MouseX = x;
        IntuitionBase->MouseY = y;

        SetAttrs(newmonitor, MA_PointerVisible, TRUE, TAG_DONE);
        SetActiveMonPointerPos(IntuitionBase);
        notify_mousemove_screensandwindows(IntuitionBase);
    }
    D(bug("[ActivateMonitor] Done\n"));
}

struct Screen *FindFirstScreen(Object *monitor, struct IntuitionBase *IntuitionBase)
{
    struct Screen *scr;

    for (scr = IntuitionBase->FirstScreen; scr; scr = scr->NextScreen) {
        if (GetPrivScreen(scr)->IMonitorNode == monitor)
            break;
    }
    return scr;
}

struct RastPort *MyCreateRastPort(struct IntuitionBase *IntuitionBase)
{
    struct IntIntuitionBase *_intuitionBase = GetPrivIBase(IntuitionBase);
    struct GfxBase *GfxBase = _intuitionBase->GfxBase;
    struct RastPort *newrp = AllocMem(sizeof(*newrp), MEMF_PUBLIC);
    
    if (newrp)
    {
        InitRastPort(newrp);
    }

    return newrp;
}

struct RastPort *MyCloneRastPort(struct IntuitionBase *IntuitionBase, struct RastPort *rp)
{
    struct RastPort *newrp = NULL;

    if (rp)
    {
        newrp = AllocMem(sizeof(*newrp), MEMF_PUBLIC);
        if (newrp)
        {
            CopyMem(rp, newrp, sizeof(struct RastPort));
        }
    }

    return newrp;
}

void MyFreeRastPort(struct IntuitionBase *IntuitionBase, struct RastPort *rp)
{
    if (rp->RP_Extra)
    {
        /* Just in case... What if someone plays with ClipRects? */
        FreeVec(rp->RP_Extra);
    }

    FreeMem(rp, sizeof(*rp));
}

#ifdef __MORPHOS__

BOOL IsLayerHiddenBySibling(struct Layer *layer, BOOL xx)
{
    struct Window *win = layer->Window;
    
    /* skip requesters attached to the same window. */
    while (layer->front && layer->front->Window == win)
    {
        layer = layer->front;
    }

    /* jDc: we need to care for layers that are on
    ** front of our layer, but don't cover it*/

    if (layer->front)
    {
        struct Layer *lay;
        
        for (lay = layer->front; lay; lay = lay->front)
        {
            struct Window *lwin = lay->Window;
            
            if (lwin && win)
            {
                if (lwin->LeftEdge > win->LeftEdge + win->Width - 1) continue;
                if (lwin->LeftEdge + lwin->Width - 1 < win->LeftEdge) continue;
                if (lwin->TopEdge > win->TopEdge + win->Height - 1) continue;
                if (lwin->TopEdge + lwin->Height - 1 < win->TopEdge) continue;
                return TRUE;
            }
        }
        return NULL;
        
    } else return NULL;
}

#endif

/*
 * Round a screen scale to the allowed grid: 100 to 400 percent in steps of
 * 25. Zero (no tag) is 100.
 */
ULONG ClampScreenScale(ULONG scale)
{
    if (scale < SA_SCALE_MIN)
        return SA_SCALE_MIN;
    if (scale > SA_SCALE_MAX)
        return SA_SCALE_MAX;
    return scale - (scale % SA_SCALE_STEP);
}

/*
 * The same font at its size times the scale. Memory fonts first
 * (graphics), then the disk, through diskfont.library when it is there.
 * Bitmap faces without a designed size are scaled by diskfont.library.
 * NULL on failure; the caller keeps the unscaled font.
 */
/* FSF_TAGGED describes a TTextAttr, not a plain TextAttr. The source
 * font is held open by both callers for the complete synchronous request,
 * so its extension tags remain borrowed and valid until the open returns. */
void ScreenFontTextAttr(struct TextFont *font, struct TTextAttr *attr)
{
    attr->tta_Name = font->tf_Message.mn_Node.ln_Name;
    attr->tta_YSize = font->tf_YSize;
    attr->tta_Style = font->tf_Style & ~FSF_TAGGED;
    attr->tta_Flags = font->tf_Flags;
    attr->tta_Tags = NULL;
    if ((font->tf_Style & FSF_TAGGED) && font->tf_Extension)
    {
        attr->tta_Tags = ((struct TextFontExtension *)font->tf_Extension)->tfe_Tags;
        if (attr->tta_Tags) attr->tta_Style |= FSF_TAGGED;
    }
}

/* Borrowed logical metadata generations remain valid until screen disposal,
 * including their font, TextAttr tags and font-sized images. A plain scale
 * change reuses the current generation; only a new base font allocates one. */
struct LogicalScreenInfo
{
    struct LogicalScreenInfo *previous;
    struct IntDrawInfo drawinfo;
    struct IntuitionCustomize customize;
    struct TTextAttr fontattr;
};

/* All these views are owned by the screen, never by a gadget or a requester.
 * Keep past base-font generations alive for clients borrowing public metadata.
 * This also keeps inherited menu images in existing legacy windows valid. */
void DisposeLogicalScreenInfo(struct LogicalScreenInfo *info,
                              struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    if (!info) return;
    DisposeObject((Object *)info->drawinfo.dri_CheckMark);
    DisposeObject((Object *)info->drawinfo.dri_AmigaKey);
    DisposeObject((Object *)info->customize.submenu);
    if (info->drawinfo.dri_Font) CloseFont(info->drawinfo.dri_Font);
    FreeVec(info);
}

struct LogicalScreenInfo *PrepareLogicalScreenInfo(struct IntScreen *screen,
    struct TextFont *basefont, struct IntuitionBase *IntuitionBase)
{
    struct LogicalScreenInfo *info;
    struct TagItem tags[] = {{SYSIA_Which, MENUCHECK}, {SYSIA_DrawInfo, 0}, {TAG_DONE, 0}};
    if (screen->LogicalInfo && screen->LogicalInfo->drawinfo.dri_Font == basefont)
        return screen->LogicalInfo;
    info = AllocVec(sizeof(*info), MEMF_PUBLIC | MEMF_CLEAR);
    if (!info) return NULL;
    info->drawinfo = screen->DInfo;
    info->drawinfo.dri_Font = NULL;
    info->drawinfo.dri_CheckMark = info->drawinfo.dri_AmigaKey = NULL;
    /* Customize contains only submenu: allocate our own logical image below,
     * never borrow physical decorator resources across later reinitialization. */
    info->drawinfo.dri_Customize = &info->customize;
    info->drawinfo.dri_Font = SafeReopenFont(IntuitionBase, &basefont);
    if (!info->drawinfo.dri_Font) goto failure;
    ScreenFontTextAttr(info->drawinfo.dri_Font, &info->fontattr);
    tags[1].ti_Data = (IPTR)&info->drawinfo;
    info->drawinfo.dri_CheckMark = (struct Image *)NewObjectA(NULL, SYSICLASS, tags);
    tags[0].ti_Data = AMIGAKEY;
    info->drawinfo.dri_AmigaKey = (struct Image *)NewObjectA(NULL, SYSICLASS, tags);
    tags[0].ti_Data = SUBMENUIMAGE;
    info->customize.submenu = (struct Image *)NewObjectA(NULL, SYSICLASS, tags);
    if (!info->drawinfo.dri_CheckMark || !info->drawinfo.dri_AmigaKey || !info->customize.submenu)
        goto failure;
    return info;
failure:
    DisposeLogicalScreenInfo(info, IntuitionBase);
    return NULL;
}

void CommitLogicalScreenInfo(struct IntScreen *screen, struct LogicalScreenInfo *info)
{
    if (screen->LogicalInfo != info)
    {
        info->previous = screen->LogicalInfo;
        screen->LogicalInfo = info;
    }
    screen->Screen.Font = (struct TextAttr *)&info->fontattr;
}

void RestoreLogicalScreenInfo(struct IntScreen *screen, struct LogicalScreenInfo *info)
{
    screen->LogicalInfo = info;
    screen->Screen.Font = info ? (struct TextAttr *)&info->fontattr : NULL;
}

void FreeLogicalScreenInfo(struct IntScreen *screen, struct IntuitionBase *IntuitionBase)
{
    struct LogicalScreenInfo *info = screen->LogicalInfo;
    screen->LogicalInfo = NULL;
    while (info)
    {
        struct LogicalScreenInfo *previous = info->previous;
        DisposeLogicalScreenInfo(info, IntuitionBase);
        info = previous;
    }
}

struct DrawInfo *LogicalScreenDrawInfo(struct Screen *screen)
{
    return IS(screen)->LogicalInfo ? (struct DrawInfo *)&IS(screen)->LogicalInfo->drawinfo : NULL;
}

struct DrawInfo *WindowDrawInfo(struct Screen *screen, struct Window *window)
{
    /* Screen gadgets render on the physical bar. Window gadgets use their
     * known destination domain; retained legacy decoration metadata stays
     * coherent even when the screen's preferred base font changes later. */
    if (!window) return (struct DrawInfo *)&IS(screen)->DInfo;
    return (struct DrawInfo *)WindowFrameDrawInfo(window);
}

struct TextFont *OpenScaledScreenFont(struct IntuitionBase *IntuitionBase,
                                      struct TextFont *font, ULONG scale)
{
    struct IntIntuitionBase *_intuitionBase = GetPrivIBase(IntuitionBase);
    struct GfxBase *GfxBase = _intuitionBase->GfxBase;
    struct TTextAttr ta;
    struct TextFont *ret;
    struct Library *DiskfontBase;

    if (font == NULL || scale == SA_SCALE_MIN)
        return NULL;
    ScreenFontTextAttr(font, &ta);
    ta.tta_YSize = (UWORD)(((ULONG)font->tf_YSize * scale + 50) / 100);
    if (ta.tta_YSize == font->tf_YSize)
        return NULL;
    ret = OpenFont((struct TextAttr *)&ta);
    if (ret && ret->tf_YSize != ta.tta_YSize)
    {
        /* graphics gave the nearest memory size; the disk may have the exact one */
        CloseFont(ret);
        ret = NULL;
    }
    if (ret == NULL)
    {
        DiskfontBase = OpenLibrary("diskfont.library", 0);
        if (DiskfontBase)
        {
            ret = OpenDiskFont((struct TextAttr *)&ta);
            if (ret && ret->tf_YSize != ta.tta_YSize)
            {
                CloseFont(ret);
                ret = NULL;
            }
            if (ret == NULL)
            {
                struct DiskFontHeader *scaled = NewScaledDiskFont(font, (struct TextAttr *)&ta);
                if (scaled) ret = &scaled->dfh_TF;
            }
            CloseLibrary(DiskfontBase);
        }
    }
    return ret;
}

struct TextFont *SafeReopenFont(struct IntuitionBase *IntuitionBase,
                                struct TextFont **fontptr)
{
    struct IntIntuitionBase *_intuitionBase = GetPrivIBase(IntuitionBase);
    struct GfxBase *GfxBase = _intuitionBase->GfxBase;
    struct TextFont *ret = NULL, *font;

    /* Atomically lock the font before, so it can't go away
     */
    Forbid();

    font = *fontptr;
    if (font)
    {
        struct TTextAttr ta;

        font->tf_Accessors++;
        Permit();

        /* Now really open it
         */
        ScreenFontTextAttr(font, &ta);

        ret = OpenFont((struct TextAttr *)&ta);

        /* Unlock it
         */
        Forbid();
        font->tf_Accessors--;
    }

    Permit();

    return ret;
}

static Object *MakePointerWithRole(struct IntuitionBase *IntuitionBase,
    const UWORD *source, int xOffset, int yOffset, int width, int height, ULONG role)
{
    struct IntIntuitionBase *_intuitionBase = GetPrivIBase(IntuitionBase);
    struct TagItem pointertags[] = {
        {POINTERA_BitMap      , (IPTR)source},
        {POINTERA_BuiltinShape, role},
        {POINTERA_XOffset     , xOffset      },
        {POINTERA_YOffset     , yOffset      },
        {SPRITEA_OldDataFormat, TRUE         },
        {SPRITEA_Width        , width        },
        {SPRITEA_OutputHeight , height       },
        {TAG_DONE                            }
    };

    return NewObjectA(_intuitionBase->pointerclass, NULL, pointertags);
}

Object *MakePointerFromData(struct IntuitionBase *IntuitionBase,
    const UWORD *source, int xOffset, int yOffset, int width, int height)
{
    return MakePointerWithRole(IntuitionBase, source, xOffset, yOffset, width, height,
        POINTER_BUILTIN_NONE);
}
Object *MakeBuiltinPointerFromData(struct IntuitionBase *IntuitionBase,
    const UWORD *source, int xOffset, int yOffset, int width, int height, ULONG role)
{
    return MakePointerWithRole(IntuitionBase, source, xOffset, yOffset, width, height, role);
}

Object *MakePointerFromPrefs(struct IntuitionBase *IntuitionBase, struct Preferences *prefs)
{
    SetPointerColors(IntuitionBase);
    
    return MakePointerFromData(IntuitionBase, prefs->PointerMatrix, prefs->XOffset, prefs->YOffset, 16, 16);
}

void InstallPointer(struct IntuitionBase *IntuitionBase, UWORD which, Object **old, Object *pointer)
{
    struct msSetPointerShape pmsg;
    struct IntScreen    *scr;
    struct Window       *win;
    struct SharedPointer *oldpointer;
    Object *oldobject;

    DEBUG_POINTER(dprintf("InstallPointer(0x%p)\n", pointer);)

    pmsg.MethodID = MM_SetPointerShape;
    pmsg.pointer = NULL;

    ULONG lock = LockIBase(0);

    GetAttr(POINTERA_SharedPointer, *old, (IPTR *)&oldpointer);
    GetAttr(POINTERA_SharedPointer, pointer, (IPTR *)&pmsg.pointer);

    for (scr = GetPrivScreen(IntuitionBase->FirstScreen); scr; scr = GetPrivScreen(scr->Screen.NextScreen))
    {
        for (win = scr->Screen.FirstWindow; win; win = win->NextWindow)
        {
            if (((struct IntWindow *)win)->pointer == *old)
            {
                win->XOffset = pmsg.pointer->xoffset;
                win->YOffset = pmsg.pointer->yoffset;
            }
        }

        if (scr->Pointer == oldpointer)
        {
            pmsg.screen = &scr->Screen;
            DEBUG_POINTER(dprintf("InstallPointer: scr 0x%lx pointer 0x%lx sprite 0x%lx\n",
                                  scr, pointer, pmsg.pointer->sprite));
            if (DoMethodA(scr->IMonitorNode, &pmsg))
            {
                ObtainSharedPointer(pmsg.pointer, IntuitionBase);
                ReleaseSharedPointer(oldpointer, IntuitionBase);
                scr->Pointer = pmsg.pointer;
            }
            else
            {
                DEBUG_POINTER(dprintf("InstallPointer: can't change pointer.\n"));
            }
        }
    }

    oldobject = *old;
    *old = pointer;
    /* Set new normal pointer image on all empty displays */
    if (which == WBP_NORMAL)
        ResetPointer(IntuitionBase);
    /* Dispose old pointer only after setting new one */
    DisposeObject(oldobject);

    UnlockIBase(lock);
}

void SetPointerColors(struct IntuitionBase *IntuitionBase)
{
    struct IntIntuitionBase *_intuitionBase = GetPrivIBase(IntuitionBase);
    struct GfxBase *GfxBase = _intuitionBase->GfxBase;
    struct Color32 *p;
    int            k;
    ULONG          lock = LockIBase(0);
    /* Probably this should apply to Workbench screen and not to currently active one? */
    struct Screen *scr = IntuitionBase->ActiveScreen;

    DEBUG_POINTER(dprintf("SetPointerColors()\n");)

    p = _intuitionBase->Colors;

    if (scr)
    {
#ifndef ALWAYS_ALLOCATE_SPRITE_COLORS
        if (GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH) < 9)
#endif
        {
            UWORD firstcol = scr->ViewPort.ColorMap->SpriteBase_Even;
            
            /* Translate bank number and offset to color number - see graphics/getcolormap.c */
            firstcol = (firstcol << 4) | (firstcol >> 8);
            for (k = 1; k < 4; ++k, ++p) {
                DEBUG_POINTER(dprintf("Color %u: R %08lx G %08lx B %08lx\n", p[k+7].red, p[k+7].green, p[k+7].blue);)
                SetRGB32(&scr->ViewPort, k + firstcol, p[k+7].red, p[k+7].green, p[k+7].blue);
            }
        }
    }

    UnlockIBase(lock);

    DEBUG_POINTER(dprintf("SetPointerColors() done\n");)
}


struct SharedPointer *CreateSharedPointer(struct ExtSprite *sprite, int x, int y,
                    struct IntuitionBase *IntuitionBase)
{
    struct SharedPointer *pointer;

    pointer = AllocMem(sizeof(*pointer), MEMF_PUBLIC);
    if (pointer)
    {
        pointer->sprite = sprite;
        pointer->xoffset = x;
        pointer->yoffset = y;
        pointer->ref_count = 1;
        pointer->physicalPixels = FALSE;
        pointer->builtinShape = POINTER_BUILTIN_NONE;
    }

    return pointer;
}

void ObtainSharedPointer(struct SharedPointer *pointer,
                         struct IntuitionBase *IntuitionBase)
{
    ULONG lock = LockIBase(0);
    ++pointer->ref_count;
    UnlockIBase(lock);
}

void ReleaseSharedPointer(struct SharedPointer *pointer,
                          struct IntuitionBase *IntuitionBase)
{
    struct IntIntuitionBase *_intuitionBase = GetPrivIBase(IntuitionBase);
    struct GfxBase *GfxBase = _intuitionBase->GfxBase;
    ULONG lock = LockIBase(0);
    if (--pointer->ref_count == 0)
    {
        FreeSpriteData(pointer->sprite);
        FreeMem(pointer, sizeof(*pointer));
    }
    UnlockIBase(lock);
}

// TODO: Move to arch specific files.
#ifdef __mc68000__
/* See intuition_intern.h */
UWORD int_MouseScaleX(const struct Screen *screen)
{
    if (screen && screen->RastPort.BitMap &&
        !IS_HIDD_BM(screen->RastPort.BitMap) &&
        !(screen->ViewPort.Modes & (HIRES | SUPERHIRES)))
        return 2;
    return 1;
}

UWORD int_MouseScaleY(const struct Screen *screen)
{
    if (screen && screen->RastPort.BitMap &&
        !IS_HIDD_BM(screen->RastPort.BitMap) &&
        !(screen->ViewPort.Modes & LACE))
        return 2;
    return 1;
}
#endif
