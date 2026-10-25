/*
    Copyright (C) 2026 John Knipper.
    Distributed under the AROS Public License, version 1.1.
*/
#ifndef SCALEGEAR_H
#define SCALEGEAR_H

/* Numeric values are steps: 0 = 100 %, 12 = 400 %. Numeric supplies
 * keyboard and wheel navigation; dragging the gear moves one step per
 * eight vertical pixels. The screen change is committed on release so
 * relayout cannot move a control while its pointer gesture is active.
 *
 * A held navigation key is the one gesture that changes the value several
 * times: Numeric's own handler applies every key-down and auto-repeat.
 * MUIA_ScaleGear_Held (notify only) is TRUE from the first key-down the gear
 * receives as active object until the next key-up or mouse button event, so
 * the editor can defer its explanation until the gesture is over. */
#define MUIA_ScaleGear_Held (TAG_USER + 0x400001)

struct ScaleGear_DATA
{
    struct MUI_EventHandlerNode handler;
    LONG start_y, start_value;
    BOOL dragging, held;
};

static void ScaleGear_EndHeld(Object *obj, struct ScaleGear_DATA *data)
{
    if (!data->held) return;
    data->held = FALSE;
    set(obj, MUIA_ScaleGear_Held, FALSE);
}

static IPTR ScaleGear__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    obj = (Object *)DoSuperNewTags(cl, obj, NULL,
        MUIA_Numeric_Min, 0, MUIA_Numeric_Max, 12,
        MUIA_Numeric_Default, 0, MUIA_CycleChain, TRUE,
        MUIA_ShortHelp, (IPTR)"Display scale: drag up to enlarge, down to reduce. Use the wheel or arrow keys. Reset restores 100%.",
        TAG_MORE, (IPTR)msg->ops_AttrList);
    if (obj)
    {
        struct ScaleGear_DATA *data = INST_DATA(cl, obj);
        data->handler.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
        /* Above Numeric's priority-0 key handler: the gesture is marked
         * before the value change that the key causes. */
        data->handler.ehn_Priority = 1;
        data->handler.ehn_Flags = MUI_EHF_GUIMODE;
        data->handler.ehn_Object = obj;
        data->handler.ehn_Class = cl;
    }
    return (IPTR)obj;
}

static IPTR ScaleGear__MUIM_Setup(Class *cl, Object *obj, struct MUIP_Setup *msg)
{
    struct ScaleGear_DATA *data = INST_DATA(cl, obj);
    if (!DoSuperMethodA(cl, obj, (Msg)msg)) return FALSE;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&data->handler);
    /* A CLI change rebuilds this window while retaining its object tree.
       Refresh the displayed percentage without firing its preview hook. */
    {
        struct Screen *screen = LockPubScreen("Workbench");
        IPTR scale = 100;
        if (screen)
        {
            GetAttr(SA_Scale, (Object *)screen, &scale);
            UnlockPubScreen(NULL, screen);
            NNSET(obj, MUIA_Numeric_Value, (scale - 100) / 25);
        }
    }
    return TRUE;
}

static IPTR ScaleGear__MUIM_Cleanup(Class *cl, Object *obj, struct MUIP_Cleanup *msg)
{
    struct ScaleGear_DATA *data = INST_DATA(cl, obj);
    data->dragging = FALSE;
    /* A hidden page cannot explain a deferred refusal; the editor's next
     * Setup starts without a gesture. */
    data->held = FALSE;
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&data->handler);
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static IPTR ScaleGear__MUIM_AskMinMax(Class *cl, Object *obj, struct MUIP_AskMinMax *msg)
{
    LONG side;
    DoSuperMethodA(cl, obj, (Msg)msg);
    side = _font(obj)->tf_YSize * 6;
    msg->MinMaxInfo->MinWidth += side;
    msg->MinMaxInfo->MinHeight += side;
    msg->MinMaxInfo->DefWidth += side;
    msg->MinMaxInfo->DefHeight += side;
    msg->MinMaxInfo->MaxWidth += side;
    msg->MinMaxInfo->MaxHeight += side;
    return TRUE;
}

static IPTR ScaleGear__MUIM_Draw(Class *cl, Object *obj, struct MUIP_Draw *msg)
{
    static const WORD sine[32] = {0,195,383,556,707,831,924,981,1000,981,924,831,707,556,383,195,0,-195,-383,-556,-707,-831,-924,-981,-1000,-981,-924,-831,-707,-556,-383,-195};
    struct RastPort *rp;
    LONG cx, cy, radius, i, r, x, y, step;
    char label[16];
    DoSuperMethodA(cl, obj, (Msg)msg);
    if (!(msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return 0;
    rp = _rp(obj);
    cx = (_mleft(obj) + _mright(obj)) / 2;
    cy = (_mtop(obj) + _mbottom(obj)) / 2;
    radius = (_mwidth(obj) < _mheight(obj) ? _mwidth(obj) : _mheight(obj)) / 2 - 2;
    DoMethod(obj, MUIM_DrawBackground, _mleft(obj), _mtop(obj), _mwidth(obj), _mheight(obj), _mleft(obj), _mtop(obj), 0);
    SetAPen(rp, _pens(obj)[MPEN_SHINE]);
    for (i = 0; i <= 32; ++i)
    {
        r = ((i % 4) == 0 || (i % 4) == 3) ? radius : radius * 4 / 5;
        x = cx + sine[i % 32] * r / 1000;
        y = cy - sine[(i + 8) % 32] * r / 1000;
        if (i) Draw(rp, x, y); else Move(rp, x, y);
    }
    SetAPen(rp, _pens(obj)[MPEN_SHADOW]);
    DrawEllipse(rp, cx, cy, radius / 2, radius / 2);
    step = XGET(obj, MUIA_Numeric_Value);
    i = (20 + step * 2) % 32;
    SetAPen(rp, _pens(obj)[MPEN_TEXT]);
    Move(rp, cx + sine[i] * radius * 3 / 5000, cy - sine[(i + 8) % 32] * radius * 3 / 5000);
    Draw(rp, cx + sine[i] * radius * 4 / 5000, cy - sine[(i + 8) % 32] * radius * 4 / 5000);
    snprintf(label, sizeof(label), "%d%%", (int)(100 + step * 25));
    SetFont(rp, _font(obj));
    SetDrMd(rp, JAM1);
    Move(rp, cx - TextLength(rp, label, strlen(label)) / 2, cy + _font(obj)->tf_Baseline / 2);
    Text(rp, label, strlen(label));
    return TRUE;
}

static IPTR ScaleGear__MUIM_HandleEvent(Class *cl, Object *obj, struct MUIP_HandleEvent *msg)
{
    struct ScaleGear_DATA *data = INST_DATA(cl, obj);
    struct IntuiMessage *imsg = msg->imsg;
    if (imsg)
    {
        BOOL inside;
        if (imsg->Class == IDCMP_RAWKEY)
        {
            /* Observe only. Numeric's own node applies the key; calling the
             * superclass here would apply it twice. */
            if (imsg->Code & IECODE_UP_PREFIX)
                ScaleGear_EndHeld(obj, data);
            else if (msg->muikey != MUIKEY_NONE && !data->held)
            {
                data->held = TRUE;
                set(obj, MUIA_ScaleGear_Held, TRUE);
            }
            return 0;
        }
        if (imsg->Class == IDCMP_MOUSEBUTTONS)
            ScaleGear_EndHeld(obj, data);
        inside = imsg->MouseX >= _left(obj) && imsg->MouseX <= _right(obj) && imsg->MouseY >= _top(obj) && imsg->MouseY <= _bottom(obj);
        if (imsg->Class == IDCMP_MOUSEBUTTONS && imsg->Code == SELECTDOWN && inside)
        {
            data->dragging = TRUE;
            data->start_y = imsg->MouseY;
            data->start_value = XGET(obj, MUIA_Numeric_Value);
            return MUI_EventHandlerRC_Eat;
        }
        if (imsg->Class == IDCMP_MOUSEBUTTONS && imsg->Code == SELECTUP && data->dragging)
        {
            data->dragging = FALSE;
            set(obj, MUIA_Numeric_Value, data->start_value + (data->start_y - imsg->MouseY) / 8);
            return MUI_EventHandlerRC_Eat;
        }
    }
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

ZUNE_CUSTOMCLASS_6(ScaleGear, NULL, MUIC_Numeric, NULL,
    OM_NEW, struct opSet *,
    MUIM_Setup, struct MUIP_Setup *,
    MUIM_Cleanup, struct MUIP_Cleanup *,
    MUIM_AskMinMax, struct MUIP_AskMinMax *,
    MUIM_Draw, struct MUIP_Draw *,
    MUIM_HandleEvent, struct MUIP_HandleEvent *);
#endif
