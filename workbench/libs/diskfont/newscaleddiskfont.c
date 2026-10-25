/*
    Copyright (C) 1995-2007, The AROS Development Team. All rights reserved.
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.
*/
#include "diskfont_intern.h"
#include <proto/graphics.h>
#include <proto/dos.h>
#include <string.h>
#include "scaledfont_bitmap.h"

#include <clib/diskfont_protos.h>
AROS_LH2(struct DiskFontHeader *, NewScaledDiskFont,
    AROS_LHA(struct TextFont *, sourceFont, A0),
    AROS_LHA(struct TextAttr *, destTextAttr, A1),
    struct Library *, DiskfontBase, 9, Diskfont)
/* Scale a monochrome bitmap font, preserving the chosen face and metrics.
 * Returns a diskfont-managed header whose TextFont has one open accessor.
 * CloseFont(&header->dfh_TF) releases it; the memory handler reclaims the
 * segment after its last user closes it. Colour fonts are unsupported.
 */
{
    AROS_LIBFUNC_INIT
    struct DiskfontBase *base = (struct DiskfontBase *)DiskfontBase;
    struct DiskFontHeader *dfh = NULL;
    struct TextFont plan = {0};
    ULONG count, size;
    UBYTE *storage;
    const char *name;

    if (!destTextAttr || !ScaleBitmapFont(sourceFont, destTextAttr->ta_YSize, &plan, NULL))
        return NULL;
    /* Scaling does not synthesize bold/italic or change the selected face. */
    if ((destTextAttr->ta_Style & ~(FSF_TAGGED | FSF_UNDERLINED)) !=
        (sourceFont->tf_Style & ~(FSF_TAGGED | FSF_UNDERLINED)))
        return NULL;
    count = (ULONG)sourceFont->tf_HiChar - sourceFont->tf_LoChar + 2;
    size = sizeof(*dfh) + count * (sizeof(ULONG) + 2 * sizeof(WORD)) +
        (ULONG)plan.tf_Modulo * plan.tf_YSize;
    dfh = AllocSegment(NULL, size, MEMF_ANY | MEMF_CLEAR, base);
    if (!dfh) return NULL;
    dfh->dfh_FileID = DFH_ID;
    dfh->dfh_Segment = MAKE_REAL_SEGMENT(dfh);
    dfh->dfh_TF = plan;
    storage = (UBYTE *)(dfh + 1);
    if (!ScaleBitmapFont(sourceFont, destTextAttr->ta_YSize, &dfh->dfh_TF, storage))
        goto fail;
    name = sourceFont->tf_Message.mn_Node.ln_Name;
    if (!name) goto fail;
    name = (const char *)FilePart((CONST_STRPTR)name);
    if (strlen(name) >= sizeof(dfh->dfh_Name)) goto fail;
    strncpy(dfh->dfh_Name, name, sizeof(dfh->dfh_Name) - 1);
    dfh->dfh_DF.ln_Name = dfh->dfh_Name;
    dfh->dfh_TF.tf_Message.mn_Node.ln_Name = dfh->dfh_Name;
    /* NOREMFONT keeps zero-accessor fonts on the public list until CleanMem
     * unlinks and frees both the extension and this real DOS segment. */
    if (!ExtendFont(&dfh->dfh_TF, NULL)) goto fail;
    ((struct TextFontExtension *)dfh->dfh_TF.tf_Extension)->tfe_Flags0 |= TE0F_NOREMFONT;
    ObtainSemaphore(base->gfxfontsem);
    AddFont(&dfh->dfh_TF);
    dfh->dfh_TF.tf_Accessors = 1;
    ADDTAIL(&base->diskfontlist, &dfh->dfh_DF);
    ReleaseSemaphore(base->gfxfontsem);
    return dfh;
fail:
    UnLoadSeg(dfh->dfh_Segment);
    return NULL;
    AROS_LIBFUNC_EXIT
}
