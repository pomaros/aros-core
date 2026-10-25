/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright (C) 2001-2003, The MorphOS Development Team. All Rights Reserved.
*/

#include <dos/dos.h>
#include <dos/dosextens.h>

#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/windecorclass.h>
#include <intuition/cghooks.h>
#include <intuition/icclass.h>
#include <intuition/extensions.h>

#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>

#include <utility/tagitem.h>
#include <utility/hooks.h>

#include <clib/macros.h>

#include <string.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#include <proto/alib.h>

#include "intuition_intern.h"
#include "gadgets.h"
#include "windowframe.h"
#include "windowsystemimage.h"

/**************************************************************************************************/

#ifdef __AROS__
#define USE_AROS_DEFSIZE 1
#else
#define USE_AROS_DEFSIZE 0
#endif

#define DEFSIZE_WIDTH  14
#define DEFSIZE_HEIGHT 14

#define HSPACING 3
#define VSPACING 3
/* Ralph Schmidt
 * heuristics for smaller arrows used in apps
 * like filer
 */
#define HSPACING_MIDDLE 2
#define VSPACING_MIDDLE 2
#define HSPACING_SMALL 1
#define VSPACING_SMALL 1

#define DRI(dri) ((struct DrawInfo *)(dri))

/**************************************************************************************************/

#if defined(__mc68000__)
/* One copy, shared with scrdecorclass.c, to save ROM space on m68k */
VOID getintframesizes(struct IntuitionBase *IntuitionBase, WORD *xsize, WORD *ysize)
#else
static inline VOID getintframesizes(struct IntuitionBase *IntuitionBase, WORD *xsize, WORD *ysize)
#endif
{
    switch (FRAME_SIZE(IntuitionBase))
    {
        case FRAMESIZE_THICK:
            *xsize = 2;
            *ysize = 2;
            break;
        case FRAMESIZE_MEDRES:
            *xsize = 2;
            *ysize = 1;
            break;
        default:    /* FRAMESIZE_THIN */
            *xsize = 1;
            *ysize = 1;
            break;
    }
}

static void renderimageframe(struct RastPort *rp, ULONG which, ULONG state, UWORD *pens,
                             WORD left, WORD top, WORD width, WORD height,
                             struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    WORD right = left + width - 1;
    WORD bottom = top + height - 1;
    BOOL leftedgegodown = FALSE;
    BOOL topedgegoright = FALSE;
    UWORD frameh;
    UWORD framev;

    getintframesizes(IntuitionBase, &frameh, &framev);

    switch(which)
    {
#if 0
    case CLOSEIMAGE:
            /* draw separator line at the right side */
            SetAPen(rp, pens[SHINEPEN]);
            RectFill(rp, right, top, right, bottom - 1);
            SetAPen(rp, pens[SHADOWPEN]);
            WritePixel(rp, right, bottom);

            right--;
            break;

    case ZOOMIMAGE:
    case DEPTHIMAGE:
    case SDEPTHIMAGE:
            /* draw separator line at the left side */
            SetAPen(rp, pens[SHINEPEN]);
            WritePixel(rp, left, top);
            SetAPen(rp, pens[SHADOWPEN]);
            RectFill(rp, left, top + 1, left, bottom);

            left++;
            break;
#endif

    case UPIMAGE:
    case DOWNIMAGE:
            leftedgegodown = TRUE;
            break;

    case LEFTIMAGE:
    case RIGHTIMAGE:
            topedgegoright = TRUE;
            break;
    }

    if (left == 0) leftedgegodown = TRUE;
    if (top == 0) topedgegoright = TRUE;

    SetAPen(rp, pens[((state == IDS_SELECTED) || (state == IDS_INACTIVESELECTED)) ? SHADOWPEN : SHINEPEN]);

    /* left edge */
    RectFill(rp, left,
             top,
             left + (frameh - 1),
             bottom - (leftedgegodown ? 0 : framev));

    /* top edge */
    RectFill(rp, left + 1,
             top,
             right - (topedgegoright ? 0 : frameh),
             top + (framev - 1));

    SetAPen(rp, pens[((state == IDS_SELECTED) || (state == IDS_INACTIVESELECTED)) ? SHINEPEN : SHADOWPEN]);

    /* right edge */
    RectFill(rp, right - (frameh - 1),
             top + (topedgegoright ? frameh : 0),
             right,
             bottom);

    /* bottom edge */
    RectFill(rp, left + (leftedgegodown ? framev : 0),
             bottom - (framev - 1),
             right - 1,
             bottom);
}

/**************************************************************************************************/

static UWORD getbgpen(ULONG state, UWORD *pens)
{
    UWORD bg;
    switch (state)
    {
    case IDS_NORMAL:
    case IDS_SELECTED:
            bg = pens[FILLPEN];
            break;

    default:
            bg = pens[BACKGROUNDPEN];
            break;
    }

    return bg;
}


/**************************************************************************************************/

IPTR WinDecorClass__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)cl->cl_UserData;
    struct Library *UtilityBase = GetPrivIBase(IntuitionBase)->UtilityBase;
    struct windecor_data *data;

    obj = (Object *)DoSuperMethodA(cl, obj, (Msg)msg);
    if (obj)
    {
        data = INST_DATA(cl, obj);
        data->userbuffersize = (ULONG) GetTagData(WDA_UserBuffer, 0, msg->ops_AttrList);
    }

    return (IPTR)obj;
}

/**************************************************************************************************/

IPTR WinDecorClass__OM_GET(Class *cl, Object *obj, struct opGet *msg)
{
    struct windecor_data *data = INST_DATA(cl, obj);

    switch(msg->opg_AttrID)
    {
        case WDA_PreparedScaleWindow:
            *msg->opg_Storage = OCLASS(obj) == cl;
            break;
        case WDA_UserBuffer:
        *msg->opg_Storage = (IPTR) data->userbuffersize;
        break;
    case WDA_TrueColorOnly:
        *msg->opg_Storage = FALSE;
        break;

    default:
        return DoSuperMethodA(cl, obj, (Msg)msg);
    }

    return 1;
}
    

/**************************************************************************************************/

IPTR WinDecorClass__WDM_GETDEFSIZE_SYSIMAGE(Class *cl, Object *obj, struct wdpGetDefSizeSysImage *msg)
{
    ULONG def_low_width = DEFSIZE_WIDTH, def_low_height = DEFSIZE_HEIGHT;
    ULONG def_med_width = DEFSIZE_WIDTH, def_med_height = DEFSIZE_HEIGHT;
    ULONG def_high_width = DEFSIZE_WIDTH, def_high_height = DEFSIZE_HEIGHT;

    ULONG refheight = msg->wdp_ReferenceFont->tf_YSize;
    ULONG refwidth = refheight;

    switch(msg->wdp_Which)
    {
    case LEFTIMAGE:
    case RIGHTIMAGE:
    #if USE_AROS_DEFSIZE
            def_low_width = def_med_width = def_high_width = DEFSIZE_WIDTH;
            def_low_height = def_med_height = def_high_height = DEFSIZE_HEIGHT;
    #else
            def_low_width = 16;
            def_med_width = 16;
            def_high_width = 23;
            def_low_height = 11;
            def_med_height = 10;
            def_high_height = 22;
    #endif
            break;

    case UPIMAGE:
    case DOWNIMAGE:
    #if USE_AROS_DEFSIZE
            def_low_width = def_med_width = def_high_width = DEFSIZE_WIDTH;
            def_low_height = def_med_height = def_high_height = DEFSIZE_HEIGHT;
    #else
            def_low_width = 13;
            def_med_width = 18;
            def_high_width = 23;
            def_low_height = 11;
            def_med_height = 11;
            def_high_height = 22;
    #endif
            break;

    case DEPTHIMAGE:
    case ZOOMIMAGE:
    case ICONIFYIMAGE:
    case LOCKIMAGE:
    case MUIIMAGE:
    case POPUPIMAGE:
    case SNAPSHOTIMAGE:
    case JUMPIMAGE:
    #if USE_AROS_DEFSIZE
            def_low_width = def_med_width = def_high_width = DEFSIZE_WIDTH;
            def_low_height = def_med_height = def_high_height = DEFSIZE_HEIGHT;
    #else
            def_low_width = 18;
            def_med_width = 24;
            def_high_width = 24;
    #endif
            break;

    case SDEPTHIMAGE:
    #if USE_AROS_DEFSIZE
            def_low_width = def_med_width = def_high_width = DEFSIZE_WIDTH;
            def_low_height = def_med_height = def_high_height = DEFSIZE_HEIGHT;
    #else
            def_low_width = 17;
            def_med_width = 23;
            def_high_width = 23;
    #endif
            break;

    case CLOSEIMAGE:
    #if USE_AROS_DEFSIZE
            def_low_width = def_med_width = def_high_width = DEFSIZE_WIDTH;
            def_low_height = def_med_height = def_high_height = DEFSIZE_HEIGHT;
    #else
            def_low_width = 15;
            def_med_width = 20;
            def_high_width = 20;
    #endif
            break;

    case SIZEIMAGE:
    #if USE_AROS_DEFSIZE
            def_low_width = def_med_width = def_high_width = DEFSIZE_WIDTH;
            def_low_height = def_med_height = def_high_height = DEFSIZE_HEIGHT;
    #else
            def_low_width = 13;
            def_med_width = 18;
            def_high_width = 18;
            def_low_height = 11;
            def_med_height = 10;
            def_high_height = 10;
    #endif
            break;

    case MENUCHECK:
            def_low_width  =
                def_med_width  =
                def_high_width = refwidth / 2 + 4; // reffont->tf_XSize * 3 / 2;
            def_low_height =
                def_med_height =
                def_high_height = refheight;
            break;

    case MXIMAGE:
            def_low_width  =
                def_med_width  =
                def_high_width = (refwidth + 1) * 2; // reffont->tf_XSize * 3 - 1;
            def_low_height =
                def_med_height =
                def_high_height = refheight + 1;
            break;

    case CHECKIMAGE:
            def_low_width  = (refwidth + 3) * 2;//reffont->tf_XSize * 2;
            def_low_height = refheight + 3;
            break;

    default:
        return FALSE;
    }

    switch(msg->wdp_SysiSize)
    {
    case SYSISIZE_LOWRES:
        *msg->wdp_Width = def_low_width;
        *msg->wdp_Height = def_low_height;
        break;

    case SYSISIZE_MEDRES:
        *msg->wdp_Width = def_med_width;
        *msg->wdp_Height = def_med_height;
        break;

    case SYSISIZE_HIRES:
        default:
        *msg->wdp_Width = def_high_width;
        *msg->wdp_Height = def_high_height;
        break;
    }

    return TRUE;
}

/**************************************************************************************************/

IPTR WinDecorClass__WDM_DRAW_SYSIMAGE(Class *cl, Object *obj, struct wdpDrawSysImage *msg)
{
    struct IntuitionBase        *IntuitionBase = (struct IntuitionBase *)cl->cl_UserData;
    struct GfxBase              *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct RastPort             *rp = msg->wdp_RPort;
    UWORD                       *pens = DRI(msg->wdp_Dri)->dri_Pens;
    LONG                        state = msg->wdp_State;
    LONG                        left = msg->wdp_X;
    LONG                        top = msg->wdp_Y;
    LONG                        width = msg->wdp_Width;
    LONG                        height = msg->wdp_Height;
    LONG                        right = left + width - 1;
    LONG                        bottom = top + height - 1;
    LONG                        h_spacing, v_spacing;
    UWORD                       frameh, framev;

    getintframesizes(IntuitionBase, &frameh, &framev);

    SetDrMd(rp, JAM1);

    switch(msg->wdp_Which)
    {
    case MUIIMAGE:
    {
        renderimageframe(rp, CLOSEIMAGE, state, pens, left, top, width, height, IntuitionBase);
        /* no code yet */
        break;
    }
    case POPUPIMAGE:
    {
        renderimageframe(rp, CLOSEIMAGE, state, pens, left, top, width, height, IntuitionBase);
        /* code should be added later */
        break;
    }
    case ICONIFYIMAGE:
    {
        UWORD  bg;
        WORD   h_spacing;
        WORD   v_spacing;

        renderimageframe(rp, ICONIFYIMAGE, state, pens,
                         left, top, width, height, IntuitionBase);
        left++;
        top++;
        width -= (frameh << 1);
        height -= (framev << 1);

        right = left + width - 1;
        bottom = top + height - 1 ;
        h_spacing = width / 6;
        v_spacing = height / 6;

        bg = getbgpen(state, pens);

        /* Clear background into correct color */
        SetAPen(rp, bg);
        RectFill(rp, left, top, right, bottom);

        left += h_spacing;
        right -= h_spacing;
        top += v_spacing;
        bottom -= v_spacing;

        SetAPen(rp, pens[SHADOWPEN]);
        RectFill(rp, left, top, right, bottom);

        SetAPen(rp, pens[(state == IDS_SELECTED) ? SHINEPEN :
                            (state == IDS_NORMAL) ? FILLPEN : BACKGROUNDPEN]);
        RectFill(rp, left + 1, top + 1, right - 1, bottom - 1);

        right = left + (right - left + 1) / 2;
        top = top + (bottom - top + 1) / 2;

        if (right - left <  4) right = left + 4;

        SetAPen(rp, pens[SHADOWPEN]);
        RectFill(rp, left, top, right, bottom);

        left += 2;
        right -= 2;
        top += 1;
        bottom -= 1;

        SetAPen(rp, pens[(state == IDS_SELECTED) ? FILLPEN :
                            (state == IDS_NORMAL) ? SHINEPEN : BACKGROUNDPEN]);
        RectFill(rp,left, top, right, bottom);
        break;
    }
    case CLOSEIMAGE:
    {
        renderimageframe(rp, CLOSEIMAGE, state, pens, left, top, width, height, IntuitionBase);
        left++;
        top++;
        width -= (frameh << 1);
        height -= (framev << 1);

        right = left + width - 1;
        bottom = top + height - 1;
        h_spacing = width * 4 / 10;
        v_spacing = height * 3 / 10;

        SetAPen(rp, getbgpen(state, pens));
        RectFill(rp, left, top, right, bottom);

        left += h_spacing;
        right -= h_spacing;
        top += v_spacing;
        bottom -= v_spacing;

        SetAPen(rp, pens[SHADOWPEN]);
        RectFill(rp, left, top, right, bottom);

        left++;
        top++;
        right--;
        bottom--;

        SetAPen(rp, pens[(state == IDS_NORMAL) ? SHINEPEN : BACKGROUNDPEN]);
        RectFill(rp, left, top, right, bottom);

        break;
    }

    case ZOOMIMAGE:
    {
        UWORD  bg;
        WORD   h_spacing;
        WORD   v_spacing;

        renderimageframe(rp, ZOOMIMAGE, state, pens,
                         left, top, width, height, IntuitionBase);
        left++;
        top++;
        width -= (frameh << 1);
        height -= (framev << 1);

        right = left + width - 1;
        bottom = top + height - 1 ;
        h_spacing = width / 6;
        v_spacing = height / 6;

        bg = getbgpen(state, pens);

        /* Clear background into correct color */
        SetAPen(rp, bg);
        RectFill(rp, left, top, right, bottom);

        left += h_spacing;
        right -= h_spacing;
        top += v_spacing;
        bottom -= v_spacing;

        SetAPen(rp, pens[SHADOWPEN]);
        RectFill(rp, left, top, right, bottom);

        SetAPen(rp, pens[(state == IDS_SELECTED) ? SHINEPEN :
                            (state == IDS_NORMAL) ? FILLPEN : BACKGROUNDPEN]);
        RectFill(rp, left + 1, top + 1, right - 1, bottom - 1);

        right = left + (right - left + 1) / 2;
        bottom = top + (bottom - top + 1) / 2;

        if (right - left <  4) right = left + 4;

        SetAPen(rp, pens[SHADOWPEN]);
        RectFill(rp, left, top, right, bottom);

        left += 2;
        right -= 2;
        top += 1;
        bottom -= 1;

        SetAPen(rp, pens[(state == IDS_SELECTED) ? FILLPEN :
                            (state == IDS_NORMAL) ? SHINEPEN : BACKGROUNDPEN]);
        RectFill(rp,left, top, right, bottom);
        break;
    }

    case DEPTHIMAGE:
    {
        UWORD  bg;
        WORD   h_spacing;
        WORD   v_spacing;

        renderimageframe(rp, DEPTHIMAGE, state, pens,
                         left, top, width, height, IntuitionBase);
        left++;
        top++;
        right--;
        bottom--;
        width -= (frameh << 1);
        height -= (framev << 1);

        h_spacing = width / 6;
        v_spacing = height / 6;

        bg = getbgpen(state, pens);

        /* Clear background into correct color */
        SetAPen(rp, bg);
        RectFill(rp, left, top, right, bottom);

        /* Draw a image of two partly overlapped tiny windows,
        */
        left += h_spacing;
        top  += v_spacing;

        width  -= h_spacing * 2;
        height -= v_spacing * 2;

        right  = left + width  - 1;
        bottom = top  + height - 1;

        /* Render top left window  */
        SetAPen(rp, pens[SHADOWPEN]);
        drawrect(rp
                 , left
                 , top
                 , right - (width / 3 )
                 , bottom - (height / 3)
                 , IntuitionBase);

        /* Fill top left window (inside of the frame above) */
        if ((state != IDS_INACTIVENORMAL))
        {
            SetAPen(rp, pens[BACKGROUNDPEN]);
            RectFill(rp, left + 1, top + 1,
                 right - (width / 3) - 1, bottom - (height / 3) - 1);
        }

        /* Render bottom right window  */
        SetAPen(rp, pens[SHADOWPEN]);
        drawrect(rp, left + (width / 3), top + (height / 3),
                 right, bottom, IntuitionBase);

        /* Fill bottom right window (inside of the frame above) */
        SetAPen(rp, pens[(state == IDS_INACTIVENORMAL) ? BACKGROUNDPEN : SHINEPEN]);
        RectFill(rp, left + (width / 3) + 1, top + (height / 3) + 1,
                 right - 1, bottom - 1);

        if (state == IDS_SELECTED)
        {
            /* Re-Render top left window  */

            SetAPen(rp, pens[SHADOWPEN]);
            drawrect(rp, left, top,
                 right - (width / 3 ), bottom - (height / 3), IntuitionBase);
        }
        break;
    }

    case SIZEIMAGE:
    {
        UWORD  bg;
        WORD   h_spacing;
        WORD   v_spacing;
        WORD   x, y;

        renderimageframe(rp, SIZEIMAGE, state, pens,
                         left, top, width, height, IntuitionBase);
        left++;
        top++;
        right--;
        bottom--;
        width -= (frameh << 1);
        height -= (framev << 1);

        h_spacing = width  / 5;
        v_spacing = height / 5;

        bg = getbgpen(state, pens);

        /* Clear background into correct color */
        SetAPen(rp, bg);
        RectFill(rp, left, top, right, bottom);

        /* A triangle image */
        left += h_spacing;
        top  += v_spacing;

        right  = left + width  - 1 - (h_spacing * 2);
        bottom = top  + height - 1 - (v_spacing * 2);

        width  = right  - left + 1;
        height = bottom - top  + 1;

        if (state != IDS_INACTIVENORMAL)
        {
            SetAPen(rp, pens[SHINEPEN]);

            for(y = top; y <= bottom; y++)
            {
                x = left + (bottom - y) * width / height;
                RectFill(rp, x, y, right, y);
            }
        }

        SetAPen(rp, pens[SHADOWPEN]);
        /* Draw triangle border */
        Move(rp, left, bottom);
        Draw(rp, right, top);
        Draw(rp, right, bottom);
        Draw(rp, left, bottom);

        break;
    }

    case LEFTIMAGE:
    {
        UWORD hspacing,vspacing;
        WORD  cy, i;

        hspacing = HSPACING;
        vspacing = VSPACING;

        if (width <= 12)
            hspacing = HSPACING_MIDDLE;

        if (width <= 10)
            hspacing = HSPACING_SMALL;

        if (height <= 12)
            vspacing = VSPACING_MIDDLE;

        if (height <= 10)
            vspacing = VSPACING_SMALL;

        renderimageframe(rp, LEFTIMAGE, state, pens,
                         left, top, width, height, IntuitionBase);
        left++;
        top++;
        right--;
        bottom--;
        width -= (frameh << 1);
        height -= (framev << 1);

        SetAPen(rp, getbgpen(state, pens));
        RectFill(rp, left, top, right, bottom);

        left += hspacing;
        top += vspacing;
        width -= hspacing * 2;
        height -= vspacing * 2;

        right = left + width - 1;
        bottom = top + height - 1;

        cy = (height + 1) / 2;

        SetAPen(rp, pens[SHADOWPEN]);

        for(i = 0; i < cy; i++)
        {
            RectFill(rp, left + (cy - i - 1) * width / cy,
                     top + i,
                     right - i * width / cy / 2,
                     top + i);
            RectFill(rp, left + (cy - i - 1) * width / cy,
                     bottom - i,
                     right - i * width / cy / 2,
                     bottom - i);
        }
        break;
    }

    case UPIMAGE:
    {
        UWORD hspacing,vspacing;
        WORD  cx, i;

        hspacing = HSPACING;
        vspacing = VSPACING;

        if (width <= 12)
            hspacing = HSPACING_MIDDLE;
    
        if (width <= 10)
            hspacing = HSPACING_SMALL;

        if (height <= 12)
            vspacing = VSPACING_MIDDLE;
    
        if (height <= 10)
            vspacing = VSPACING_SMALL;

        renderimageframe(rp, UPIMAGE, state, pens,
                         left, top, width, height, IntuitionBase);
        left++;
        top++;
        right--;
        bottom--;
        width -= (frameh << 1);
        height -= (framev << 1);

        SetAPen(rp, getbgpen(state, pens));
        RectFill(rp, left, top, right, bottom);

        left += hspacing;
        top += vspacing;
        width -= hspacing * 2;
        height -= vspacing * 2;

        right = left + width - 1;
        bottom = top + height - 1;

        cx = (width + 1) / 2;

        SetAPen(rp, pens[SHADOWPEN]);

        for(i = 0; i < cx; i++)
        {
            RectFill(rp, left + i,
                     top + (cx - i - 1) * height / cx,
                     left + i,
                     bottom - i * height / cx / 2);
            RectFill(rp, right - i,
                     top + (cx - i - 1) * height / cx,
                     right - i,
                     bottom - i * height / cx / 2);
        }

        break;
    }

    case RIGHTIMAGE:
    {
        UWORD hspacing,vspacing;
        WORD  cy, i;

        hspacing = HSPACING;
        vspacing = VSPACING;

        if (width <= 12)
            hspacing = HSPACING_MIDDLE;

        if (width <= 10)
            hspacing = HSPACING_SMALL;

        if (height <= 12)
            vspacing = VSPACING_MIDDLE;

        if (height <= 10)
            vspacing = VSPACING_SMALL;

        renderimageframe(rp, RIGHTIMAGE, state, pens,
                         left, top, width, height, IntuitionBase);
        left++;
        top++;
        right--;
        bottom--;
        width -= (frameh << 1);
        height -= (framev << 1);

        SetAPen(rp, getbgpen(state, pens));
        RectFill(rp, left, top, right, bottom);

        left += hspacing;
        top += vspacing;
        width -= hspacing * 2;
        height -= vspacing * 2;

        right = left + width - 1;
        bottom = top + height - 1;

        cy = (height + 1) / 2;

        SetAPen(rp, pens[SHADOWPEN]);

        for(i = 0; i < cy; i++)
        {
            RectFill(rp, left + i * width / cy / 2,
                     top + i,
                     right - (cy - i - 1) * width / cy,
                     top + i);
            RectFill(rp, left + i * width / cy / 2,
                     bottom - i,
                     right - (cy - i - 1) * width / cy,
                     bottom - i);
        }
        break;
    }

    case DOWNIMAGE:
    {
        UWORD hspacing,vspacing;
        WORD  cx, i;

        hspacing = HSPACING;
        vspacing = VSPACING;

        if (width <= 12)
            hspacing = HSPACING_MIDDLE;

        if (width <= 10)
            hspacing = HSPACING_SMALL;

        if (height <= 12)
            vspacing = VSPACING_MIDDLE;

        if (height <= 10)
            vspacing = VSPACING_SMALL;

        renderimageframe(rp, DOWNIMAGE, state, pens,
                         left, top, width, height, IntuitionBase);
        left++;
        top++;
        right--;
        bottom--;
        width -= (frameh << 1);
        height -= (framev << 1);

        SetAPen(rp, getbgpen(state, pens));
        RectFill(rp, left, top, right, bottom);

        left += hspacing;
        top += vspacing;
        width -= hspacing * 2;
        height -= vspacing * 2;

        right = left + width - 1;
        bottom = top + height - 1;

        cx = (width + 1) / 2;

        SetAPen(rp, pens[SHADOWPEN]);

        for(i = 0; i < cx; i++)
        {
            RectFill(rp, left + i,
                     top + i * height / cx / 2,
                     left + i,
                     bottom - (cx - i - 1) * height / cx);
            RectFill(rp, right - i,
                     top + i * height / cx / 2,
                     right -  i,
                     bottom - (cx - i - 1) * height / cx);
        }
        break;
    }

    default:
        return FALSE;
    }

    return TRUE;
}

/**************************************************************************************************/

static void findtitlearea(struct Window *win, LONG *left, LONG *right)
{
    struct Gadget *g;
    LONG gap = (WindowFrameScale(win) + 50) / 100;

    *left = 0;
    *right = win->Width - 1;

    for (g = win->FirstGadget; g; g = g->NextGadget)
    {
        if (g->Activation & GACT_TOPBORDER && g != (struct Gadget *)IW(win)->sysgads[DRAGBAR])
        {
            if (!(g->Flags & GFLG_RELRIGHT))
            {
                if (g->LeftEdge + g->Width > *left)
                    *left = g->LeftEdge + g->Width;
            }
            else
            {
                if (g->LeftEdge + win->Width - 1 - gap < *right)
                    *right = g->LeftEdge + win->Width - 1 - gap;
            }
        }
    }
}

/**************************************************************************************************/

IPTR INTERNAL_WDM_DRAW_WINTITLE(Class *cl, Object *obj, struct wdpDrawWinBorder *msg)
{
    struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)cl->cl_UserData;
    struct GfxBase       *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct RastPort      *rp = msg->wdp_RPort;
    struct Window        *window = msg->wdp_Window;
    UWORD                *pens = DRI(msg->wdp_Dri)->dri_Pens;
    LONG                  right, left;
    LONG unit = (WindowFrameScale(window) + 50) / 100;

    findtitlearea(window, &left, &right);

    SetDrMd(rp, JAM1);
    SetAPen(rp, pens[(window->Flags & WFLG_WINDOWACTIVE) ? FILLPEN : BACKGROUNDPEN]);
    CheckRectFill(rp, left + unit, unit, right - unit, window->BorderTop - unit * 2, IntuitionBase);

    if (right - left > (window->BorderLeft + window->BorderRight))
    {
        ULONG                   textlen, titlelen;
        struct TextFont         *tf;
        struct TextExtent       te;
        UWORD                   xunused, ypad;

        getintframesizes(IntuitionBase, &xunused, &ypad);
        if (IW(window)->ScaleAware) ypad = 1;
        ypad = (ypad * WindowFrameScale(window) + 50) / 100;

        tf = DRI(msg->wdp_Dri)->dri_Font;
        SetFont(rp, tf);

        titlelen = strlen(window->Title);
        textlen = TextFit(rp
                          , window->Title
                          , titlelen
                          , &te
                          , NULL
                          , 1
                          , right - left - (window->BorderLeft + window->BorderRight)
                          , window->BorderTop - (ypad << 1));

        if (textlen)
        {
#define WINTITLEPAD 3
            left = left + (WINTITLEPAD * WindowFrameScale(window) + 50) / 100;

            SetAPen(rp, pens[(window->Flags & WFLG_WINDOWACTIVE) ? FILLTEXTPEN : TEXTPEN]);

            Move(rp, left, ypad + tf->tf_Baseline + ((window->BorderTop - tf->tf_YSize) >> 1));
            Text(rp, window->Title, textlen);
        }
    }

    return TRUE;
}

/**************************************************************************************************/

IPTR WinDecorClass__WDM_DRAW_WINBORDER(Class *cl, Object *obj, struct wdpDrawWinBorder *msg)
{
    struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)cl->cl_UserData;
    struct GfxBase       *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct RastPort      *rp = msg->wdp_RPort;
    struct Window        *window = msg->wdp_Window;
    UWORD                *pens = DRI(msg->wdp_Dri)->dri_Pens;
    LONG                  left, right;
    LONG unit = (WindowFrameScale(window) + 50) / 100;

    SetDrMd(rp, JAM1);
    SetAPen(rp, pens[SHINEPEN]);

    if (window->BorderTop > 0)
    {
        /* Outer shine edge on top side */

        CheckRectFill(rp, 0, 0, window->Width - 1, unit - 1, IntuitionBase);
    }

    if (!(msg->wdp_Flags & WDF_DWB_TOP_ONLY))
    {
        if (window->BorderLeft > 0)
        {
            /* Outer shine edge on left side */

            CheckRectFill(rp, 0, 0, unit - 1, window->Height - 1, IntuitionBase);
        }

        if (window->BorderRight > unit)
        {
            /* Inner shine edge on right side */

            CheckRectFill(rp,
                      window->Width - window->BorderRight, window->BorderTop,
                          window->Width - window->BorderRight + unit - 1, window->Height - window->BorderBottom + unit - 1,
                          IntuitionBase);
        }

        if (window->BorderBottom > unit)
        {
            /* Inner shine edge on bottom side */

            CheckRectFill(rp,
                          window->BorderLeft, window->Height - window->BorderBottom,
                          window->Width - window->BorderRight + unit - 1, window->Height - window->BorderBottom + unit - 1,
                          IntuitionBase);
        }
    }

    SetAPen(rp, pens[SHADOWPEN]);

    if (!(msg->wdp_Flags & WDF_DWB_TOP_ONLY))
    {
        if (window->BorderRight > 0)
        {
            /* Outer shadow edge on right side */

            CheckRectFill(rp, window->Width - unit, unit,
                      window->Width - 1, window->Height - 1, IntuitionBase);
        }

        if (window->BorderBottom > 0)
        {
            /* Outer shadow edge on bottom side */

            CheckRectFill(rp, unit, window->Height - unit,
                  window->Width - 1, window->Height - 1, IntuitionBase);
        }

        if (window->BorderLeft > unit)
        {
            /* Inner shadow edge on left side */

            CheckRectFill(rp, window->BorderLeft - unit, window->BorderTop - unit,
                      window->BorderLeft - 1, window->Height - window->BorderBottom + unit - 1,
                      IntuitionBase);
        }
    }

    if (window->BorderTop > unit)
    {
        /* Inner shadow edge on top side */

        CheckRectFill(rp, window->BorderLeft - unit, window->BorderTop - unit,
                      window->Width - window->BorderRight + unit - 1, window->BorderTop - 1,
                      IntuitionBase);
    }

    SetAPen(rp, pens[(window->Flags & WFLG_WINDOWACTIVE) ? FILLPEN : BACKGROUNDPEN]);

    if (window->BorderTop > unit * 2)
    {
        /* Fill on top side */

        CheckRectFill(rp, unit, unit, window->Width - unit - 1, window->BorderTop - unit - 1, IntuitionBase);
    }

    if (!(msg->wdp_Flags & WDF_DWB_TOP_ONLY))
    {
        if (window->BorderLeft > unit * 2)
        {
            /* Fill on left side */

            CheckRectFill(rp, unit, unit, window->BorderLeft - unit - 1, window->Height - unit - 1, IntuitionBase);
        }

        if (window->BorderRight > unit * 2)
        {
            /* Fill on right side */

            CheckRectFill(rp, window->Width - window->BorderRight + unit, unit,
                      window->Width - unit - 1, window->Height - unit - 1, IntuitionBase);
        }

        if (window->BorderBottom > unit * 2)
        {
            /* Fill on bottom side */

            CheckRectFill(rp, unit, window->Height - window->BorderBottom + unit,
                          window->Width - unit - 1, window->Height - unit - 1, IntuitionBase);
        }
    }

    findtitlearea(window, &left, &right);

    if (left != 0)
    {
        /* Left edge of title area */

        SetAPen(rp, pens[SHINEPEN]);
        CheckRectFill(rp, left, unit, left + unit - 1, window->BorderTop - unit - 1, IntuitionBase);
    }

    if (right != window->Width - 1)
    {
        /* Right edges of title area */

        SetAPen(rp, pens[SHADOWPEN]);
        CheckRectFill(rp, right - unit + 1, unit, right, window->BorderTop - unit - 1, IntuitionBase);
    }

    if (window->Title) INTERNAL_WDM_DRAW_WINTITLE(cl, obj, msg);

    return TRUE;
}

/**************************************************************************************************/

static IPTR window_layout_border_gadgets(struct wdpLayoutBorderGadgets *msg)
{
    struct Gadget        *gadget = msg->wdp_Gadgets;

    if (!(msg->wdp_Flags & WDF_LBG_SYSTEMGADGET)) return TRUE;

    while(gadget)
    {
        switch(gadget->GadgetType & GTYP_SYSTYPEMASK)
        {
        case GTYP_SIZING:
            gadget->LeftEdge = -gadget->Width + 1;
            gadget->TopEdge = -gadget->Height + 1;
            gadget->Flags &= ~(GFLG_RELWIDTH | GFLG_RELHEIGHT);
            gadget->Flags |= GFLG_RELRIGHT | GFLG_RELBOTTOM;
            break;
        case GTYP_CLOSE:
            gadget->LeftEdge = 0;
            gadget->Width = gadget->Height;
            gadget->Flags &= ~(GFLG_RELRIGHT | GFLG_RELWIDTH);
            break;

        case GTYP_WDEPTH:
            gadget->LeftEdge = -gadget->Height + 1;
            gadget->Width = gadget->Height;
            gadget->Flags &= ~GFLG_RELWIDTH;
            gadget->Flags |= GFLG_RELRIGHT;
            break;

        case GTYP_WZOOM:
            gadget->LeftEdge = -gadget->Height * 2 + 1;
            gadget->Width = gadget->Height;
            gadget->Flags &= ~GFLG_RELWIDTH;
            gadget->Flags |= GFLG_RELRIGHT;
            break;

        case GTYP_WDRAGGING:
            gadget->LeftEdge = 0;
            gadget->Width = 0;
            gadget->Flags &= ~GFLG_RELRIGHT;
            gadget->Flags |= GFLG_RELWIDTH;
            break;
        }

        if (gadget->GadgetType == GTYP_CUSTOMGADGET)
        {
            switch(gadget->GadgetID)
            {
            case ETI_Iconify:
                gadget->LeftEdge = -gadget->Height * 3 + 1;
                gadget->Width = gadget->Height;
                gadget->Flags &= ~GFLG_RELWIDTH;
                gadget->Flags |= GFLG_RELRIGHT;
                break;
            }
        }

        if (msg->wdp_Flags & WDF_LBG_MULTIPLE)
        {
            gadget = gadget->NextGadget;
        }
        else
        {
            gadget = NULL;
        }
    }

    return TRUE;
}

IPTR WinDecorClass__WDM_LAYOUT_BORDERGADGETS(Class *cl, Object *obj,
    struct wdpLayoutBorderGadgets *msg)
{
    return window_layout_border_gadgets(msg);
}

/**************************************************************************************************/

IPTR WinDecorClass__WDM_DRAW_BORDERPROPBACK(Class *cl, Object *obj, struct wdpDrawBorderPropBack *msg)
{
    struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)cl->cl_UserData;
    struct GfxBase       *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct Window        *window = msg->wdp_Window;
    struct RastPort      *rp = msg->wdp_RPort;
    struct Gadget        *gadget = msg->wdp_Gadget;
    struct Rectangle     *r = msg->wdp_RenderRect;
    struct PropInfo      *pi = ((struct PropInfo *)gadget->SpecialInfo);
    UWORD                *pens = DRI(msg->wdp_Dri)->dri_Pens;

    SetDrMd(rp, JAM2);

    if (pi->Flags & PROPNEWLOOK)
    {
        static CONST UWORD pattern[] = {0x5555,0xAAAA};

        SetAfPt(rp, pattern, 1);
        SetAPen(rp, pens[SHADOWPEN]);
        SetBPen(rp, pens[(window->Flags & WFLG_WINDOWACTIVE) ? FILLPEN : BACKGROUNDPEN]);
        RectFill(rp, r->MinX, r->MinY, r->MaxX, r->MaxY);
        SetAfPt(rp, NULL, 0);
    }
    else
    {
        SetAPen(rp, pens[BACKGROUNDPEN]);
        RectFill(rp, r->MinX, r->MinY, r->MaxX, r->MaxY);
    }

    return TRUE;
}

/**************************************************************************************************/

IPTR WinDecorClass__WDM_DRAW_BORDERPROPKNOB(Class *cl, Object *obj, struct wdpDrawBorderPropKnob *msg)
{
    struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)cl->cl_UserData;
    struct GfxBase       *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct Window        *window = msg->wdp_Window;
    struct RastPort      *rp = msg->wdp_RPort;
    struct Gadget        *gadget = msg->wdp_Gadget;
    struct Rectangle     *r = msg->wdp_RenderRect;
    struct PropInfo      *pi = ((struct PropInfo *)gadget->SpecialInfo);
    UWORD                *pens = DRI(msg->wdp_Dri)->dri_Pens;

    SetDrMd(rp, JAM2);

    if (pi->Flags & PROPBORDERLESS)
    {
        SetAPen(rp, pens[SHINEPEN]);

        /* Top edge */
        RectFill(rp, r->MinX, r->MinY, r->MaxX - 1, r->MinY);

        /* Left edge */
        RectFill(rp, r->MinX, r->MinY + 1, r->MinX, r->MaxY - 1);

        SetAPen(rp, pens[SHADOWPEN]);

        /* Right edge */
        RectFill(rp, r->MaxX, r->MinY, r->MaxX, r->MaxY);

        /* Bottom edge */
        RectFill(rp, r->MinX, r->MaxY, r->MaxX - 1, r->MaxY);

        r->MinX++;
        r->MinY++;
        r->MaxX--;
        r->MaxY--;

    } /* PROPBORDERLESS */
    else
    {
        SetAPen(rp, pens[SHADOWPEN]);

        if (pi->Flags & FREEHORIZ)
        {
            /* black line at the left and at the right */

            RectFill(rp, r->MinX, r->MinY, r->MinX, r->MaxY);
            RectFill(rp, r->MaxX, r->MinY, r->MaxX, r->MaxY);

            r->MinX++;
            r->MaxX--;
        }

        if (pi->Flags & FREEVERT)
        {
            /* black line at the top and at the bottom */

            RectFill(rp, r->MinX, r->MinY, r->MaxX, r->MinY);
            RectFill(rp, r->MinX, r->MaxY, r->MaxX, r->MaxY);

            r->MinY++,
            r->MaxY--;
        }

    } /* not PROPBORDERLESS */

    SetAPen(rp, pens[(window->Flags & WFLG_WINDOWACTIVE) ? FILLPEN : BACKGROUNDPEN]);

    /* interior */
    RectFill(rp, r->MinX, r->MinY, r->MaxX, r->MaxY);

    return TRUE;
}


/**************************************************************************************************/

IPTR WinDecorClass__WDM_INITWINDOW(Class *cl, Object *obj, struct wdpInitWindow *msg)
{
    return TRUE;
}

/**************************************************************************************************/

IPTR WinDecorClass__WDM_EXITWINDOW(Class *cl, Object *obj, struct wdpExitWindow *msg)
{
    return TRUE;
}

/**************************************************************************************************/

IPTR WinDecorClass__WDM_WINDOWSHAPE(Class *cl, Object *obj, struct wdpWindowShape *msg)
{
    return 0;
}

IPTR WinDecorClass__WDM_PREPARESCALESHAPE(Class *cl, Object *obj,
    struct wdpPrepareScaleShape *msg)
{
    if (!msg->wdp_Result) return FALSE;
    *msg->wdp_Result = NULL;
    if (OCLASS(obj) != cl || !msg->wdp_Window || !msg->wdp_Font ||
        msg->wdp_Width <= 0 || msg->wdp_Width > 32767 ||
        msg->wdp_Height <= 0 || msg->wdp_Height > 32767 ||
        msg->wdp_BorderTop < 0 || msg->wdp_BorderTop > 127 ||
        msg->wdp_BorderTop > msg->wdp_Height) return FALSE;
    return TRUE; /* built-in default is a rectangle, distinct from OOM */
}

/* Explicit immutable render-domain images. These vectors use physical stroke
 * widths while their dimensions originate once from the100% baseline. */
static ULONG window_scale_metric(ULONG value, ULONG scale)
{
    return (value * scale + 50) / 100;
}

BOOL WindowScaleVectorSize(struct wdpGetScaleDefSizeSysImage *msg,
    struct IntuitionBase *IntuitionBase)
{
    ULONG width, height, scale;
    (void)IntuitionBase;
    if (!msg->base.wdp_Width || !msg->base.wdp_Height || !msg->context ||
        !msg->context->src_LogicalDrawInfo || !msg->context->src_PhysicalDrawInfo ||
        msg->context->src_Scale < 100 || msg->context->src_Scale > 400 ||
        msg->context->src_Scale % 25) return FALSE;
    if (msg->prepared) *msg->prepared = NULL; /* vectors own no bitmap */
    switch (msg->base.wdp_Which)
    {
        case LEFTIMAGE: case RIGHTIMAGE: case UPIMAGE: case DOWNIMAGE:
        case CLOSEIMAGE: case DEPTHIMAGE: case ZOOMIMAGE: case SIZEIMAGE:
        case ICONIFYIMAGE: case LOCKIMAGE: case MUIIMAGE: case POPUPIMAGE:
        case SNAPSHOTIMAGE: case JUMPIMAGE: break;
        default: return FALSE;
    }
    scale = msg->logical ? 100 : msg->context->src_Scale;
    width = height = window_scale_metric(DEFSIZE_WIDTH, scale);
    if (msg->requestedHeight > 32767 || msg->requestedWidth > 32767) return FALSE;
    if (msg->requestedHeight && !msg->requestedWidth)
    {
        UQUAD aspect = ((UQUAD)width * msg->requestedHeight + height / 2) / height;
        if (!aspect || aspect > 32767) return FALSE;
        width = aspect;
    }
    if (msg->requestedWidth) width = msg->requestedWidth;
    if (msg->requestedHeight) height = msg->requestedHeight;
    if (!width || !height || width > 32767 || height > 32767) return FALSE;
    *msg->base.wdp_Width = width; *msg->base.wdp_Height = height;
    return TRUE;
}

/* Every primitive clips to the requested image box, including tiny explicit
 * caller extents smaller than a normal stroke or inset. */
static void window_scale_rect(struct wdpDrawSysImage *m, LONG l, LONG t, LONG r, LONG b,
    LONG logicalWidth, LONG logicalHeight, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r >= logicalWidth) r = logicalWidth - 1;
    if (b >= logicalHeight) b = logicalHeight - 1;
    if (l <= r && t <= b)
    {
        LONG left = ((UQUAD)l * m->wdp_Width + logicalWidth / 2) / logicalWidth;
        LONG top = ((UQUAD)t * m->wdp_Height + logicalHeight / 2) / logicalHeight;
        LONG right = ((UQUAD)(r + 1) * m->wdp_Width + logicalWidth / 2) / logicalWidth - 1;
        LONG bottom = ((UQUAD)(b + 1) * m->wdp_Height + logicalHeight / 2) / logicalHeight - 1;
        if (left <= right && top <= bottom)
            RectFill(m->wdp_RPort, m->wdp_X + left, m->wdp_Y + top,
                m->wdp_X + right, m->wdp_Y + bottom);
    }
}

BOOL WindowScaleVectorDraw(struct wdpDrawScaleSysImage *msg,
    struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct wdpDrawSysImage *m = &msg->base;
    ULONG scale;
    LONG stroke, inset, w, h, i;
    UWORD *pens;
    BOOL selected;
    if (!msg->context || !msg->context->src_LogicalDrawInfo ||
        !msg->context->src_PhysicalDrawInfo || msg->prepared || !m->wdp_Dri ||
        !m->wdp_RPort || m->wdp_Width < 1 || m->wdp_Height < 1 ||
        m->wdp_Width > 32767 || m->wdp_Height > 32767 ||
        msg->context->src_Scale < 100 || msg->context->src_Scale > 400 ||
        msg->context->src_Scale % 25) return FALSE;
    switch (m->wdp_Which)
    {
        case LEFTIMAGE: case RIGHTIMAGE: case UPIMAGE: case DOWNIMAGE:
        case CLOSEIMAGE: case DEPTHIMAGE: case ZOOMIMAGE: case SIZEIMAGE:
        case ICONIFYIMAGE: case LOCKIMAGE: case MUIIMAGE: case POPUPIMAGE:
        case SNAPSHOTIMAGE: case JUMPIMAGE: break;
        default: return FALSE;
    }
    scale = msg->logical ? 100 : msg->context->src_Scale;
    /* Construct vectors in canonical units, then round each rectangle edge
     * once into the final image. Integer scales preserve all glyph/stroke
     * pixels; fractional scales draw directly with sharp raster boundaries. */
    w = ((UQUAD)m->wdp_Width * 100 + scale / 2) / scale;
    h = ((UQUAD)m->wdp_Height * 100 + scale / 2) / scale;
    if (!w) w = 1;
    if (!h) h = 1;
    stroke = 1;
    inset = 3;
    pens = m->wdp_Dri->dri_Pens;
    selected = m->wdp_State == IDS_SELECTED || m->wdp_State == IDS_INACTIVESELECTED;
    SetDrMd(m->wdp_RPort, JAM1);
    SetAPen(m->wdp_RPort, getbgpen(m->wdp_State, pens));
    window_scale_rect(m, 0, 0, w - 1, h - 1, w, h, IntuitionBase);
    SetAPen(m->wdp_RPort, pens[selected ? SHADOWPEN : SHINEPEN]);
    window_scale_rect(m, 0, 0, w - 1, stroke - 1, w, h, IntuitionBase);
    window_scale_rect(m, 0, 0, stroke - 1, h - 1, w, h, IntuitionBase);
    SetAPen(m->wdp_RPort, pens[selected ? SHINEPEN : SHADOWPEN]);
    window_scale_rect(m, w - stroke, stroke, w - 1, h - 1, w, h, IntuitionBase);
    window_scale_rect(m, stroke, h - stroke, w - 1, h - 1, w, h, IntuitionBase);
    SetAPen(m->wdp_RPort, pens[selected ? SHINEPEN : SHADOWPEN]);
    if (inset * 2 >= w || inset * 2 >= h) return TRUE;
    switch (m->wdp_Which)
    {
        case CLOSEIMAGE:
            window_scale_rect(m, inset, inset, w - inset - 1, h - inset - 1, w, h, IntuitionBase);
            break;
        case ZOOMIMAGE: case DEPTHIMAGE: case SNAPSHOTIMAGE:
            window_scale_rect(m, inset, inset, w - inset - 1, inset + stroke - 1, w, h, IntuitionBase);
            window_scale_rect(m, inset, inset, inset + stroke - 1, h - inset - 1, w, h, IntuitionBase);
            window_scale_rect(m, w - inset - stroke, inset, w - inset - 1, h - inset - 1, w, h, IntuitionBase);
            window_scale_rect(m, inset, h - inset - stroke, w - inset - 1, h - inset - 1, w, h, IntuitionBase);
            if (m->wdp_Which != ZOOMIMAGE)
                window_scale_rect(m, inset + stroke, h / 2, w - inset - stroke, h / 2 + stroke - 1, w, h, IntuitionBase);
            break;
        case LEFTIMAGE: case RIGHTIMAGE:
            for (i = 0; i < (h - inset * 2 + 1) / 2; ++i)
            {
                LONG x = m->wdp_Which == LEFTIMAGE ? inset + i : w - inset - 1 - i;
                window_scale_rect(m, x, h / 2 - i, x + stroke - 1, h / 2 + i, w, h, IntuitionBase);
            }
            break;
        case UPIMAGE: case DOWNIMAGE:
            for (i = 0; i < (w - inset * 2 + 1) / 2; ++i)
            {
                LONG y = m->wdp_Which == UPIMAGE ? inset + i : h - inset - 1 - i;
                window_scale_rect(m, w / 2 - i, y, w / 2 + i, y + stroke - 1, w, h, IntuitionBase);
            }
            break;
        case SIZEIMAGE:
            for (i = inset; i < w - inset; i += stroke * 3)
                window_scale_rect(m, i, h - inset - stroke, i + stroke - 1, h - inset - 1, w, h, IntuitionBase);
            break;
        case LOCKIMAGE:
            window_scale_rect(m, inset, h / 2, w - inset - 1, h - inset - 1, w, h, IntuitionBase);
            window_scale_rect(m, inset + stroke, inset, w - inset - stroke - 1, inset + stroke - 1, w, h, IntuitionBase);
            window_scale_rect(m, inset + stroke, inset, inset + stroke * 2 - 1, h / 2, w, h, IntuitionBase);
            window_scale_rect(m, w - inset - stroke * 2, inset, w - inset - stroke - 1, h / 2, w, h, IntuitionBase);
            break;
        default:
            window_scale_rect(m, inset, h / 2, w - inset - 1, h / 2 + stroke - 1, w, h, IntuitionBase);
            window_scale_rect(m, w / 2, inset, w / 2 + stroke - 1, h - inset - 1, w, h, IntuitionBase);
            break;
    }
    return TRUE;
}

static void window_scale_refuse(struct wdpScaleRefusal *refusal, ULONG code)
{
    if (refusal) refusal->wsr_Code = code;
}
IPTR WinDecorClass__WDM_GETSCALEDEFSIZE_SYSIMAGE(Class *cl, Object *obj,
    struct wdpGetScaleDefSizeSysImage *msg)
{
    if (OCLASS(obj) != cl)
    {
        window_scale_refuse(msg->refusal, WDP_REFUSE_UNSUPPORTED);
        return WDP_SCALE_FAILED;
    }
    return WindowScaleVectorSize(msg, (struct IntuitionBase *)cl->cl_UserData);
}
IPTR WinDecorClass__WDM_DRAWSCALE_SYSIMAGE(Class *cl, Object *obj,
    struct wdpDrawScaleSysImage *msg)
{
    return OCLASS(obj) == cl && WindowScaleVectorDraw(msg,
        (struct IntuitionBase *)cl->cl_UserData);
}
IPTR WinDecorClass__WDM_RELEASESCALE_SYSIMAGE(Class *cl, Object *obj,
    struct wdpReleaseScaleSysImage *msg)
{
    return OCLASS(obj) == cl && !msg->prepared;
}
IPTR WinDecorClass__WDM_PREPARESCALEWINDOW(Class *cl, Object *obj,
    struct wdpPrepareScaleWindow *msg)
{
    if (OCLASS(obj) != cl)
    {
        window_scale_refuse(msg->refusal, WDP_REFUSE_UNSUPPORTED);
        return FALSE;
    }
    if (!msg->context || !msg->font || msg->width <= 0 || msg->height <= 0)
    {
        if (msg->refusal)
        {
            msg->refusal->wsr_Code = WDP_REFUSE_ARGUMENTS;
            msg->refusal->wsr_Value[0] = msg->context ? msg->context->src_Scale : 0;
            msg->refusal->wsr_Value[1] = msg->width;
            msg->refusal->wsr_Value[2] = msg->height;
        }
        return FALSE;
    }
    return TRUE;
}
/* Not exact-class: an admitted subclass (WDA_PreparedScaleWindow, exact
 * WDM_PREPARESCALEWINDOW) delegates its generic gadget placement here
 * through DoSuperMethodA, where OCLASS(obj) is that subclass. */
IPTR WinDecorClass__WDM_LAYOUTSCALEBORDERGADGETS(Class *cl, Object *obj,
    struct wdpLayoutScaleBorderGadgets *msg)
{
    if (!msg->context || !msg->base.wdp_Window || !msg->base.wdp_Gadgets)
    {
        if (msg->refusal)
        {
            msg->refusal->wsr_Code = WDP_REFUSE_ARGUMENTS;
            msg->refusal->wsr_Value[0] = msg->context ? msg->context->src_Scale : 0;
            msg->refusal->wsr_Value[1] = msg->base.wdp_Window ? msg->base.wdp_Window->Width : 0;
            msg->refusal->wsr_Value[2] = msg->base.wdp_Window ? msg->base.wdp_Window->Height : 0;
        }
        return FALSE;
    }
    return window_layout_border_gadgets(&msg->base);
}
