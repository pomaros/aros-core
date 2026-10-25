/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    This file is part of AROS and is distributed under the AROS Public License 1.1.
*/
#include <proto/alib.h>
#include <proto/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/imageclass.h>
#include <string.h>
#include "asl_intern.h"
#include "layout.h"
#include "propunits.h"
#include "filereqhooks.h"
#include "fontreqhooks.h"
#include "modereqhooks.h"

#define SWAP(a,b) do { __typeof__(a) temporary = (a); (a) = (b); (b) = temporary; } while (0)

static Object *ScrollerList(struct LayoutData *live, Object *prop)
{
    switch (live->ld_IntReq->ir_ReqType)
    {
        case ASL_FileRequest:
        {
            struct FRUserData *data = live->ld_UserData;
            return data->ScrollGad.prop == prop ? data->Listview : NULL;
        }
        case ASL_FontRequest:
        {
            struct FOUserData *data = live->ld_UserData;
            if (data->NameScrollGad.prop == prop) return data->NameListview;
            return data->SizeScrollGad.prop == prop ? data->SizeListview : NULL;
        }
        case ASL_ScreenModeRequest:
        {
            struct SMUserData *data = live->ld_UserData;
            return data->ScrollGad.prop == prop ? data->Listview : NULL;
        }
    }
    return NULL;
}

/* Called ONLY by the exact private ASL Listview method through
 * DoGadgetMethodA. GadgetLock excludes InputTask's Locked_DoMethodA throughout
 * this no-allocation publication. Candidate objects are never attached.
 * StringInfo, buffers, undo, active/selected state, notification models and
 * public object identities remain live. Losing resources are disposed later,
 * outside this method and after the last InputTask reader has completed. */
BOOL AslPublishScale(struct LayoutData *live, struct LayoutData *candidate,
    struct GadgetInfo *ginfo, struct AslBase_intern *AslBase)
{
    struct AslObjectRecord *old, *next;
    /* Validate the complete object graph before changing its first field.
     * Hook/custom-font requests never enter this path. */
    if (!live || !candidate || (!live->ld_ScaleAware && live->ld_GadgetsAttached) ||
        candidate->ld_Window != live->ld_Window || !candidate->ld_GadgetContext)
        return FALSE;
    for (old = live->ld_Objects, next = candidate->ld_Objects; old && next;
         old = old->next, next = next->next)
    {
        Class *cl = old->objectClass;
        if (cl != next->objectClass || old->kind != next->kind) return FALSE;
        if (old->kind != ASLOBJ_GADGET) continue;
        if (OCLASS(old->object) != cl || OCLASS(next->object) != cl) return FALSE;
        if (((struct Gadget *)old->object)->GadgetID != ((struct Gadget *)next->object)->GadgetID)
            return FALSE;
        if (cl == AslBase->aslstringclass)
        {
            struct Gadget *g = (struct Gadget *)old->object;
            if (!(g->Flags & GFLG_STRINGEXTEND) || !g->SpecialInfo ||
                !((struct StringInfo *)g->SpecialInfo)->Extension) return FALSE;
        }
        else if (cl == AslBase->asllistviewclass)
        {
            struct AslListViewData *x = INST_DATA(cl, old->object), *y = INST_DATA(cl, next->object);
            if (y->lineheight <= 0 || x->total < 0 || (QUAD)x->total * y->lineheight > 2147483647)
                return FALSE;
        }
        else if (cl == AslBase->aslpropclass)
        {
            /* The native drag keeps a global pixel click offset. Defer the
             * whole adoption until release rather than changing its basis. */
            struct PropInfo *info = ((struct Gadget *)old->object)->SpecialInfo;
            if (info && (info->Flags & KNOBHIT)) return FALSE;
        }
        else if (cl == AslBase->aslcycleclass)
        {
            struct AslCycleData *a = INST_DATA(cl, old->object), *b = INST_DATA(cl, next->object);
            LONG i;
            if (a->popupwindow || a->numitems != b->numitems) return FALSE;
            for (i = 0; i < a->numitems; ++i)
                if (strcmp(a->itemmemory[i].string, b->itemmemory[i].string)) return FALSE;
        }
        else if (cl == AslBase->aslcolorpickerclass)
        {
            if (((struct AslColorPickerData *)INST_DATA(cl, old->object))->popupwindow) return FALSE;
        }
        else if (cl != AslBase->asllistviewclass && cl != AslBase->aslstringclass &&
                 cl != AslBase->aslbuttonclass && cl != AslBase->aslpropclass &&
                 cl != AslBase->aslarrowclass && cl != AslBase->aslfontpreviewclass &&
                 cl != AslBase->aslfontstyleclass && cl != AslBase->asleraserclass)
            return FALSE;
    }
    if (old || next) return FALSE;
    if (live->ld_Window2 && live->ld_IntReq->ir_ReqType == ASL_ScreenModeRequest)
    {
        struct SMUserData *data = live->ld_UserData;
        struct AslListViewData *list;
        if (!data->PropertyGadget || OCLASS(data->PropertyGadget) != AslBase->asllistviewclass)
            return FALSE;
        list = INST_DATA(AslBase->asllistviewclass, data->PropertyGadget);
        if (!list->frame || OCLASS(list->frame) != AslBase->aslframeclass ||
            list->total < 0 || (QUAD)list->total * (candidate->ld_Font->tf_YSize +
            2 * AslScaleValue(candidate, BORDERLVITEMSPACINGY)) > 2147483647) return FALSE;
    }

    for (old = live->ld_Objects, next = candidate->ld_Objects; old;
         old = old->next, next = next->next)
    {
        struct Gadget *a = (struct Gadget *)old->object, *b = (struct Gadget *)next->object;
        Class *cl = old->objectClass;
        if (old->kind == ASLOBJ_IMAGE)
        {
            /* The stable arrow gadget adopts the new image; its live owner
             * record adopts that same image. Candidate disposal owns the old. */
            SWAP(old->object, next->object);
            continue;
        }
        if (old->kind == ASLOBJ_MODEL) continue;
        a->LeftEdge = b->LeftEdge; a->TopEdge = b->TopEdge;
        a->Width = b->Width; a->Height = b->Height;
        a->Flags = (a->Flags & ~(GFLG_RELRIGHT|GFLG_RELBOTTOM|GFLG_RELWIDTH|GFLG_RELHEIGHT|GFLG_RELSPECIAL)) |
            (b->Flags & (GFLG_RELRIGHT|GFLG_RELBOTTOM|GFLG_RELWIDTH|GFLG_RELHEIGHT|GFLG_RELSPECIAL));
        if ((a->Flags & GFLG_EXTENDED) && (((struct ExtGadget *)a)->MoreFlags & GMORE_BOUNDS))
        {
            struct ExtGadget *ea = (struct ExtGadget *)a, *eb = (struct ExtGadget *)b;
            ea->BoundsLeftEdge = eb->BoundsLeftEdge; ea->BoundsTopEdge = eb->BoundsTopEdge;
            ea->BoundsWidth = eb->BoundsWidth; ea->BoundsHeight = eb->BoundsHeight;
        }
        a->UserData = candidate->ld_GadgetContext;
        if (cl == AslBase->aslbuttonclass)
        {
            struct AslButtonData *x = INST_DATA(cl,a), *y = INST_DATA(cl,b);
            SWAP(x->frame,y->frame); SWAP(x->coolimagepal,y->coolimagepal);
            SWAP(x->coolpixels,y->coolpixels); SWAP(x->coolwidth,y->coolwidth);
            SWAP(x->coolheight,y->coolheight); SWAP(x->ld,y->ld);
        }
        else if (cl == AslBase->aslstringclass)
        {
            struct AslStringData *x = INST_DATA(cl,a), *y = INST_DATA(cl,b);
            struct StringInfo *info = a->SpecialInfo;
            SWAP(x->frame,y->frame); SWAP(x->ld,y->ld);
            /* These exact ASL strings always use the built-in extended string
             * superclass. Change its borrowed font only: never copy either
             * instance or its live text/undo/work buffers from the candidate. */
            info->Extension->Font = candidate->ld_Font;
        }
        else if (cl == AslBase->aslpropclass)
        {
            struct AslPropData *x = INST_DATA(cl,a), *y = INST_DATA(cl,b);
            SWAP(x->frame,y->frame); SWAP(x->ld,y->ld);
            x->deltafactor = y->deltafactor;
        }
        else if (cl == AslBase->asllistviewclass)
        {
            struct AslListViewData *x = INST_DATA(cl,a), *y = INST_DATA(cl,b);
            LONG row = x->top;
            SWAP(x->frame,y->frame); SWAP(x->ld,y->ld); SWAP(x->font,y->font);
            x->itemheight = y->itemheight; x->spacing = y->spacing;
            x->default_renderhook.h_Data = candidate->ld_GadgetContext;
            x->lineheight = y->lineheight; x->totalpixels = x->total * x->lineheight;
            x->toppixel = row * x->lineheight; x->layouted = TRUE; x->scroll = 0;
            getgadgetcoords(a, ginfo, &x->minx, &x->miny, &x->width, &x->height);
            x->maxx = x->minx + x->width - 1; x->maxy = x->miny + x->height - 1;
            x->visiblepixels = x->height - AslScaleValue(x->ld, BORDERLVSPACINGY) * 2;
            if (x->visiblepixels < 0) x->visiblepixels = 0;
            if (x->toppixel + x->visiblepixels > x->totalpixels)
                x->toppixel = x->totalpixels - x->visiblepixels;
            if (x->toppixel < 0) x->toppixel = 0;
            x->top = x->toppixel / x->lineheight;
            x->visible = x->visiblepixels / x->lineheight;
        }
        else if (cl == AslBase->aslcycleclass)
        {
            struct AslCycleData *x = INST_DATA(cl,a), *y = INST_DATA(cl,b);
            LONG i;
            for (i = 0; i < x->numitems; ++i)
                y->itemmemory[i].string = x->itemmemory[i].string;
            SWAP(x->frame,y->frame); SWAP(x->ld,y->ld); SWAP(x->font,y->font);
            SWAP(x->itemmemory,y->itemmemory);
        }
        else if (cl == AslBase->aslcolorpickerclass)
        {
            struct AslColorPickerData *x = INST_DATA(cl,a), *y = INST_DATA(cl,b);
            SWAP(x->frame,y->frame); SWAP(x->ld,y->ld);
        }
        else if (cl == AslBase->aslfontpreviewclass)
        {
            struct AslFontPreviewData *x = INST_DATA(cl,a), *y = INST_DATA(cl,b);
            SWAP(x->frame,y->frame); SWAP(x->ld,y->ld); SWAP(x->font,y->font);
        }
        else if (cl == AslBase->aslfontstyleclass)
        {
            struct AslFontStyleData *x = INST_DATA(cl,a), *y = INST_DATA(cl,b);
            SWAP(x->frame,y->frame); SWAP(x->ld,y->ld);
        }
        else if (cl == AslBase->aslarrowclass)
        {
            SWAP(a->GadgetRender,b->GadgetRender);
        }
    }
    /* Synchronize native propgclass's normalized drag state before input can
     * resume. Exact AslProp -> PropGClass OM_SET with NULL GadgetInfo and only
     * these PGA tags updates scalars/body/pot: no render, notification, font,
     * allocation or external callback. Internal IC models keep their targets. */
    for (old = live->ld_Objects; old; old = old->next)
        if (old->kind == ASLOBJ_GADGET && old->objectClass == AslBase->aslpropclass)
        {
            Object *list = ScrollerList(live, old->object);
            if (list)
            {
                struct AslListViewData *view = INST_DATA(AslBase->asllistviewclass, list);
                struct AslPropUnits units = AslPropEncode(view->totalpixels, view->visiblepixels, view->toppixel);
                struct TagItem tags[] = {{PGA_Top, units.top},
                    {PGA_Total, units.total}, {PGA_Visible, units.visible}, {TAG_DONE,0}};
                struct opSet set = {OM_SET, tags, NULL};
                DoMethodA(old->object, (Msg)&set);
            }
        }
    /* The optional properties list is read-only, owns no external drawing
     * resources, and shares the requester's generation. Its vector frame and
     * borrowed font can follow that generation without replacing its gadget. */
    if (live->ld_Window2 && live->ld_IntReq->ir_ReqType == ASL_ScreenModeRequest)
    {
        struct SMUserData *data = live->ld_UserData;
        struct Gadget *g = (struct Gadget *)data->PropertyGadget;
        struct AslListViewData *list = INST_DATA(AslBase->asllistviewclass, g);
        struct AslFrameData *frame = INST_DATA(AslBase->aslframeclass, list->frame);
        g->UserData = candidate->ld_GadgetContext;
        list->ld = candidate->ld_GadgetContext; list->font = candidate->ld_Font;
        list->default_renderhook.h_Data = candidate->ld_GadgetContext;
        frame->ld = candidate->ld_GadgetContext;
        list->itemheight = candidate->ld_Font->tf_YSize;
        list->spacing = AslScaleValue(candidate, BORDERLVITEMSPACINGY) * 2;
        list->lineheight = list->itemheight + list->spacing;
        list->totalpixels = list->total * list->lineheight;
        g->LeftEdge = live->ld_Window2->BorderLeft + AslScaleValue(candidate, OUTERSPACINGX);
        g->TopEdge = live->ld_Window2->BorderTop + AslScaleValue(candidate, OUTERSPACINGY);
        g->Width = live->ld_Window2->Width - live->ld_Window2->BorderRight -
            AslScaleValue(candidate, OUTERSPACINGX) - g->LeftEdge;
        g->Height = live->ld_Window2->Height - live->ld_Window2->BorderBottom -
            AslScaleValue(candidate, OUTERSPACINGY) - g->TopEdge;
        list->minx = g->LeftEdge; list->miny = g->TopEdge;
        list->width = g->Width; list->height = g->Height;
        list->maxx = list->minx + list->width - 1; list->maxy = list->miny + list->height - 1;
        list->visiblepixels = list->height - AslScaleValue(candidate, BORDERLVSPACINGY) * 2;
        list->visible = list->visiblepixels / list->lineheight;
        list->layouted = TRUE;
    }
    return TRUE;
}
