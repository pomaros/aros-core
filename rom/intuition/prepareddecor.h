/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#ifndef INTUITION_PREPAREDDECOR_H
#define INTUITION_PREPAREDDECOR_H
#include <exec/types.h>
#include <intuition/scrdecorclass.h>
struct PreparedScreenScaleDecoration;
struct IntuitionBase;
struct ScreenRenderContext;
struct ScreenScaleResult;
/* Window/theme lifetimes and screen gadget identity are pinned by caller.
 * Prepare queries proposed metrics; Bind attaches the immutable context-owned
 * image before any live state change. Install/rollback are scalar stores only. Release runs
 * after rollback or final commit; image ownership stays with the context.
 * Arbitrary decorator layouts (int_CalcSkinInfo) belong to postcommit work.
 */
struct PreparedScreenScaleDecoration *PrepareScreenScaleDecoration(
    struct Screen *screen, struct TextFont *basefont, struct TextFont *font,
    ULONG scale, struct ScreenScaleResult *, struct IntuitionBase *IntuitionBase);
BOOL BindScreenScaleDecoration(struct PreparedScreenScaleDecoration *,
    const struct ScreenRenderContext *);
const struct ScreenScaleMetrics *PreparedScreenScaleDecorationMetrics(
    const struct PreparedScreenScaleDecoration *prepared);
void InstallScreenScaleDecoration(struct PreparedScreenScaleDecoration *prepared,
    BOOL rollback);
void ReleaseScreenScaleDecoration(struct PreparedScreenScaleDecoration *prepared,
    struct IntuitionBase *IntuitionBase);
#endif
