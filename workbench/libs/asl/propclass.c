/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Copyright (C) 1995-2011, The AROS Development Team. All rights reserved.
*/

#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>

#include <exec/memory.h>
#include <intuition/screens.h>
#include <intuition/icclass.h>
#include <intuition/cghooks.h>
#include <intuition/imageclass.h>
#include <intuition/gadgetclass.h>
#include <graphics/gfx.h>
#include <cybergraphx/cybergraphics.h>

#include <string.h>

#include "asl_intern.h"
#include "layout.h"

#define SDEBUG 0
#define DEBUG 0

#include <aros/debug.h>

#define CLASS_ASLBASE ((struct AslBase_intern *)cl->cl_UserData)
#define HOOK_ASLBASE  ((struct AslBase_intern *)hook->h_Data)

#define AslBase CLASS_ASLBASE

/********************** ASL PROP CLASS **************************************************/

IPTR AslProp__OM_NEW(Class * cl, Object * o, struct opSet * msg)
{
    struct AslPropData *data;
    struct TagItem fitags[] =
    {
        {IA_FrameType, FRAME_BUTTON},
        {IA_EdgesOnly, TRUE        },
        {TAG_DONE, 0UL}
    };

    struct ExtGadget *eg = (struct ExtGadget *)DoSuperMethodA(cl, o, (Msg)msg);
    if (eg)
    {
        data = INST_DATA(cl, eg);
        data->ld = (struct LayoutData *)GetTagData(GA_UserData, 0, msg->ops_AttrList);

        eg->BoundsLeftEdge = eg->LeftEdge;
        eg->BoundsTopEdge  = eg->TopEdge;
        eg->BoundsWidth    = eg->Width;
        eg->BoundsHeight   = eg->Height;
        eg->MoreFlags |= GMORE_BOUNDS;

        eg->LeftEdge += AslScaleValue(data->ld, BORDERPROPSPACINGX);
        eg->TopEdge  += AslScaleValue(data->ld, BORDERPROPSPACINGY);
        eg->Width    -= AslScaleValue(data->ld, BORDERPROPSPACINGX) * 2;
        eg->Height   -= AslScaleValue(data->ld, BORDERPROPSPACINGY) * 2;

        data->deltafactor = 1;
        data->frame = AslFrame((struct LayoutData *)GetTagData(GA_UserData, 0, msg->ops_AttrList), fitags, AslBase);
        if (!data->frame)
        {
            CoerceMethod(cl, (Object *)eg, OM_DISPOSE);
            eg = NULL;
        }
    }

    return (IPTR)eg;
}

/***********************************************************************************/

IPTR AslProp__OM_DISPOSE(Class * cl, Object * o, Msg msg)
{
    struct AslPropData *data;
    IPTR retval;

    data = INST_DATA(cl, o);
    if (data->frame) DisposeObject(data->frame);

    retval = DoSuperMethodA(cl, o, msg);

    return retval;
}

/***********************************************************************************/

IPTR AslProp__OM_SET(Class * cl, Object * o, struct opSet * msg)
{
    struct AslPropData *data;
    struct TagItem *tstate = msg->ops_AttrList;
    struct TagItem *ti;
    IPTR retval;
    BOOL rerender = FALSE;

    data = INST_DATA(cl, o);

    while((ti = NextTagItem(&tstate)))
    {
        switch(ti->ti_Tag)
        {
            case ASLSC_ListView:
                data->listview = (Object *)ti->ti_Data;
                break;

            case ASLSC_DeltaFactor:
                data->deltafactor = (LONG)ti->ti_Data;
                break;

            case ASLSC_Inc:
            case ASLSC_Dec:
                if (((LONG)ti->ti_Data > 0) &&
                    (((struct opUpdate *)msg)->opu_Flags & OPUF_INTERIM))
                {
                    IPTR top, total, visible, newtop;

                    if (data->listview)
                    {
                        /* Arrow buttons move exact logical rows, independent
                         * of the native knob's bounded representation. */
                        struct TagItem tags[] = {{ASLLV_ScrollPixels,
                            ti->ti_Data == ID_ARROWDEC ? -data->deltafactor : data->deltafactor}, {TAG_DONE, 0}};
                        struct opSet set = {OM_SET, tags, msg->ops_GInfo};
                        DoMethodA(data->listview, (Msg)&set);
                        break;
                    }
                    GetAttr(PGA_Top,     o, &top);
                    GetAttr(PGA_Total,   o, &total);
                    GetAttr(PGA_Visible, o, &visible);

                    newtop = top;
                    if (ti->ti_Data == ID_ARROWDEC)
                    {
                        if (newtop < data->deltafactor)
                        {
                           newtop = 0;
                        }
                        else
                        {
                            newtop -= data->deltafactor;
                        }

                    }
                    else
                    {
                        if (top <= total - visible - data->deltafactor)
                        {
                            newtop += data->deltafactor;
                        }
                        else
                        {
                            newtop = total - visible;
                        }
                    }

                    if (newtop != top)
                    {
                        struct TagItem set_tags [] =
                        {
                            {PGA_Top, newtop    },
                            {TAG_DONE           }
                        };
                        struct opSet ops;

                        ops.MethodID      = OM_SET;
                        ops.ops_AttrList = set_tags;
                        ops.ops_GInfo     = msg->ops_GInfo;

                        DoMethodA(o, (Msg)&ops);

                        /* rerender = TRUE; */
                    }

                } /* if ((ti->ti_Data > 0) && (((struct opUpdate *)msg)->opu_Flags & OPUF_INTERIM)) */

        } /* switch(ti->ti_Tag) */

    } /* while((ti = NextTagItem(&tstate))) */

    retval = DoSuperMethodA(cl, o, (Msg) msg);

    if (rerender)
    {
        struct RastPort *rp;

        rp = ObtainGIRPort(msg->ops_GInfo);
        if (NULL != rp)
        {
            DoMethod(o, GM_RENDER, (IPTR) msg->ops_GInfo, (IPTR) rp, GREDRAW_UPDATE);
            ReleaseGIRPort(rp);
        }
    }

    return retval;
}

/***********************************************************************************/

/* Only verified native input completion carries fresh endpoint intent.
 * Ordinary PGA setters and notification echoes must retain the list's exact
 * pixel position even when it shares a quantized endpoint bin. */
static void SnapInputEndpoint(Class *cl, Object *o, struct GadgetInfo *gi)
{
    struct AslPropData *data = INST_DATA(cl, o);
    IPTR top, total, visible, pixels, viewport;
    LONG endpoint;
    if (!data->listview) return;
    GetAttr(PGA_Top, o, &top);
    GetAttr(PGA_Total, o, &total);
    GetAttr(PGA_Visible, o, &visible);
    if (top != 0 && (total <= visible || top != total - visible)) return;
    GetAttr(ASLLV_TotalPixels, data->listview, &pixels);
    GetAttr(ASLLV_VisiblePixels, data->listview, &viewport);
    endpoint = top == 0 || pixels <= viewport ? 0 : pixels - viewport;
    {
        struct TagItem tags[] = {{ASLLV_TopPixel, endpoint}, {TAG_DONE, 0}};
        struct opSet set = {OM_SET, tags, gi};
        DoMethodA(data->listview, (Msg)&set);
    }
}

IPTR AslProp__GM_GOACTIVE(Class *cl, Object *o, struct gpInput *msg)
{
    IPTR result = DoSuperMethodA(cl, o, (Msg)msg);
    if (result & GMR_VERIFY) SnapInputEndpoint(cl, o, msg->gpi_GInfo);
    return result;
}

IPTR AslProp__GM_HANDLEINPUT(Class *cl, Object *o, struct gpInput *msg)
{
    IPTR result = DoSuperMethodA(cl, o, (Msg)msg);
    if (result & GMR_VERIFY) SnapInputEndpoint(cl, o, msg->gpi_GInfo);
    return result;
}

/***********************************************************************************/

IPTR AslProp__GM_RENDER(Class *cl, struct Gadget *g, struct gpRender *msg)
{
    struct AslPropData *data;
    IPTR retval;

    data = INST_DATA(cl, g);

    if (msg->gpr_Redraw == GREDRAW_REDRAW)
    {
        struct TagItem im_tags[] =
        {
            {IA_Width   , 0     },
            {IA_Height  , 0     },
            {TAG_DONE           }
        };
        WORD x, y, w, h;

        getgadgetcoords(g, msg->gpr_GInfo, &x, &y, &w, &h);

        x -= AslScaleValue(data->ld, BORDERPROPSPACINGX);
        y -= AslScaleValue(data->ld, BORDERPROPSPACINGY);
        w += AslScaleValue(data->ld, BORDERPROPSPACINGX) * 2;
        h += AslScaleValue(data->ld, BORDERPROPSPACINGY) * 2;

        im_tags[0].ti_Data = w;
        im_tags[1].ti_Data = h;

        SetAttrsA(data->frame, im_tags);

        DrawImageState(msg->gpr_RPort,
                       (struct Image *)data->frame,
                       x,
                       y,
                       IDS_NORMAL,
                       ((struct LayoutData *)g->UserData)->ld_Dri);

    #if AVOID_FLICKER
        {
            struct IBox ibox, fbox;

            fbox.Left = x;
            fbox.Top = y;
            fbox.Width = w;
            fbox.Height = h;

            ibox.Left   = x + AslScaleValue(data->ld, BORDERPROPSPACINGX);
            ibox.Top    = y + AslScaleValue(data->ld, BORDERPROPSPACINGY);
            ibox.Width  = w - AslScaleValue(data->ld, BORDERPROPSPACINGX) * 2;
            ibox.Height = h - AslScaleValue(data->ld, BORDERPROPSPACINGY) * 2;

            PaintInnerFrame(msg->gpr_RPort,
                            ((struct LayoutData *)g->UserData)->ld_Dri,
                            data->frame,
                            &fbox,
                            &ibox,
                            ((struct LayoutData *)g->UserData)->ld_Dri->dri_Pens[BACKGROUNDPEN],
                            AslBase);

        }
    #endif

    } /* if (msg->gpr_Redraw == GREDRAW_REDRAW) */

    retval = DoSuperMethodA(cl, (Object *)g, (Msg)msg);

    return retval;
}

/***********************************************************************************/
