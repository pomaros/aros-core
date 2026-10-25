/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: FontDiag - which fonts the desktop really uses: the system default
          font, the default screen font and the Workbench screen's font, then
          whether OpenDiskFont() opens each font ENV:Sys/font.prefs names.

    One line per finding:

      [fontdiag] system=topaz.font/8 screen-default=... workbench=...
      [fontdiag] prefs type=1 ttcourier.font/14 open=yes got=14 proportional=no
*/

#include <exec/types.h>
#include <dos/dos.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <intuition/screens.h>
#include <intuition/intuitionbase.h>
#include <libraries/iffparse.h>
#include <prefs/prefhdr.h>
#include <prefs/font.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/diskfont.h>
#include <proto/iffparse.h>
#include <string.h>
#include "fontdiag_workbench.h"
#include "fontdiag_render.h"

/* The on-disk FONT chunk, as IPrefs reads it (workbench/c/iprefs). */
struct FileFontPrefs
{
    UBYTE   fp_Reserved[4 * 3];
    UBYTE   fp_Reserved2[2];
    UBYTE   fp_Type[2];
    UBYTE   fp_FrontPen;
    UBYTE   fp_BackPen;
    UBYTE   fp_Drawmode;
    UBYTE   fp_pad;
    UBYTE   fp_TextAttr_ta_Name[4];
    UBYTE   fp_TextAttr_ta_YSize[2];
    UBYTE   fp_TextAttr_ta_Style;
    UBYTE   fp_TextAttr_ta_Flags;
    BYTE    fp_Name[FONTNAMESIZE];
};

struct Library *DiskfontBase;
struct Library *IFFParseBase;

static void show_font(const char *what, struct TextFont *font)
{
    if (font)
        Printf(" %s=%s/%ld", (IPTR)what,
               (IPTR)(font->tf_Message.mn_Node.ln_Name ? (char *)font->tf_Message.mn_Node.ln_Name : "?"),
               (LONG)font->tf_YSize);
    else
        Printf(" %s=none", (IPTR)what);
}

int main(void)
{
    struct Screen *wb;
    struct IFFHandle *iff;
    struct FontDiagRender render = {0};
    BOOL render_valid = TRUE;

    DiskfontBase = OpenLibrary("diskfont.library", 0);
    IFFParseBase = OpenLibrary("iffparse.library", 0);

    PutStr("[fontdiag]");
    show_font("system", GfxBase->DefaultFont);
    wb = FontDiag_LockExistingWorkbench();
    if (wb) {
        render_valid = FontDiag_CaptureRender(wb, &render);
        if (render_valid) {
            show_font("workbench", render.raster);
            Printf(" workbench-attr=%s/%ld", (IPTR)render.logical.ta_Name,
                   (LONG)render.logical.ta_YSize);
        } else PutStr(" workbench=incoherent");
    }
    Printf(" diskfont=%s\n", (IPTR)(DiskfontBase ? (char *)DiskfontBase->lib_IdString : "none"));
    if (wb) {
        if (render_valid) {
            const struct ScreenRenderContext *context = render.context;
            Printf("[fontdiag] render-context scale=%ld legacy-factor=%ld bar-height=%ld coherent=yes",
                   (LONG)context->src_Scale, (LONG)context->src_LegacyScaleFactor,
                   (LONG)context->src_PhysicalBarHeight);
            show_font("physical-font", context->src_PhysicalDrawInfo->dri_Font);
            show_font("logical-font", context->src_LogicalDrawInfo->dri_Font);
            PutStr("\n");
        } else PutStr("[fontdiag] render-context result=FAIL reason=incoherent\n");
        UnlockPubScreen(NULL, wb);
    }

    if (!DiskfontBase || !IFFParseBase)
        return RETURN_FAIL;
    iff = AllocIFF();
    if (iff && (iff->iff_Stream = (IPTR)Open("ENV:Sys/font.prefs", MODE_OLDFILE))) {
        InitIFFasDOS(iff);
        if (OpenIFF(iff, IFFF_READ) == 0) {
            StopChunk(iff, ID_PREF, ID_FONT);
            while (ParseIFF(iff, IFFPARSE_SCAN) == 0) {
                struct FileFontPrefs fp;

                if (ReadChunkBytes(iff, &fp, sizeof(fp)) == sizeof(fp)) {
                    struct TextAttr ta;
                    struct TextFont *font;
                    UWORD type = (fp.fp_Type[0] << 8) + fp.fp_Type[1];

                    ta.ta_Name = fp.fp_Name;
                    ta.ta_YSize = (fp.fp_TextAttr_ta_YSize[0] << 8) + fp.fp_TextAttr_ta_YSize[1];
                    ta.ta_Style = fp.fp_TextAttr_ta_Style;
                    ta.ta_Flags = fp.fp_TextAttr_ta_Flags;
                    font = OpenDiskFont(&ta);
                    Printf("[fontdiag] prefs type=%ld %s/%ld open=%s", (LONG)type, (IPTR)ta.ta_Name,
                           (LONG)ta.ta_YSize, (IPTR)(font ? "yes" : "no"));
                    if (font) {
                        Printf(" got=%ld proportional=%s", (LONG)font->tf_YSize,
                               (IPTR)((font->tf_Flags & FPF_PROPORTIONAL) ? "yes" : "no"));
                        CloseFont(font);
                    }
                    PutStr("\n");
                }
            }
            CloseIFF(iff);
        }
        Close((BPTR)iff->iff_Stream);
    } else
        PutStr("[fontdiag] no ENV:Sys/font.prefs\n");
    if (iff)
        FreeIFF(iff);
    CloseLibrary(IFFParseBase);
    CloseLibrary(DiskfontBase);
    return render_valid ? RETURN_OK : RETURN_FAIL;
}
