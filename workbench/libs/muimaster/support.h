/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright (C) 2002-2013, The AROS Development Team. All rights reserved.
*/

#ifndef _MUIMASTER_SUPPORT_H
#define _MUIMASTER_SUPPORT_H

#ifndef EXEC_TYPES_H
#   include <exec/types.h>
#endif
#ifndef INTUITION_CLASSUSR_H
#   include <intuition/classusr.h>
#endif
#ifndef INTUITION_CLASSES_H
#   include <intuition/classes.h>
#endif
#ifndef CLIB_MACROS_H
#   include <clib/macros.h>
#endif

#ifdef __AROS__
#   ifndef AROS_ASMCALL_H
#       include <aros/asmcall.h>
#   endif
#   include <aros/macros.h>
#   define IMSPEC_EXTERNAL_PREFIX "IMAGES:Zune/"
#   include "support_aros.h"
#else
#   include "support_amigaos.h"
#endif

struct MUI_RenderInfo;
struct IntuiMessage;
struct Region;
struct Rectangle;
struct Object;
struct Library;

#define mui_alloc(x) AllocVec(x,MEMF_CLEAR)
#define mui_alloc_struct(x) ((x *)AllocVec(sizeof(x),MEMF_CLEAR))
#define mui_free(x) FreeVec(x)


int isRegionWithinBounds(struct Region *r, int left, int top, int width,
    int height);
ULONG ConvertKey(struct IntuiMessage *imsg);

#define _between(a,x,b) ((x)>=(a) && (x)<=(b))
#define _isinobject(obj, x, y) (_between(_mleft(obj), (x), _mright(obj)) \
                          && _between(_mtop(obj), (y), _mbottom(obj)))

/* add mask in flags if tag is true, else sub mask */
#define _handle_bool_tag(flags, tag, mask) \
((tag != 0) ? ((flags) |= (mask)) : ((flags) &= ~(mask)))

#define CLAMP(x, low, high) \
    (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

#ifndef __GNUC__
IPTR XGET(Object * obj, Tag attr);
#endif

const struct ScreenRenderContext *zune_screen_context(struct Screen *);
APTR zune_visualinfo(struct MUI_RenderInfo *);

BOOL zune_set_class_capabilities(Class *cl, ULONG caps, struct Library *base);
BOOL zune_class_scale_aware(Class *cl, struct Library *base);
BOOL zune_object_scale_aware(Object *obj, struct Library *base);
BOOL zune_text_scale_supported(CONST_STRPTR text);
ULONG zune_render_scale(const struct MUI_RenderInfo *mri);
ULONG zune_screen_scale(struct Screen *screen);
ULONG zune_legacy_factor(struct Screen *screen);
LONG zune_scale(Object *obj, LONG points);
LONG zune_scale_value(LONG points, ULONG scale);

IPTR DoSetupMethod(Object * obj, struct MUI_RenderInfo *info);
IPTR DoShowMethod(Object * obj);
IPTR DoHideMethod(Object * obj);

/* returns next node of this node */
void *Node_Next(APTR node);
/* returns first node of this list */
void *List_First(APTR list);

WORD SubtractRectFromRect(struct Rectangle *a, struct Rectangle *b,
    struct Rectangle *destrectarray);
ULONG IsObjectVisible(Object * child, struct Library *MUIMasterBase);

#endif /* _MUIMASTER_SUPPORT_H */
