/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright (C) 2003-2020, The AROS Development Team. All rights reserved.
*/

#include <string.h>
#include <exec/types.h>
#include <diskfont/diskfont.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/diskfont.h>
#include <proto/dos.h>
#include <proto/graphics.h>

#include "muimaster_intern.h"
#include "font.h"
#include "prefs.h"
#include "support.h"

/*  #define MYDEBUG 1 */
#include "debug.h"

extern struct Library *MUIMasterBase;

/* Prefer the exact disk/outline size. Bitmap-only fonts use diskfont's
 * managed scaled segment; the returned font is released with CloseFont. */
static struct TextFont *zune_open_scaled_font(struct TextAttr *attr,
    UWORD base_size, ULONG scale)
{
    struct TextFont *font = OpenDiskFont(attr);
    struct TTextAttr base_attr;
    struct TextFont *base;
    struct DiskFontHeader *scaled;
    if (scale == 100 || (font && font->tf_YSize == attr->ta_YSize))
        return font;
    if (font) CloseFont(font);
    base_attr.tta_Name = attr->ta_Name;
    base_attr.tta_YSize = base_size;
    base_attr.tta_Style = attr->ta_Style;
    base_attr.tta_Flags = attr->ta_Flags;
    base_attr.tta_Tags = (attr->ta_Style & FSF_TAGGED)
        ? ((struct TTextAttr *)attr)->tta_Tags : NULL;
    base = OpenDiskFont((struct TextAttr *)&base_attr);
    if (!base) return NULL;
    scaled = (struct DiskFontHeader *)NewScaledDiskFont(base, attr);
    CloseFont(base);
    return scaled ? &scaled->dfh_TF : NULL;
}

/* Screen and system font defaults preserve real tagged attributes. */
static void zune_font_textattr(struct TextFont *font, struct TTextAttr *attr)
{
    attr->tta_Name = font->tf_Message.mn_Node.ln_Name;
    attr->tta_YSize = font->tf_YSize;
    attr->tta_Style = font->tf_Style & ~FSF_TAGGED;
    attr->tta_Flags = font->tf_Flags;
    attr->tta_Tags = NULL;
    if ((font->tf_Style & FSF_TAGGED) && font->tf_Extension)
    {
        attr->tta_Tags = ((struct TextFontExtension *)font->tf_Extension)->tfe_Tags;
        if (attr->tta_Tags) attr->tta_Style |= FSF_TAGGED;
    }
}

/* Returns a given text font, if necessary it opens the font.
 * Must be called after Area's MUIM_Setup.
 */

struct TextFont *zune_font_get(Object *obj, IPTR font)
{
    struct MUI_GlobalInfo *mgi;
    struct MUI_RenderInfo *mri;
    SIPTR preset = (SIPTR) font;

    if ((preset <= (SIPTR) MUIV_Font_Inherit)
        && (preset >= (SIPTR) MUIV_Font_NegCount))
    {
        CONST_STRPTR name;

        mri = muiRenderInfo(obj);
        /* font already loaded, just return it */
        if (mri->mri_Fonts[-preset])
        {
            D(bug("zune_font_get : return mri_Fonts[%d]=0x%p\n",
                    preset, mri->mri_Fonts[-preset]));
            return mri->mri_Fonts[-preset];
        }

        mgi = muiGlobalInfo(obj);
        /* font name given, load it */
        name = mgi->mgi_Prefs->fonts[-preset];
        D(bug("zune_font_get : preset=%d, name=%s\n", preset, name));
        if (name != NULL && name[0] != 0)
        {
            struct TextAttr ta;
            if ((ta.ta_Name = (char *)AllocVec(strlen(name) + 10, 0)))
            {
                char *p;
                LONG size;

                strcpy(ta.ta_Name, name);
                StrToLong(FilePart(ta.ta_Name), &size);
                ta.ta_YSize = zune_scale(obj, size);
                ta.ta_Style = 0;
                ta.ta_Flags = 0;

                if ((p = PathPart(ta.ta_Name)))
                    strcpy(p, ".font");
                D(bug("zune_font_get : OpenDiskFont(%s)\n", ta.ta_Name));
                mri->mri_Fonts[-preset] = zune_open_scaled_font(&ta, size,
                    zune_render_scale(mri));

                FreeVec(ta.ta_Name);
            }

        }
        else                    /* fallback to window normal font */
        {
            /* avoid infinite recursion */
            if (preset != (SIPTR) MUIV_Font_Normal && preset
                != (SIPTR) MUIV_Font_Fixed)
            {
                /* don't do this, would result in the font being closed more than once */
/*              return (mri->mri_Fonts[-preset] = zune_font_get(obj, MUIV_Font_Normal)); */
                return zune_font_get(obj, MUIV_Font_Normal);
            }
        }

        /* no font loaded, fallback to screen font or system font */
        if (!mri->mri_Fonts[-preset])
        {
            if ((SIPTR) MUIV_Font_Normal == preset)
            {
                struct TTextAttr scr_attr = mri->mri_RenderContext->src_BaseFont;
                UWORD base_size = scr_attr.tta_YSize;
                /* The base attributes and render percentage belong to the same
                 * immutable generation; scale its base exactly once. */
                scr_attr.tta_YSize = zune_scale(obj, base_size);
                scr_attr.tta_Flags = 0;
                D(bug("zune_font_get : OpenDiskFont(%s) (screen font)\n",
                        scr_attr.tta_Name));
                mri->mri_Fonts[-preset] = zune_open_scaled_font((struct TextAttr *)&scr_attr,
                    base_size, zune_render_scale(mri));
            }
            else                /* MUIV_Font_Fixed */
            {
                struct TTextAttr def_attr;
                zune_font_textattr(GfxBase->DefaultFont, &def_attr);
                def_attr.tta_YSize = zune_scale(obj, GfxBase->DefaultFont->tf_YSize);
                D(bug("zune_font_get : OpenDiskFont(%s) (system font)\n",
                        def_attr.tta_Name));
                mri->mri_Fonts[-preset] = zune_open_scaled_font((struct TextAttr *)&def_attr,
                    GfxBase->DefaultFont->tf_YSize, zune_render_scale(mri));
            }
        }
        return mri->mri_Fonts[-preset];
    }
    return (struct TextFont *)font;
}
