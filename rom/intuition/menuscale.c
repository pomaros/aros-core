/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#include "intuition_intern.h"
#include "intuition_customize.h"
#include "menuscale.h"
#include "rendercontextbinding.h"
#include <exec/memory.h>
#include <intuition/imageclass.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <intuition/menudecorclass.h>
#include <string.h>
#include <stddef.h>

/* These limits also terminate cyclic client links without unbounded work. */
#define MENU_SCALE_MAX_NODES 4096
struct MenuScaleMenu
{
    struct MenuScaleMenu *next;
    struct Menu *original;
    struct Menu copy;
};
struct MenuScaleItem
{
    struct MenuScaleItem *next;
    struct MenuItem *original;
    struct MenuItem copy;
    BOOL separator;
    LONG projectedWidth, projectedHeight;
};
struct MenuScaleText
{
    struct MenuScaleText *next;
    struct IntuiText copy;
};
struct MenuScaleGeneration
{
    struct MenuScaleView view;
    struct MenuScaleMenu *menus;
    struct MenuScaleItem *items;
    struct MenuScaleText *texts;
    struct TTextAttr font;
    struct RastPort measure;
    struct IClass *separatorClass;
    ULONG sourceScale, nodes;
    BOOL measureReady;
    struct IntuitionBase *intuitionBase;
};

static BOOL menu_scale_value(LONG value, ULONG scale, ULONG reference, LONG *out)
{
    QUAD product = (QUAD)value * scale;
    QUAD rounded;
    if (!reference || scale < 100 || scale > 400 || reference < 100 || reference > 400 ||
        scale % 25 || reference % 25)
        return FALSE;
    rounded = product < 0 ? -((-product + reference / 2) / reference) :
        (product + reference / 2) / reference;
    if (rounded < -32768 || rounded > 32767) return FALSE;
    *out = rounded;
    return TRUE;
}
LONG MenuScalePoint(const struct MenuScaleView *view, LONG points)
{
    QUAD product = (QUAD)points * view->scale;
    return product < 0 ? -(LONG)((-product + 50) / 100) : (LONG)((product + 50) / 100);
}
LONG MenuScaleMouse(const struct MenuScaleView *view, LONG physical)
{
    LONG factor = view->factor;
    return physical < 0 ? -((-(QUAD)physical + factor - 1) / factor) : physical / factor;
}
struct MenuScaleSnapshot
{
    struct Screen *screen;
    const struct ScreenRenderContext *source, *target, *bound;
    struct Menu *menu;
    Object *provider;
    struct Image *checkmark, *amigakey, *submenu;
    LONG width, height;
    BOOL logical;
};

/* The input task's MenuLock lifetime protocol pins client nodes/Window while
 * a menu is active. Scale publication uses IBase and the screen LayerInfo,
 * independently of MenuLock. Copy every mutable input in that same interval;
 * all fallible clone/layout/provider work runs after both locks are released. */
static BOOL menu_scale_snapshot(struct Window *window, struct MenuScaleSnapshot *snapshot,
    struct IntuitionBase *IntuitionBase)
{
    struct LayersBase *LayersBase = GetPrivIBase(IntuitionBase)->LayersBase;
    struct Screen *screen;
    ULONG lock;
    if (!window || !snapshot) return FALSE;
    lock = LockIBase(0);
    screen = window->WScreen;
    if (!screen) { UnlockIBase(lock); return FALSE; }
    LockLayerInfo(&screen->LayerInfo);
    snapshot->screen = screen;
    snapshot->source = IW(window)->MenuRenderContext;
    snapshot->target = CurrentScreenRenderContext(screen);
    snapshot->bound = WindowBoundRenderContext(window);
    snapshot->menu = window->MenuStrip;
    snapshot->logical = IW(window)->MenuRenderLogical;
    snapshot->checkmark = IW(window)->Checkmark;
    snapshot->amigakey = IW(window)->AmigaKey;
    snapshot->submenu = IW(window)->SubMenuImage;
    snapshot->provider = IS(screen)->MenuDecorObj;
    snapshot->width = screen->Width;
    snapshot->height = screen->Height;
    UnlockLayerInfo(&screen->LayerInfo);
    UnlockIBase(lock);
    return TRUE;
}

const struct ScreenRenderContext *CaptureMenuScaleContext(struct Window *window, struct IntuitionBase *IntuitionBase)
{
    struct MenuScaleSnapshot snapshot;
    if (!menu_scale_snapshot(window, &snapshot, IntuitionBase)) return NULL;
    /* Bound menus use the same generation as their caller's VisualInfo. */
    return snapshot.bound ? snapshot.bound : snapshot.target;
}
static BOOL menu_scale_tags(struct TagItem *left, struct TagItem *right,
    struct IntuitionBase *IntuitionBase)
{
    struct Library *UtilityBase = GetPrivIBase(IntuitionBase)->UtilityBase;
    ULONG count;
    for (count = 0; count < 128; ++count)
    {
        struct TagItem *a = NextTagItem(&left), *b = NextTagItem(&right);
        if (!a || !b) return a == b;
        if (a->ti_Tag != b->ti_Tag || a->ti_Data != b->ti_Data) return FALSE;
    }
    return FALSE;
}
static BOOL menu_scale_font(const struct TextAttr *attr, struct TextFont *font,
    struct IntuitionBase *IntuitionBase)
{
    if (!attr) return TRUE;
    if (!font || !attr->ta_Name || !font->tf_Message.mn_Node.ln_Name ||
        strcmp((const char *)attr->ta_Name,
            (const char *)font->tf_Message.mn_Node.ln_Name) ||
        attr->ta_YSize != font->tf_YSize ||
        (attr->ta_Style & ~FSF_TAGGED) != (font->tf_Style & ~FSF_TAGGED) ||
        (attr->ta_Flags & (FPF_REVPATH | FPF_TALLDOT | FPF_WIDEDOT)) !=
            (font->tf_Flags & (FPF_REVPATH | FPF_TALLDOT | FPF_WIDEDOT))) return FALSE;
    /* Compare the complete retained tag values, including a GadTools-owned
     * CloneTagItems copy, instead of guessing DPI from size or pointer identity. */
    if (attr->ta_Style & FSF_TAGGED)
    {
        if (!(font->tf_Style & FSF_TAGGED) || !font->tf_Extension) return FALSE;
        return menu_scale_tags(((const struct TTextAttr *)attr)->tta_Tags,
            ((struct TextFontExtension *)font->tf_Extension)->tfe_Tags, IntuitionBase);
    }
    return TRUE;
}
static BOOL menu_scale_texts(struct IntuiText *text, struct TextFont *font, ULONG *count,
    struct IntuitionBase *IntuitionBase)
{
    for (; text; text = text->NextText)
    {
        if (++*count > MENU_SCALE_MAX_NODES || text->LeftEdge < 0 || text->TopEdge < 0 ||
            !menu_scale_font(text->ITextFont, font, IntuitionBase))
            return FALSE;
    }
    return TRUE;
}
static BOOL menu_scale_separator(struct MenuScaleGeneration *generation, struct MenuItem *item)
{
    struct Image *image = item->ItemFill;
    return !(item->Flags & ITEMTEXT) && image && !image->NextImage && image->Depth == CUSTOMIMAGEDEPTH &&
        OCLASS((Object *)image) == generation->separatorClass;
}
static BOOL menu_scale_qualified(struct MenuScaleGeneration *generation,
    struct MenuItem *items, struct TextFont *font, ULONG *count, BOOL subitems)
{
    struct MenuItem *item, *other;
    for (item = items; item; item = item->NextItem)
    {
        if (++*count > MENU_SCALE_MAX_NODES) return FALSE;
        if (item->Flags & ITEMTEXT)
        {
            if (!menu_scale_texts(item->ItemFill, font, count, generation->intuitionBase) ||
                ((item->Flags & HIGHFLAGS) == HIGHIMAGE &&
                    !menu_scale_texts(item->SelectFill, font, count, generation->intuitionBase))) return FALSE;
        }
        else if ((item->ItemFill && !menu_scale_separator(generation, item)) ||
            ((item->Flags & HIGHFLAGS) == HIGHIMAGE && item->SelectFill))
            return FALSE;
        if (item->SubItem && (subitems ||
            !menu_scale_qualified(generation, item->SubItem, font, count, TRUE))) return FALSE;
        /* Unknown overlapping item layouts are not silently rearranged. */
        ULONG checked = 0;
        for (other = item->NextItem; other; other = other->NextItem)
        {
            if (++checked > MENU_SCALE_MAX_NODES) return FALSE;
            if (item->Width > 0 && item->Height > 0 && other->Width > 0 && other->Height > 0 &&
                item->LeftEdge < (LONG)other->LeftEdge + other->Width &&
                other->LeftEdge < (LONG)item->LeftEdge + item->Width &&
                item->TopEdge < (LONG)other->TopEdge + other->Height &&
                other->TopEdge < (LONG)item->TopEdge + item->Height) return FALSE;
        }
    }
    return TRUE;
}
static void *menu_scale_allocate(struct MenuScaleGeneration *generation, ULONG size)
{
    if (++generation->nodes > MENU_SCALE_MAX_NODES) return NULL;
    return AllocVec(size, MEMF_PUBLIC | MEMF_CLEAR);
}
static BOOL menu_scale_copy_text(struct MenuScaleGeneration *generation,
    struct IntuiText *original, APTR *result)
{
    struct IntuiText **tail = (struct IntuiText **)result;
    for (; original; original = original->NextText)
    {
        struct MenuScaleText *node = menu_scale_allocate(generation, sizeof(*node));
        LONG left, top;
        if (!node) return FALSE;
        node->next = generation->texts; generation->texts = node;
        node->copy = *original; node->copy.NextText = NULL;
        if (generation->view.sharp)
        {
            if (!menu_scale_value(original->LeftEdge, generation->view.scale,
                    generation->sourceScale, &left) ||
                !menu_scale_value(original->TopEdge, generation->view.scale,
                    generation->sourceScale, &top)) return FALSE;
            node->copy.LeftEdge = left; node->copy.TopEdge = top;
            node->copy.ITextFont = (struct TextAttr *)&generation->font;
        }
        *tail = &node->copy; tail = &node->copy.NextText;
    }
    *tail = NULL;
    return TRUE;
}
static BOOL menu_scale_text_extent(struct MenuScaleGeneration *generation,
    struct IntuiText *text, LONG *width, LONG *height)
{
    struct GfxBase *GfxBase = GetPrivIBase(generation->intuitionBase)->GfxBase;
    for (; text; text = text->NextText)
    {
        LONG pixels;
        size_t length = text->IText ? strlen((const char *)text->IText) : 0;
        if (length > 32767) return FALSE;
        pixels = TextLength(&generation->measure, text->IText, length);
        if (pixels < 0 || (QUAD)text->LeftEdge + pixels > 32767 ||
            (QUAD)text->TopEdge + generation->view.dri->dri_Font->tf_YSize > 32767)
            return FALSE;
        if (text->LeftEdge + pixels > *width) *width = text->LeftEdge + pixels;
        if (text->TopEdge + generation->view.dri->dri_Font->tf_YSize > *height)
            *height = text->TopEdge + generation->view.dri->dri_Font->tf_YSize;
    }
    return TRUE;
}
/* Expand rows and columns from measured target text, retaining source gaps.
 * Menu numbers and linked-list order do not change. */
static BOOL menu_scale_reflow(struct MenuItem *items, BOOL vertical)
{
    LONG last = -32769, shift = 0;
    for (;;)
    {
        struct MenuItem *item;
        LONG coordinate = 32768, growth = 0;
        for (item = items; item; item = item->NextItem)
        {
            struct MenuScaleItem *node = (struct MenuScaleItem *)((UBYTE *)item -
                offsetof(struct MenuScaleItem, copy));
            LONG position = vertical ? node->original->TopEdge : node->original->LeftEdge;
            if (position > last && position < coordinate) coordinate = position;
        }
        if (coordinate == 32768) break;
        for (item = items; item; item = item->NextItem)
        {
            struct MenuScaleItem *node = (struct MenuScaleItem *)((UBYTE *)item -
                offsetof(struct MenuScaleItem, copy));
            LONG position = vertical ? node->original->TopEdge : node->original->LeftEdge;
            if (position == coordinate)
            {
                LONG transformed = vertical ? item->TopEdge : item->LeftEdge;
                if ((QUAD)transformed + shift > 32767) return FALSE;
                if (vertical) item->TopEdge = transformed + shift;
                else item->LeftEdge = transformed + shift;
                LONG amount = vertical ? item->Height : item->Width;
                LONG projected = vertical ? node->projectedHeight : node->projectedWidth;
                if (amount - projected > growth) growth = amount - projected;
            }
        }
        shift += growth; last = coordinate;
    }
    return TRUE;
}
static BOOL menu_scale_copy_items(struct MenuScaleGeneration *generation,
    struct MenuItem *original, struct MenuItem **result, BOOL subitems)
{
    struct MenuItem **tail = result;
    for (; original; original = original->NextItem)
    {
        struct MenuScaleItem *node = menu_scale_allocate(generation, sizeof(*node));
        LONG left, top, width, height;
        if (!node) return FALSE;
        node->next = generation->items; generation->items = node;
        node->original = original; node->copy = *original;
        node->copy.NextItem = node->copy.SubItem = NULL;
        node->separator = menu_scale_separator(generation, original);
        *tail = &node->copy; tail = &node->copy.NextItem;
        if (generation->view.sharp)
        {
            if (!menu_scale_value(original->LeftEdge, generation->view.scale,
                    generation->sourceScale, &left) ||
                !menu_scale_value(original->TopEdge, generation->view.scale,
                    generation->sourceScale, &top) ||
                !menu_scale_value(original->Width, generation->view.scale,
                    generation->sourceScale, &width) ||
                !menu_scale_value(original->Height, generation->view.scale,
                    generation->sourceScale, &height) || width <= 0 || height <= 0)
                return FALSE;
            node->copy.LeftEdge = left; node->copy.TopEdge = top;
            node->copy.Width = width; node->copy.Height = height;
        }
        node->projectedWidth = node->copy.Width;
        node->projectedHeight = node->copy.Height;
        if (original->Flags & ITEMTEXT)
        {
            node->copy.ItemFill = node->copy.SelectFill = NULL;
            if (!menu_scale_copy_text(generation, original->ItemFill, &node->copy.ItemFill) ||
                ((original->Flags & HIGHFLAGS) == HIGHIMAGE &&
                    !menu_scale_copy_text(generation, original->SelectFill, &node->copy.SelectFill)))
                return FALSE;
            if (generation->view.sharp)
            {
                LONG measuredwidth = 0, measuredheight = 0;
                LONG reserve = MenuScalePoint(&generation->view, 2);
                if (original->Flags & CHECKIT)
                {
                    struct IntuiText *text;
                    LONG minimum = generation->view.checkmark->Width + reserve;
                    for (text = node->copy.ItemFill; text; text = text->NextText)
                        if (text->LeftEdge < minimum) text->LeftEdge = minimum;
                    for (text = node->copy.SelectFill; text; text = text->NextText)
                        if (text->LeftEdge < minimum) text->LeftEdge = minimum;
                }
                if (!menu_scale_text_extent(generation, node->copy.ItemFill,
                        &measuredwidth, &measuredheight) ||
                    !menu_scale_text_extent(generation, node->copy.SelectFill,
                        &measuredwidth, &measuredheight)) return FALSE;
                if (original->Flags & COMMSEQ)
                    reserve += generation->view.amigakey->Width +
                        generation->view.dri->dri_Font->tf_XSize +
                        MenuScalePoint(&generation->view, 6);
                if (original->SubItem) reserve += generation->view.submenuimage->Width;
                if ((QUAD)measuredwidth + reserve > 32767 ||
                    (QUAD)measuredheight + MenuScalePoint(&generation->view, 2) > 32767)
                    return FALSE;
                measuredwidth += reserve;
                measuredheight += MenuScalePoint(&generation->view, 2);
                if (measuredwidth > node->copy.Width) node->copy.Width = measuredwidth;
                if (measuredheight > node->copy.Height) node->copy.Height = measuredheight;
            }
        }
        if (original->SubItem)
        {
            if (subitems || !menu_scale_copy_items(generation, original->SubItem,
                    &node->copy.SubItem, TRUE)) return FALSE;
        }
    }
    *tail = NULL;
    if (generation->view.sharp &&
        (!menu_scale_reflow(*result, TRUE) || !menu_scale_reflow(*result, FALSE))) return FALSE;
    return TRUE;
}
static BOOL menu_scale_box(struct MenuItem *items, struct Menu *menu,
    const struct MenuScaleView *view)
{
    LONG left = 32767, top = 32767, right = -32768, bottom = -32768;
    if (!items) { menu->JazzX = menu->JazzY = menu->BeatX = menu->BeatY = 0; return TRUE; }
    for (; items; items = items->NextItem)
    {
        LONG r = (LONG)items->LeftEdge + items->Width - 1;
        LONG b = (LONG)items->TopEdge + items->Height - 1;
        if (items->Width <= 0 || items->Height <= 0 || r > 32767 || b > 32767) return FALSE;
        if (items->LeftEdge < left) left = items->LeftEdge;
        if (items->TopEdge < top) top = items->TopEdge;
        if (r > right) right = r;
        if (b > bottom) bottom = b;
    }
    left -= view->menuhborder; top -= view->menuvborder;
    right += view->menuhborder; bottom += view->menuvborder;
    if (right < left || bottom < top || left < -32768 || top < -32768 ||
        right > 32767 || bottom > 32767) return FALSE;
    menu->JazzX = left; menu->JazzY = top; menu->BeatX = right; menu->BeatY = bottom;
    return TRUE;
}
struct MenuScaleGeneration *PrepareMenuScale(struct Window *window,
    struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct MenuScaleSnapshot snapshot;
    const struct ScreenRenderContext *source, *context;
    struct MenuScaleGeneration *generation;
    struct Menu *menu, **tail;
    struct DrawInfo *sourceinfo;
    ULONG count = 0;
    BOOL sharp = TRUE;
    if (!menu_scale_snapshot(window, &snapshot, IntuitionBase)) return NULL;
    source = snapshot.source;
    context = snapshot.target;
    if (!source || !context || !snapshot.menu ||
        !source->src_LogicalDrawInfo || !source->src_PhysicalDrawInfo ||
        !context->src_LogicalDrawInfo || !context->src_PhysicalDrawInfo ||
        context->src_Scale < 100 || context->src_Scale > 400 || context->src_Scale % 25 ||
        context->src_LegacyScaleFactor < 1 || context->src_LegacyScaleFactor > 4)
        return NULL;
    generation = AllocVec(sizeof(*generation), MEMF_PUBLIC | MEMF_CLEAR);
    if (!generation) return NULL;
    generation->intuitionBase = IntuitionBase;
    generation->separatorClass = FindClass(MENUBARLABELCLASS);
    generation->sourceScale = snapshot.logical ? 100 : source->src_Scale;
    sourceinfo = snapshot.logical ? source->src_LogicalDrawInfo : source->src_PhysicalDrawInfo;
    {
        struct mdpQueryCapabilities capability = {MDM_QUERYCAPABILITIES};
        generation->view.typedDecorator = (DoMethodA(snapshot.provider,
            (Msg)&capability) & MDM_CAP_SCALE) != 0;
        if (!generation->view.typedDecorator) sharp = FALSE;
    }
    for (menu = snapshot.menu; menu; menu = menu->NextMenu)
    {
        if (++count > MENU_SCALE_MAX_NODES) goto fail;
        if (!menu_scale_qualified(generation, menu->FirstItem, sourceinfo->dri_Font,
                &count, FALSE)) sharp = FALSE;
    }
    if (!sourceinfo->dri_Font || !context->src_PhysicalDrawInfo->dri_Font ||
        !((struct IntDrawInfo *)sourceinfo)->dri_Customize ||
        !((struct IntDrawInfo *)context->src_PhysicalDrawInfo)->dri_Customize) goto fail;
    if ((snapshot.checkmark != sourceinfo->dri_CheckMark &&
            snapshot.checkmark != context->src_PhysicalDrawInfo->dri_CheckMark) ||
        (snapshot.amigakey != sourceinfo->dri_AmigaKey &&
            snapshot.amigakey != context->src_PhysicalDrawInfo->dri_AmigaKey) ||
        (snapshot.submenu != ((struct IntDrawInfo *)sourceinfo)->dri_Customize->submenu &&
            snapshot.submenu != ((struct IntDrawInfo *)context->src_PhysicalDrawInfo)->dri_Customize->submenu))
        sharp = FALSE;
    /* An explicit aware client owns its custom physical resources. A stale
     * installation cannot be silently reinterpreted in another generation. */
    if (!sharp && !snapshot.logical && source != context)
    {
        bug("[MENU-SCALE] result=REFUSED reason=stale-aware-custom\n");
        goto fail;
    }
    generation->view.context = context;
    generation->view.sharp = sharp;
    generation->view.logical = !sharp && snapshot.logical;
    generation->view.factor = !sharp && snapshot.logical ?
        context->src_LegacyScaleFactor : 1;
    generation->view.scale = sharp || !snapshot.logical ? context->src_Scale : 100;
    generation->view.dri = sharp ? context->src_PhysicalDrawInfo : sourceinfo;
    generation->view.checkmark = sharp ? generation->view.dri->dri_CheckMark : snapshot.checkmark;
    generation->view.amigakey = sharp ? generation->view.dri->dri_AmigaKey : snapshot.amigakey;
    generation->view.submenuimage = sharp ? ((struct IntDrawInfo *)generation->view.dri)->dri_Customize->submenu :
        snapshot.submenu;
    if (!generation->view.checkmark || !generation->view.amigakey || !generation->view.submenuimage)
        goto fail;
    generation->view.width = snapshot.width / generation->view.factor;
    generation->view.height = snapshot.height / generation->view.factor;
    generation->view.barheight = (context->src_PhysicalBarHeight +
        generation->view.factor - 1) / generation->view.factor - 1;
    if (!sharp && snapshot.logical)
    {
        generation->view.barhborder = context->src_LogicalBarHBorder;
        generation->view.barvborder = context->src_LogicalBarVBorder;
        generation->view.menuhborder = context->src_LogicalMenuHBorder;
        generation->view.menuvborder = context->src_LogicalMenuVBorder;
    }
    else
    {
        generation->view.barhborder = context->src_PhysicalBarHBorder;
        generation->view.barvborder = context->src_PhysicalBarVBorder;
        generation->view.menuhborder = context->src_PhysicalMenuHBorder;
        generation->view.menuvborder = context->src_PhysicalMenuVBorder;
    }
    /* Fractional physical bar rounding must also fit both logical padding edges. */
    if (generation->view.logical)
    {
        LONG minimum = generation->view.dri->dri_Font->tf_YSize +
            2 * (LONG)generation->view.barvborder;
        if (generation->view.barheight + 1 < minimum)
            generation->view.barheight = minimum - 1;
    }
    ScreenFontTextAttr(generation->view.dri->dri_Font, &generation->font);
    InitRastPort(&generation->measure);
    generation->measureReady = TRUE;
    SetFont(&generation->measure, generation->view.dri->dri_Font);
    tail = &generation->view.menu;
    for (menu = snapshot.menu; menu; menu = menu->NextMenu)
    {
        struct MenuScaleMenu *node = menu_scale_allocate(generation, sizeof(*node));
        LONG left, top, width, height;
        if (!node) goto fail;
        node->next = generation->menus; generation->menus = node;
        node->original = menu; node->copy = *menu;
        node->copy.NextMenu = NULL; node->copy.FirstItem = NULL;
        *tail = &node->copy; tail = &node->copy.NextMenu;
        if (sharp)
        {
            LONG textwidth;
            size_t length = menu->MenuName ? strlen((const char *)menu->MenuName) : 0;
            if (length > 32767 ||
                !menu_scale_value(menu->LeftEdge, generation->view.scale,
                    generation->sourceScale, &left) ||
                !menu_scale_value(menu->TopEdge, generation->view.scale,
                    generation->sourceScale, &top) ||
                !menu_scale_value(menu->Width, generation->view.scale,
                    generation->sourceScale, &width) ||
                !menu_scale_value(menu->Height, generation->view.scale,
                    generation->sourceScale, &height)) goto fail;
            textwidth = TextLength(&generation->measure, menu->MenuName, length);
            if (textwidth < 0 || textwidth > 32767 - MenuScalePoint(&generation->view, 4)) goto fail;
            if (width < textwidth + MenuScalePoint(&generation->view, 4))
                width = textwidth + MenuScalePoint(&generation->view, 4);
            node->copy.LeftEdge = left; node->copy.TopEdge = top;
            node->copy.Width = width; node->copy.Height = height;
        }
        if (!menu_scale_copy_items(generation, menu->FirstItem, &node->copy.FirstItem, FALSE) ||
            !menu_scale_box(node->copy.FirstItem, &node->copy, &generation->view)) goto fail;
    }
    if (sharp)
    {
        /* A changed typeface must not make adjacent title rectangles overlap.
         * Client title rectangles and source list order remain untouched. */
        struct Menu *previous = NULL;
        for (menu = generation->view.menu; menu; menu = menu->NextMenu)
        {
            LONG left = menu->LeftEdge;
            if (previous && left < (LONG)previous->LeftEdge + previous->Width)
                left = (LONG)previous->LeftEdge + previous->Width;
            if (left > 32767 || left + menu->Width > 32767) goto fail;
            menu->LeftEdge = left;
            previous = menu;
        }
    }
    return generation;
fail:
    DisposeMenuScale(generation);
    return NULL;
}
const struct MenuScaleView *MenuScaleView(struct MenuScaleGeneration *generation)
{
    return generation ? &generation->view : NULL;
}
void CommitMenuScaleState(struct MenuScaleGeneration *generation)
{
    struct MenuScaleItem *item;
    if (!generation) return;
    for (item = generation->items; item; item = item->next)
    {
        item->original->Flags = (item->original->Flags & ~CHECKED) | (item->copy.Flags & CHECKED);
        item->original->NextSelect = item->copy.NextSelect;
    }
}
void DisposeMenuScale(struct MenuScaleGeneration *generation)
{
    if (!generation) return;
    if (generation->measureReady) DeinitRastPort(&generation->measure);
    while (generation->texts)
    {
        struct MenuScaleText *node = generation->texts;
        generation->texts = node->next; FreeVec(node);
    }
    while (generation->items)
    {
        struct MenuScaleItem *node = generation->items;
        generation->items = node->next; FreeVec(node);
    }
    while (generation->menus)
    {
        struct MenuScaleMenu *node = generation->menus;
        generation->menus = node->next; FreeVec(node);
    }
    FreeVec(generation);
}
BOOL MenuScaleDrawSeparator(struct MenuScaleGeneration *generation, struct MenuItem *item,
    struct RastPort *rp, LONG x, LONG y, struct IntuitionBase *IntuitionBase)
{
    struct GfxBase *GfxBase = GetPrivIBase(IntuitionBase)->GfxBase;
    struct MenuScaleItem *node;
    LONG stroke, left, top, width;
    if (!generation || !generation->view.sharp) return FALSE;
    for (node = generation->items; node && &node->copy != item; node = node->next) ;
    if (!node || !node->separator) return FALSE;
    stroke = MenuScalePoint(&generation->view, 1);
    left = x + MenuScalePoint(&generation->view, 2);
    top = y + (item->Height - stroke * 2) / 2;
    width = item->Width - MenuScalePoint(&generation->view, 4);
    if (width <= 0 || item->Height < stroke * 2) return TRUE;
    SetAPen(rp, generation->view.dri->dri_Pens[SHADOWPEN]);
    RectFill(rp, left, top, left + width - 1, top + stroke - 1);
    SetAPen(rp, generation->view.dri->dri_Pens[SHINEPEN]);
    RectFill(rp, left, top + stroke, left + width - 1, top + stroke * 2 - 1);
    return TRUE;
}
