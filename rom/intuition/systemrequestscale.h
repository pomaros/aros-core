/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#ifndef INTUITION_SYSTEMREQUESTSCALE_H
#define INTUITION_SYSTEMREQUESTSCALE_H
#include <exec/types.h>
struct Window;
struct Screen;
struct ScreenRenderContext;
struct WindowFitGeometry;
struct RastPort;
struct Rectangle;
struct IntuitionBase;
struct PreparedSystemRequesterScale;
struct IntuiText;
struct IntuiMessage;
/* Coordinator runs in the serialized input.device action and owns IBase -> screen Layer_Info and client-layer leases.
 * TRUE/out NULL means this Window is not an adapted OS-owned requester.
 * Preparation and painting may allocate, but never open fonts/libraries/DOS
 * or call client hooks. The immutable context supplies the physical font.
 * desired and border metrics describe the exact proposed native frame.
 * No live Window, gadget, image or raster is changed before Install. */
BOOL PrepareSystemRequesterScale(struct Window *, const struct ScreenRenderContext *,
    const struct WindowFitGeometry *, LONG, LONG, LONG, LONG,
    struct PreparedSystemRequesterScale **, struct IntuitionBase *);
/* PREPARE only: paint complete proposed client into caller-owned staging RP.
 * The RP origin is the outer Window origin. Bounds excludes native borders. */
BOOL PaintSystemRequesterScale(struct PreparedSystemRequesterScale *,
    struct RastPort *, struct IntuitionBase *);
BOOL SystemRequesterScaleBounds(const struct PreparedSystemRequesterScale *,
    struct Rectangle *);
/* After frame install (also on rollback): copies only owned gadget geometry,
 * render pointers and current generation. Identities, links, selected/input
 * state, Window identity and ExpectedRenderContext remain unchanged. */
void InstallSystemRequesterScale(struct PreparedSystemRequesterScale *, BOOL rollback);
/* After pixel lease release, with Window/owner lifetime still pinned by IBase.
 * A committed render generation remains alive until
 * requester close, including resources a pending input render can reference. */
void FinishSystemRequesterScale(struct PreparedSystemRequesterScale *, BOOL committed,
    struct IntuitionBase *);
/* Process constructor: handledFALSE means unsupported/custom logical route.
 * handledTRUE/NULL means an ordinary construction refusal. */
struct Window *OpenScaledSystemRequester(struct Screen *, CONST_STRPTR,
    CONST_STRPTR, const struct IntuiText *, STRPTR *, ULONG, ULONG,
    LONG, LONG, BOOL *, struct IntuitionBase *);
/* actionCloseWindow, under IBase, before Window removal/free. */
void DetachSystemRequesterScale(struct Window *, struct IntuitionBase *);
BOOL FreeScaledSystemRequester(struct Window *, struct IntuitionBase *);
BOOL HandleScaledSystemRequesterEvent(struct Window *, struct IntuiMessage *,
    struct IntuitionBase *);
BOOL LogicalSystemRequesterScreen(struct Screen *, struct Screen *,
    const struct ScreenRenderContext **, struct IntuitionBase *);
#endif
