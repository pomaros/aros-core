/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright (C) 2002, The AROS Development Team.
    All rights reserved.
    
*/

#include <string.h>

#include <intuition/classes.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/keymap.h>
#include <proto/utility.h>

#include "mui.h"
#include "support.h"
#include "muimaster_intern.h"

extern struct Library *MUIMasterBase;

/**************************************************************************
 check if region is entirely within given bounds
**************************************************************************/
int isRegionWithinBounds(struct Region *r, int left, int top, int width,
    int height)
{
    if ((left <= r->bounds.MinX) && (left + width - 1 >= r->bounds.MaxX)
        && (top <= r->bounds.MinY) && (top + height - 1 >= r->bounds.MaxY))
        return 1;

    return 0;
}


/**************************************************************************
 Converts a Rawkey to a vanillakey
**************************************************************************/
ULONG ConvertKey(struct IntuiMessage * imsg)
{
    struct InputEvent event;
    UBYTE code = 0;
    event.ie_NextEvent = NULL;
    event.ie_Class = IECLASS_RAWKEY;
    event.ie_SubClass = 0;
    event.ie_Code = imsg->Code;
    event.ie_Qualifier = imsg->Qualifier;
    event.ie_EventAddress = (APTR *) * ((IPTR *) imsg->IAddress);
    MapRawKey(&event, &code, 1, NULL);
    return code;
}

/**************************************************************************
 Convenient way to get an attribute of an object easily. If the object
 doesn't support the attribute this call returns an undefined value. So use
 this call only if the attribute is known to be known by the object.
 Implemented as a macro when compiling with GCC.
**************************************************************************/
#ifndef __GNUC__
IPTR XGET(Object * obj, Tag attr)
{
    IPTR storage = 0;
    GetAttr(attr, obj, &storage);
    return storage;
}
#endif /* __GNUC__ */

/**************************************************************************
 Call the Setup Method of an given object, but before set the renderinfo
**************************************************************************/
IPTR DoSetupMethod(Object * obj, struct MUI_RenderInfo * info)
{
    /* MUI set the correct render info *before* it calls MUIM_Setup so please
     * only use this function instead of DoMethodA() */
    muiRenderInfo(obj) = info;
    return DoMethod(obj, MUIM_Setup, (IPTR) info);
}

IPTR DoShowMethod(Object * obj)
{
    IPTR ret;

    ret = DoMethod(obj, MUIM_Show);
    if (ret)
        _flags(obj) |= MADF_CANDRAW;
    return ret;
}

IPTR DoHideMethod(Object * obj)
{
    _flags(obj) &= ~MADF_CANDRAW;
    return DoMethod(obj, MUIM_Hide);
}


void *Node_Next(APTR node)
{
    if (node == NULL)
        return NULL;
    if (((struct MinNode *)node)->mln_Succ == NULL)
        return NULL;
    if (((struct MinNode *)node)->mln_Succ->mln_Succ == NULL)
        return NULL;
    return ((struct MinNode *)node)->mln_Succ;
}

void *List_First(APTR list)
{
    if (!((struct MinList *)list)->mlh_Head)
        return NULL;
    if (((struct MinList *)list)->mlh_Head->mln_Succ == NULL)
        return NULL;
    return ((struct MinList *)list)->mlh_Head;
}

/* subtract rectangle b from rectangle b. resulting rectangles will be put into
   destrectarray which must have place for at least 4 rectangles. Returns number
   of resulting rectangles */

WORD SubtractRectFromRect(struct Rectangle *a, struct Rectangle *b,
    struct Rectangle *destrectarray)
{
    struct Rectangle intersect;
    BOOL intersecting = FALSE;
    WORD numrects = 0;

    /* calc. intersection between a and b */

    if (a->MinX <= b->MaxX)
    {
        if (a->MinY <= b->MaxY)
        {
            if (a->MaxX >= b->MinX)
            {
                if (a->MaxY >= b->MinY)
                {
                    intersect.MinX = MAX(a->MinX, b->MinX);
                    intersect.MinY = MAX(a->MinY, b->MinY);
                    intersect.MaxX = MIN(a->MaxX, b->MaxX);
                    intersect.MaxY = MIN(a->MaxY, b->MaxY);

                    intersecting = TRUE;
                }
            }
        }
    }

    if (!intersecting)
    {
        destrectarray[numrects++] = *a;

    }                           /* not intersecting */
    else
    {
        if (intersect.MinY > a->MinY)   /* upper */
        {
            destrectarray->MinX = a->MinX;
            destrectarray->MinY = a->MinY;
            destrectarray->MaxX = a->MaxX;
            destrectarray->MaxY = intersect.MinY - 1;

            numrects++;
            destrectarray++;
        }

        if (intersect.MaxY < a->MaxY)   /* lower */
        {
            destrectarray->MinX = a->MinX;
            destrectarray->MinY = intersect.MaxY + 1;
            destrectarray->MaxX = a->MaxX;
            destrectarray->MaxY = a->MaxY;

            numrects++;
            destrectarray++;
        }

        if (intersect.MinX > a->MinX)   /* left */
        {
            destrectarray->MinX = a->MinX;
            destrectarray->MinY = intersect.MinY;
            destrectarray->MaxX = intersect.MinX - 1;
            destrectarray->MaxY = intersect.MaxY;

            numrects++;
            destrectarray++;
        }

        if (intersect.MaxX < a->MaxX)   /* right */
        {
            destrectarray->MinX = intersect.MaxX + 1;
            destrectarray->MinY = intersect.MinY;
            destrectarray->MaxX = a->MaxX;
            destrectarray->MaxY = intersect.MaxY;

            numrects++;
            destrectarray++;
        }

    }                           /* intersecting */

    return numrects;

}

ULONG IsObjectVisible(Object * child, struct Library * MUIMasterBase)
{
    Object *wnd;
    Object *obj;

    wnd = _win(child);
    obj = child;

    while (get(obj, MUIA_Parent, &obj))
    {
        if (!obj)
            break;
        if (obj == wnd)
            break;

        if (_right(child) < _mleft(obj) || _left(child) > _mright(obj)
            || _bottom(child) < _mtop(obj) || _top(child) > _mbottom(obj))
            return FALSE;
    }
    return TRUE;
}

/* Screen metrics stay in points in attributes/preferences. Convert only when
 * calculating layout, so repeated layout and live scale changes cannot compound.
 */
ULONG zune_screen_scale(struct Screen *screen)
{
    IPTR scale = 100;
    if (screen)
        GetAttr(SA_Scale, (Object *)screen, &scale);
    return scale;
}

ULONG zune_legacy_factor(struct Screen *screen)
{
    IPTR factor = 1;
    if (screen) GetAttr(SA_LegacyScaleFactor, (Object *)screen, &factor);
    return factor;
}

LONG zune_scale_value(LONG points, ULONG scale)
{
    if (points == MUI_MAXMAX)
        return points;
    return (points * scale + 50) / 100;
}

LONG zune_scale(Object *obj, LONG points)
{
    struct MUI_RenderInfo *mri = muiRenderInfo(obj);
    return zune_scale_value(points, zune_render_scale(mri));
}

/* One immutable setup snapshot and its nested resources remain borrowed until
 * screen close, including during in-flight drawing and queued notifications. */
const struct ScreenRenderContext *zune_screen_context(struct Screen *screen)
{
    IPTR context = 0;
    if (!screen || !GetAttr(SA_RenderContext, (Object *)screen, &context)) return NULL;
    return (const struct ScreenRenderContext *)context;
}

APTR zune_visualinfo(struct MUI_RenderInfo *mri)
{
    if (!mri || !mri->mri_RenderContext) return NULL;
    struct TagItem tags[] = {{GTVI_RenderContext, (IPTR)mri->mri_RenderContext},
        {GTVI_Logical, !mri->mri_ScaleAware}, {TAG_DONE, 0}};
    return GetVisualInfoA(mri->mri_Screen, tags);
}
