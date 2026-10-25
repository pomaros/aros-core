/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#ifndef INTUITION_SCALERESULT_H
#define INTUITION_SCALERESULT_H
#include <intuition/screens.h>
/* Private synchronous output helpers. Callers pin window lifetime with IBase
 * and the window's titlebuffer with the screen LayerInfo lock (windowtitle.h).
 * Only that Intuition-owned copy is read; no pointer into a Window or a
 * client-supplied title escapes. */
static inline void ScaleResultReset(struct ScreenScaleResult *result,
    ULONG requested, ULONG applied)
{
    if (result)
    {
        *result = (struct ScreenScaleResult){0};
        result->ssr_Status = SSR_FAILED;
        result->ssr_Reason = SSR_REASON_PREPARE_FAILED;
        result->ssr_Stage = SSR_STAGE_VALIDATE;
        result->ssr_RequestedScale = requested;
        result->ssr_AppliedScale = applied;
    }
}
static inline void ScaleResultSetStage(struct ScreenScaleResult *result,
    ULONG stage, struct Window *window)
{
    ULONG i;
    if (!result) return;
    ScaleResultReset(result, result->ssr_RequestedScale, result->ssr_AppliedScale);
    result->ssr_Stage = stage;
    if (!window) return;
    result->ssr_Window = (IPTR)window;
    for (i = 0; i + 1 < sizeof(result->ssr_WindowTitle) &&
        i < sizeof(IW(window)->titlebuffer) && IW(window)->titlebuffer[i]; ++i)
        result->ssr_WindowTitle[i] = IW(window)->titlebuffer[i];
    result->ssr_WindowTitle[i] = 0;
}
static inline void ScaleResultReason(struct ScreenScaleResult *result, ULONG reason)
{
    if (result) result->ssr_Reason = reason;
}
/* The exact refusing check. Names are copied, bounded, into the receipt. */
static inline void ScaleResultDetail(struct ScreenScaleResult *result,
    ULONG detail, LONG value0, LONG value1, LONG value2, const char *name)
{
    ULONG i = 0;
    if (!result) return;
    result->ssr_Detail = detail;
    result->ssr_DetailValue[0] = value0;
    result->ssr_DetailValue[1] = value1;
    result->ssr_DetailValue[2] = value2;
    if (name)
        for (; i + 1 < sizeof(result->ssr_DetailName) && name[i]; ++i)
            result->ssr_DetailName[i] = name[i];
    result->ssr_DetailName[i] = 0;
}
/* A refusing layer: Intuition's role for it and its Layers refusal. The
 * class name is copied bounded from a possibly unterminated record. */
static inline void ScaleResultLayer(struct ScreenScaleResult *result, ULONG detail,
    ULONG role, IPTR layer, ULONG check, ULONG flags, ULONG pixels, ULONG lease,
    ULONG driver, const LONG *values, const UBYTE *name, ULONG namesize)
{
    ULONG i = 0;
    if (!result) return;
    ScaleResultDetail(result, detail, 0, 0, 0, NULL);
    result->ssr_LayerRole = role; result->ssr_Layer = layer;
    result->ssr_LayerCheck = check; result->ssr_LayerFlags = flags;
    result->ssr_LayerPixels = pixels; result->ssr_LayerLease = lease;
    result->ssr_LayerDriver = driver;
    for (i = 0; i < 4; ++i) result->ssr_LayerValue[i] = values ? values[i] : 0;
    i = 0;
    if (name)
        for (; i < namesize && i + 1 < sizeof(result->ssr_DetailName) && name[i]; ++i)
            result->ssr_DetailName[i] = name[i];
    result->ssr_DetailName[i] = 0;
}
/* A decorator refusal record is caller-owned and may be unterminated. */
static inline void ScaleResultDecorator(struct ScreenScaleResult *result,
    ULONG method, ULONG code, const LONG *values, const UBYTE *name, ULONG namesize)
{
    ULONG i = 0;
    if (!result) return;
    ScaleResultDetail(result, SSR_DETAIL_DECORATOR, values[0], values[1], values[2], NULL);
    result->ssr_DetailMethod = method;
    result->ssr_DecorRefusal = code;
    for (; i < namesize && i + 1 < sizeof(result->ssr_DetailName) && name[i]; ++i)
        result->ssr_DetailName[i] = name[i];
    result->ssr_DetailName[i] = 0;
}
static inline void ScaleResultFinish(struct ScreenScaleResult *result,
    ULONG status, ULONG applied)
{
    if (!result) return;
    ScaleResultReset(result, result->ssr_RequestedScale, applied);
    result->ssr_Status = status;
    result->ssr_Reason = SSR_REASON_NONE;
    result->ssr_Stage = SSR_STAGE_NONE;
}
static inline void ScaleResultGeometry(struct ScreenScaleResult *result,
    struct Window *window, ULONG domain, ULONG axis, LONG minwidth, LONG minheight,
    LONG maxwidth, LONG maxheight, LONG width, LONG height)
{
    if (!result) return;
    ScaleResultSetStage(result, SSR_STAGE_GEOMETRY, window);
    result->ssr_Reason = SSR_REASON_GEOMETRY;
    result->ssr_Domain = domain; result->ssr_Axis = axis;
    result->ssr_MinWidth = minwidth; result->ssr_MinHeight = minheight;
    result->ssr_MaxWidth = maxwidth < width ? maxwidth : width;
    result->ssr_MaxHeight = maxheight < height ? maxheight : height;
    result->ssr_AvailableWidth = width; result->ssr_AvailableHeight = height;
}
#endif
