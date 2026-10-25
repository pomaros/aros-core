/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright  1995-2011, The AROS Development Team. All rights reserved.
    Copyright  2001-2003, The MorphOS Development Team. All Rights Reserved.
*/


#include <dos/dos.h>
#include <dos/dosextens.h>

#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/menudecorclass.h>
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
#include "menuscale.h"


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
#define REFHEIGHT (msg->mdp_ReferenceFont->tf_YSize)
#define REFWIDTH  REFHEIGHT

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

#undef DEBUG
#define DEBUG 0
#include <aros/debug.h>
/**************************************************************************************************/

void menu_draw_thick_line(Class *cl, struct RastPort *rport,
                     LONG x1, LONG y1, LONG x2, LONG y2,
                     UWORD thickness)
{
    struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)cl->cl_UserData;
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;

    Move(rport, x1, y1);
    Draw(rport, x2, y2);
    /* Georg Steger */
    Move(rport, x1 + 1, y1);
    Draw(rport, x2 + 1, y2);
}

IPTR MenuDecorClass__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)cl->cl_UserData;
    struct Library *UtilityBase = GetPrivIBase(IntuitionBase)->UtilityBase;
    struct scrdecor_data *data;
    
    obj = (Object *)DoSuperMethodA(cl, obj, (Msg)msg);
    if (obj)
    {
        data = INST_DATA(cl, obj);

        data->userbuffersize = (ULONG) GetTagData(MDA_UserBuffer, 0, msg->ops_AttrList);
        
    }
    
    return (IPTR)obj;
}

/**************************************************************************************************/

IPTR MenuDecorClass__OM_GET(Class *cl, Object *obj, struct opGet *msg)
{
    struct scrdecor_data *data = INST_DATA(cl, obj);

    switch(msg->opg_AttrID)
    {
        case MDA_UserBuffer:
            *msg->opg_Storage = (IPTR) data->userbuffersize;
            break;

        case MDA_TrueColorOnly:
            *msg->opg_Storage = FALSE;
            break;
            
        default:
            return DoSuperMethodA(cl, obj, (Msg)msg);
    }
    
    return 1;
}
    

/**************************************************************************************************/

IPTR MenuDecorClass__MDM_GETDEFSIZE_SYSIMAGE(Class *cl, Object *obj, struct mdpGetDefSizeSysImage *msg)
{
    struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)cl->cl_UserData;
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;

    switch(msg->mdp_Which)
    {
        case SUBMENUIMAGE:
            *msg->mdp_Width = 0;
            *msg->mdp_Height = 0;
            struct  RastPort *rp = CreateRastPort();
            if (rp)
            {
                struct  TextExtent TextExt;
                SetFont(rp, msg->mdp_ReferenceFont);
                TextExtent(rp, "\xBB", 1, &TextExt);
                *msg->mdp_Width = TextExt.te_Width;
                *msg->mdp_Height = TextExt.te_Height;
                FreeRastPort(rp);
            }
               break;

        case MENUCHECK:
            *msg->mdp_Width = REFWIDTH / 2 + 4; // reffont->tf_XSize * 3 / 2;
            *msg->mdp_Height= REFHEIGHT;
            break;

        default:
               *msg->mdp_Width = DEFSIZE_WIDTH;
               *msg->mdp_Height = DEFSIZE_HEIGHT;
               break;
    }

    return TRUE;
}

IPTR MenuDecorClass__MDM_DRAW_SYSIMAGE(Class *cl, Object *obj, struct mdpDrawSysImage *msg)
{
    struct IntuitionBase *IntuitionBase = (struct IntuitionBase *)cl->cl_UserData;
    struct GfxBase       *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct RastPort      *rport = msg->mdp_RPort;
    UWORD                *pens = DRI(msg->mdp_Dri)->dri_Pens;
    LONG                  left = msg->mdp_X;
    LONG                  top = msg->mdp_Y;
    LONG                  width = msg->mdp_Width;
    LONG                  height = msg->mdp_Height;
    LONG                  right = left + width - 1;
    LONG                  bottom = top + height - 1;
    
    SetDrMd(rport, JAM1);
    
    switch(msg->mdp_Which)
    {
        case SUBMENUIMAGE:
        {
            if (MENUS_AMIGALOOK(IntuitionBase))
            {
                SetAPen(rport, pens[BARBLOCKPEN]);
            }
            else
            {
                SetAPen(rport, pens[(msg->mdp_State == IDS_SELECTED) ? FILLPEN : BACKGROUNDPEN]);
            }
            RectFill(rport, left, top, right, bottom);
            SetAPen(rport, pens[BARDETAILPEN]);
            SetDrMd(rport, JAM1);
            WORD x = left;

            Move(rport, x, top + rport->Font->tf_Baseline);
            Text(rport, "\xBB", 1);
            break;
        }

        case MENUCHECK:
        {
            if (MENUS_AMIGALOOK(IntuitionBase))
            {
                SetAPen(rport, pens[BARBLOCKPEN]);
            }
            else
            {
                SetAPen(rport, pens[(msg->mdp_State == IDS_SELECTED) ? FILLPEN : BACKGROUNDPEN]);
            }

            RectFill(rport, left, top, right, bottom);

            SetAPen(rport, pens[BARDETAILPEN]);
            menu_draw_thick_line(cl, rport, left + 1, top + height / 3 , left + 1, bottom, 0);
            menu_draw_thick_line(cl, rport, left + 2, bottom, right - 2, top, 0);

            break;
        }

        case AMIGAKEY:
        {
            struct TextFont *oldfont;
            UBYTE            oldstyle;
            
            if (MENUS_AMIGALOOK(IntuitionBase))
            {
                SetAPen(rport, pens[BARDETAILPEN]);
            }
            else
            {
                SetAPen(rport, pens[SHINEPEN]);
            }

            RectFill(rport, left, top, right, bottom);

            if (MENUS_AMIGALOOK(IntuitionBase))
            {
                SetAPen(rport, pens[BARBLOCKPEN]);
    
                oldfont = rport->Font;
                oldstyle = rport->AlgoStyle;
    
                SetFont(rport, GfxBase->DefaultFont);
                SetSoftStyle(rport, FSF_ITALIC, AskSoftStyle(rport));
    
                Move(rport, left + (width - rport->TxWidth) / 2,
                     top  + (height - rport->TxHeight) / 2 + rport->TxBaseline);
                Text(rport, "A", 1);
    
                SetSoftStyle(rport, oldstyle, AskSoftStyle(rport));
                SetFont(rport, oldfont);
    
                SetAPen(rport, pens[BARBLOCKPEN]);
            }
            else
            {
                SetAPen(rport, pens[SHADOWPEN]);
    
                RectFill(rport, left + 1, top, right - 1, top);
                RectFill(rport, right, top + 1, right, bottom - 1);
                RectFill(rport, left + 1, bottom, right - 1, bottom);
                RectFill(rport, left, top + 1, left, bottom - 1);
    
                SetAPen(rport, pens[BACKGROUNDPEN]);
                RectFill(rport, left + 1, bottom - 1, right - 1, bottom - 1);
                RectFill(rport, right - 1, top + 1, right - 1, bottom - 2);
    
                RectFill(rport, left + 2, top + 2, left + 4, top + 2);
                RectFill(rport, left + 2, top + 3, left + 2, top + 4);
    
                SetAPen(rport, pens[SHADOWPEN]);
                RectFill(rport, left + 2, bottom - 2, right - 2, bottom - 2);
                RectFill(rport, right - 2, top + 2, right - 2, bottom - 4);
    
                {
                    WORD a_size   = height - 7;
                    WORD a_left   = left + 5;
                    WORD a_top    = top + 2;
                    WORD a_right  = a_left + a_size;
                    WORD a_bottom = a_top + a_size;
    
                    Move(rport, a_left, a_bottom);
                    Draw(rport, a_right, a_top);
                    Draw(rport, a_right, a_bottom);
                    Move(rport, a_right - 1, a_top + 1);
                    Draw(rport, a_right - 1, a_bottom);
                }
    
                SetAPen(rport, pens[(msg->mdp_State == IDS_SELECTED) ? FILLPEN : BACKGROUNDPEN]);
            }
            
            WritePixel(rport, left, top);
            WritePixel(rport, right, top);
            WritePixel(rport, right, bottom);
            WritePixel(rport, left, bottom);
        }
    }
    return TRUE;
}

IPTR MenuDecorClass__MDM_GETMENUSPACES(Class *cl, Object *obj, struct mdpGetMenuSpaces *msg)
{
    return FALSE;
}

/**************************************************************************************************/

IPTR MenuDecorClass__MDM_DRAWBACKGROUND(Class *cl, Object *obj, struct mdpDrawBackground *msg)
{
    return FALSE;
}

IPTR MenuDecorClass__MDM_INITMENU(Class *cl, Object *obj, struct mdpInitMenu *msg)
{
    return FALSE;
}

IPTR MenuDecorClass__MDM_EXITMENU(Class *cl, Object *obj, struct mdpExitMenu *msg)
{
    return FALSE;
}

/**************************************************************************************************/

/* Context-qualified vector images. The exact-class check prevents a custom
 * subclass from inheriting a sharp-rendering promise for foreign resources. */
static BOOL menu_scale_context(Class *cl, Object *obj,
    const struct ScreenRenderContext *context)
{
    return OCLASS(obj) == cl && context && context->src_Scale >= 100 &&
        context->src_Scale <= 400 && !(context->src_Scale % 25) &&
        context->src_LogicalDrawInfo && context->src_PhysicalDrawInfo;
}
static ULONG menu_scale_metric(ULONG value, ULONG scale)
{
    return ((UQUAD)value * scale + 50) / 100;
}
IPTR MenuDecorClass__MDM_QUERYCAPABILITIES(Class *cl, Object *obj,
    struct mdpQueryCapabilities *msg)
{
    (void)msg;
    return OCLASS(obj) == cl ? MDM_CAP_SCALE : 0;
}
IPTR MenuDecorClass__MDM_GETSCALESPACES(Class *cl, Object *obj,
    struct mdpGetScaleSpaces *msg)
{
    /* The caller's defaults already came from this immutable context. */
    return menu_scale_context(cl, obj, msg->context);
}
BOOL MenuScaleVectorSize(struct mdpGetScaleDefSizeSysImage *msg,
    struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct TextFont *font;
    ULONG width, height, scale;
    struct RastPort rp;
    if (!msg->prepared || !msg->base.mdp_Width || !msg->base.mdp_Height ||
        !msg->context || !msg->context->src_LogicalDrawInfo ||
        msg->context->src_Scale < 100 || msg->context->src_Scale > 400 ||
        msg->context->src_Scale % 25) return FALSE;
    *msg->prepared = NULL; /* The built-in images are vectors. */
    scale = msg->logical ? 100 : msg->context->src_Scale;
    font = msg->logical ? msg->base.mdp_ReferenceFont :
        msg->context->src_LogicalDrawInfo->dri_Font;
    if (!font) return FALSE;
    switch (msg->base.mdp_Which)
    {
        case SUBMENUIMAGE:
            /* Measure the actual destination font, including a typeface change. */
            font = msg->base.mdp_ReferenceFont;
            if (!font) return FALSE;
            InitRastPort(&rp);
            SetFont(&rp, font);
            width = TextLength(&rp, "\xBB", 1);
            height = font->tf_YSize;
            DeinitRastPort(&rp);
            break;
        case MENUCHECK:
            width = menu_scale_metric(font->tf_YSize / 2 + 4, scale);
            height = menu_scale_metric(font->tf_YSize, scale);
            break;
        case AMIGAKEY:
            width = height = menu_scale_metric(DEFSIZE_WIDTH, scale);
            break;
        default: return FALSE;
    }
    if (!width || !height) return FALSE;
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
    *msg->base.mdp_Width = width;
    *msg->base.mdp_Height = height;
    return TRUE;
}
BOOL MenuScaleVectorDraw(struct mdpDrawScaleSysImage *msg,
    struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct mdpDrawSysImage *m = &msg->base;
    struct RastPort *rp = m->mdp_RPort;
    UWORD *pens;
    LONG left = m->mdp_X, top = m->mdp_Y, right, bottom;
    LONG stroke, i, inset;
    if (!msg->context || !msg->context->src_LogicalDrawInfo ||
        !msg->context->src_PhysicalDrawInfo ||
        msg->context->src_Scale < 100 || msg->context->src_Scale > 400 ||
        msg->context->src_Scale % 25 || msg->prepared || !rp ||
        (m->mdp_Which != SUBMENUIMAGE && m->mdp_Which != MENUCHECK && m->mdp_Which != AMIGAKEY) ||
        !m->mdp_Dri || m->mdp_Width <= 0 || m->mdp_Height <= 0 ||
        m->mdp_Width > 32767 || m->mdp_Height > 32767) return FALSE;
    right = left + m->mdp_Width - 1;
    bottom = top + m->mdp_Height - 1;
    stroke = menu_scale_metric(1, msg->logical ? 100 : msg->context->src_Scale);
    if (stroke > m->mdp_Width) stroke = m->mdp_Width;
    if (stroke > m->mdp_Height) stroke = m->mdp_Height;
    pens = m->mdp_Dri->dri_Pens;
    SetDrMd(rp, JAM1);
    SetAPen(rp, pens[MENUS_AMIGALOOK(IntuitionBase) ? BARBLOCKPEN :
        (m->mdp_State == IDS_SELECTED ? FILLPEN : BACKGROUNDPEN)]);
    RectFill(rp, left, top, right, bottom);
    SetAPen(rp, pens[BARDETAILPEN]);
    switch (m->mdp_Which)
    {
        case SUBMENUIMAGE:
            /* Two vector chevrons also respect explicit caller extents; a
             * destination-font glyph could overdraw a smaller requested box. */
            for (i = 0; i < stroke; i++)
            {
                LONG half = m->mdp_Width / 2;
                Move(rp, left, top + i);
                Draw(rp, left + half, top + m->mdp_Height / 2);
                Draw(rp, left, bottom - i);
                Move(rp, left + half, top + i);
                Draw(rp, right, top + m->mdp_Height / 2);
                Draw(rp, left + half, bottom - i);
            }
            break;
        case MENUCHECK:
            for (i = 0; i < stroke; i++)
            {
                Move(rp, left + i, top + m->mdp_Height / 2);
                Draw(rp, left + m->mdp_Width / 3 + i, bottom);
                Draw(rp, right, top + i);
            }
            break;
        case AMIGAKEY:
            /* Stroke the key border and A at the target pixel density. */
            for (i = 0; i < stroke; i++)
            {
                Move(rp, left + i, bottom - i); Draw(rp, left + i, top + i);
                Draw(rp, right - i, top + i); Draw(rp, right - i, bottom - i);
                Draw(rp, left + i, bottom - i);
            }
            inset = stroke * 2;
            if (m->mdp_Width > inset * 2 && m->mdp_Height > inset * 2)
                for (i = 0; i < stroke; i++)
                {
                    Move(rp, left + inset, bottom - inset);
                    Draw(rp, right - inset - i, top + inset);
                    Draw(rp, right - inset - i, bottom - inset);
                }
            break;
        default: return FALSE;
    }
    return TRUE;
}
IPTR MenuDecorClass__MDM_RELEASESCALE_SYSIMAGE(Class *cl, Object *obj,
    struct mdpReleaseScaleSysImage *msg)
{
    return OCLASS(obj) == cl && msg->prepared == NULL;
}

IPTR MenuDecorClass__MDM_INITSCALEMENU(Class *cl, Object *obj,
    struct mdpInitScaleMenu *msg)
{
    return menu_scale_context(cl, obj, msg->context);
}

IPTR MenuDecorClass__MDM_GETSCALEDEFSIZE_SYSIMAGE(Class *cl, Object *obj,
    struct mdpGetScaleDefSizeSysImage *msg)
{
    return menu_scale_context(cl, obj, msg->context) &&
        MenuScaleVectorSize(msg, (struct IntuitionBase *)cl->cl_UserData);
}
IPTR MenuDecorClass__MDM_DRAWSCALE_SYSIMAGE(Class *cl, Object *obj,
    struct mdpDrawScaleSysImage *msg)
{
    return menu_scale_context(cl, obj, msg->context) &&
        MenuScaleVectorDraw(msg, (struct IntuitionBase *)cl->cl_UserData);
}
