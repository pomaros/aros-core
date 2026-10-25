/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    This file is part of AROS, distributed under the AROS Public License 1.1.
*/
#ifndef INTUITION_POINTERSCALE_H
#define INTUITION_POINTERSCALE_H
#include <exec/types.h>
#include <hidd/preparedcursor.h>
struct Screen;
struct IntuitionBase;
struct IMonitorNode;
struct SharedPointer;
struct HIDD_PixelMap;
struct PointerRenderGeneration;
struct PreparedScreenPointerScale;
/* Caller holds IBase, keeping screen/monitor/source and prepared metadata
 * stable. Preparation allocates/pins all image resources without publication.
 * Bind runs while the screen's typed pixel lease is held. The frame backend
 * renders PreparedScreenPointerCursor() into staged pixels with the ordinary
 * renderer; Install copies state/metadata only, without methods or allocation.
 * Release runs AFTER the frame lease is released, freeing the losing state.
 */
struct PreparedScreenPointerScale *PrepareScreenPointerScale(struct Screen *, ULONG,
    struct IntuitionBase *);
struct HIDD_CursorState *PreparedScreenPointerCursor(struct PreparedScreenPointerScale *);
BOOL BindScreenPointerScale(struct PreparedScreenPointerScale *, struct HIDD_PixelMap *);
void InstallScreenPointerScale(struct PreparedScreenPointerScale *, BOOL rollback);
BOOL ReleaseScreenPointerScale(struct PreparedScreenPointerScale *, BOOL committed,
    struct IntuitionBase *);
BOOL SetMonitorPointerScale(struct IMonitorNode *, struct Screen *, struct SharedPointer *,
    struct IntuitionBase *);
void ForgetScreenPointerScale(struct Screen *, struct IntuitionBase *);
void DisposeMonitorPointerScale(struct IMonitorNode *, struct IntuitionBase *);
#endif
