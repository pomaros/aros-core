/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <exec/memory.h>
#include <string.h>
#include "mui.h"
#include "muimaster_intern.h"
#include "support.h"

/* Qualification lives in the exact Class allocation, so FreeClass and address
 * reuse cannot leave a stale registry entry. MakeClass clears these bits even
 * if a caller copies its superclass flags. No class-name inference is used. */
BOOL zune_set_class_capabilities(Class *cl, ULONG caps, struct Library *base)
{
    struct MUIMasterBase_intern *data = MUIMB(base);
    BOOL result = FALSE;
    if (!cl || (caps & ~MUI_CLASS_SCALE_AWARE)) return FALSE;
    ObtainSemaphore(&data->ZuneSemaphore);
    if (cl->cl_Flags & CLF_SCALE_DECLARED)
        result = !!(cl->cl_Flags & CLF_SCALE_AWARE) == !!(caps & MUI_CLASS_SCALE_AWARE);
    else if (!cl->cl_ObjectCount && !cl->cl_SubclassCount)
    {
        cl->cl_Flags |= CLF_SCALE_DECLARED;
        if (caps & MUI_CLASS_SCALE_AWARE) cl->cl_Flags |= CLF_SCALE_AWARE;
        result = TRUE;
    }
    ReleaseSemaphore(&data->ZuneSemaphore);
    return result;
}
BOOL zune_class_scale_aware(Class *cl, struct Library *base)
{
    struct MUIMasterBase_intern *data = MUIMB(base);
    BOOL result = TRUE;
    if (!cl) return FALSE;
    ObtainSemaphore(&data->ZuneSemaphore);
    while (cl && cl->cl_Super)
    {
        if ((cl->cl_Flags & (CLF_SCALE_AWARE | CLF_SCALE_DECLARED)) !=
            (CLF_SCALE_AWARE | CLF_SCALE_DECLARED)) { result = FALSE; break; }
        cl = cl->cl_Super;
    }
    ReleaseSemaphore(&data->ZuneSemaphore);
    return result;
}
BOOL zune_object_scale_aware(Object *obj, struct Library *base)
{
    struct MinList *children;
    Object *state, *child;
    if (!obj || !zune_class_scale_aware(OCLASS(obj), base)) return FALSE;
    if (!DoMethod(obj, MUIM_ScaleContextSupported)) return FALSE;
    /* Typed framework ownership, never similarly named arbitrary data lists. */
    children = (struct MinList *)DoMethod(obj, MUIM_ScaleContextChildren);
    if (children)
    {
        state = (Object *)children->mlh_Head;
        while ((child = NextObject(&state)))
            if (!zune_object_scale_aware(child, base)) return FALSE;
    }
    return TRUE;
}
ULONG zune_render_scale(const struct MUI_RenderInfo *mri)
{
    return mri && mri->mri_RenderScale ? mri->mri_RenderScale : 100;
}

/* Inline objects/alpha arrays have an external pixel contract. Ordinary text
 * styles remain font-driven; unknown embedded resources need explicit opt-in. */
BOOL zune_text_scale_supported(CONST_STRPTR text)
{
    const unsigned char *p = (const unsigned char *)text;
    if (!p) return TRUE;
    while (*p)
    {
        if (*p++ == 27 && (*p == 'I' || *p == 'O' || *p == 'A')) return FALSE;
    }
    return TRUE;
}
