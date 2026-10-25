/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    This file is part of AROS and is distributed under the AROS Public License 1.1.
*/
#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <intuition/imageclass.h>
#include "asl_intern.h"
#include "layout.h"

#define ASLFRAME_LayoutData (IA_Dummy + 0x500)

Object *AslFrame(struct LayoutData *ld, struct TagItem *tags, struct AslBase_intern *AslBase)
{
    struct TagItem context[] = {{ASLFRAME_LayoutData, (IPTR)ld}, {TAG_MORE, (IPTR)tags}};
    return NewObjectA(AslBase->aslframeclass, NULL, context);
}

#define AslBase ((struct AslBase_intern *)cl->cl_UserData)

static VOID FrameAttributes(struct AslFrameData *d, struct TagItem *tags)
{
    struct TagItem *ti;
    while ((ti = NextTagItem(&tags)))
        switch (ti->ti_Tag)
        {
            case ASLFRAME_LayoutData: d->ld = (APTR)ti->ti_Data; break;
            case IA_FrameType: d->type = ti->ti_Data; break;
            case IA_Recessed: d->recessed = ti->ti_Data != 0; break;
            case IA_EdgesOnly: d->edges = ti->ti_Data != 0; break;
        }
}
IPTR AslFrame__OM_NEW(Class *cl, Object *o, struct opSet *m)
{
    o = (Object *)DoSuperMethodA(cl, o, (Msg)m);
    if (o) FrameAttributes(INST_DATA(cl, o), m->ops_AttrList);
    return (IPTR)o;
}
IPTR AslFrame__OM_SET(Class *cl, Object *o, struct opSet *m)
{
    FrameAttributes(INST_DATA(cl, o), m->ops_AttrList);
    return DoSuperMethodA(cl, o, (Msg)m);
}
IPTR AslFrame__IM_FRAMEBOX(Class *cl, Object *o, struct impFrameBox *m)
{
    struct AslFrameData *d = INST_DATA(cl, o);
    LONG inset = AslScaleValue(d->ld, d->type == FRAME_RIDGE ? 2 : 1);
    if (m->imp_FrameFlags & FRAMEF_SPECIFY)
    {
        m->imp_FrameBox->Left = m->imp_ContentsBox->Left + (m->imp_ContentsBox->Width - m->imp_FrameBox->Width) / 2;
        m->imp_FrameBox->Top = m->imp_ContentsBox->Top + (m->imp_ContentsBox->Height - m->imp_FrameBox->Height) / 2;
    }
    else
    {
        m->imp_FrameBox->Left = m->imp_ContentsBox->Left - inset;
        m->imp_FrameBox->Top = m->imp_ContentsBox->Top - inset;
        m->imp_FrameBox->Width = m->imp_ContentsBox->Width + 2 * inset;
        m->imp_FrameBox->Height = m->imp_ContentsBox->Height + 2 * inset;
    }
    return TRUE;
}
static IPTR PaintFrame(Class *cl, struct Image *im, struct impDraw *m, LONG w, LONG h)
{
    struct AslFrameData *d = INST_DATA(cl, im);
    struct RastPort *rp = m->imp_RPort;
    struct DrawInfo *di = d->ld ? d->ld->ld_Dri : m->imp_DrInfo;
    LONG x = im->LeftEdge + m->imp_Offset.X, y = im->TopEdge + m->imp_Offset.Y;
    LONG stroke = AslScaleValue(d->ld, 1), inset, n;
    ULONG olda = rp->FgPen, oldb = rp->BgPen, oldmode = rp->DrawMode;
    UWORD shine, shadow;
    BOOL selected = m->imp_State == IDS_SELECTED || m->imp_State == IDS_INACTIVESELECTED;
    if (!di || w < 2 * stroke || h < 2 * stroke) return FALSE;
    shine = di->dri_Pens[SHINEPEN]; shadow = di->dri_Pens[SHADOWPEN];
    if (d->recessed != selected) { UWORD t = shine; shine = shadow; shadow = t; }
    inset = stroke * (d->type == FRAME_RIDGE ? 2 : 1);
    SetDrMd(rp, JAM1);
    for (n = 0; n < inset && n * 2 < w && n * 2 < h; ++n)
    {
        BOOL inner = n >= stroke;
        SetAPen(rp, inner ? shadow : shine);
        Move(rp, x+n, y+h-n-1); Draw(rp, x+n, y+n); Draw(rp, x+w-n-1, y+n);
        SetAPen(rp, inner ? shine : shadow);
        Draw(rp, x+w-n-1, y+h-n-1); Draw(rp, x+n+1, y+h-n-1);
    }
    if (!d->edges && w > 2 * inset && h > 2 * inset)
    {
        SetAPen(rp, di->dri_Pens[selected ? FILLPEN : BACKGROUNDPEN]);
        RectFill(rp, x+inset, y+inset, x+w-inset-1, y+h-inset-1);
    }
    SetABPenDrMd(rp, olda, oldb, oldmode);
    return TRUE;
}
IPTR AslFrame__IM_DRAW(Class *cl, struct Image *im, struct impDraw *m)
{ return PaintFrame(cl, im, m, im->Width, im->Height); }
IPTR AslFrame__IM_DRAWFRAME(Class *cl, struct Image *im, struct impDraw *m)
{ struct impDraw *f = m; return PaintFrame(cl, im, m, f->imp_Dimensions.Width, f->imp_Dimensions.Height); }
