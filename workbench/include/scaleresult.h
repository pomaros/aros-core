/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Shared CLI and Settings diagnostics. Every receipt belongs to one
    synchronous attempt; formatting never follows a window pointer.
*/
#ifndef DISPLAYSCALE_RESULT_H
#define DISPLAYSCALE_RESULT_H
#include <exec/libraries.h>
#include <intuition/screens.h>
#include <intuition/scaledetail.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>

#define SCALE_DIAGNOSTIC_SIZE 768
struct ScaleAttempt
{
    struct ScreenScaleResult result;
    IPTR requested, actual;
    /* actual_reported: actual came from the receipt, not from a read-back. */
    BOOL screen_available, api_available, actual_known, actual_reported;
};

static inline BOOL ScaleAttemptSucceeded(const struct ScaleAttempt *attempt)
{
    return attempt->api_available && attempt->actual_known && attempt->actual == attempt->requested &&
        attempt->result.ssr_RequestedScale == attempt->requested &&
        attempt->result.ssr_AppliedScale == attempt->actual &&
        (attempt->result.ssr_Status == SSR_SUCCESS ||
         attempt->result.ssr_Status == SSR_UNCHANGED);
}

static inline void ScaleAttemptApply(struct Screen *screen, IPTR wanted,
                                     struct ScaleAttempt *attempt)
{
    memset(attempt, 0, sizeof(*attempt));
    attempt->requested = wanted;
    if (!screen) return;
    attempt->screen_available = TRUE;
    {
        const struct Library *base = (const struct Library *)IntuitionBase;
        if (!base) return;
        attempt->api_available = base->lib_Version > 50 ||
            (base->lib_Version == 50 && base->lib_Revision >= 20);
        if (!attempt->api_available)
        {
            attempt->actual_known = GetAttr(SA_Scale, (Object *)screen, &attempt->actual) &&
                attempt->actual >= 100 && attempt->actual <= 400 && !(attempt->actual % 25);
            return;
        }
    }
    SetAttrs((Object *)screen, SA_ScaleResult, (IPTR)&attempt->result,
             SA_Scale, wanted, TAG_DONE);
    attempt->actual_known = GetAttr(SA_Scale, (Object *)screen, &attempt->actual) &&
        attempt->actual >= 100 && attempt->actual <= 400 && !(attempt->actual % 25);
    /* A reported applied value also belongs to this synchronous attempt. It
     * keeps the gear truthful if the independent readback is unavailable. */
    if (!attempt->actual_known && attempt->result.ssr_Status != SSR_UNREPORTED &&
        attempt->result.ssr_AppliedScale >= 100 &&
        attempt->result.ssr_AppliedScale <= 400 &&
        !(attempt->result.ssr_AppliedScale % 25))
    {
        attempt->actual = attempt->result.ssr_AppliedScale;
        attempt->actual_known = TRUE;
        attempt->actual_reported = TRUE;
    }
}

static inline const char *ScaleResultStage(ULONG stage)
{
    switch (stage)
    {
    case SSR_STAGE_VALIDATE: return "Request validation";
    case SSR_STAGE_FONT: return "Font preparation";
    case SSR_STAGE_LOGICAL_SCREEN: return "Logical screen preparation";
    case SSR_STAGE_DECORATION: return "Window decoration preparation";
    case SSR_STAGE_RENDER_CONTEXT: return "Render context preparation";
    case SSR_STAGE_FRAME: return "Window frame preparation";
    case SSR_STAGE_GEOMETRY: return "Window geometry preparation";
    case SSR_STAGE_ACTIVE_GADGET: return "Active gadget preparation";
    case SSR_STAGE_REQUESTER: return "Requester preparation";
    case SSR_STAGE_MENU: return "Menu preparation";
    case SSR_STAGE_POINTER: return "Pointer preparation";
    case SSR_STAGE_LAYER_GEOMETRY: return "Layer geometry preparation";
    case SSR_STAGE_CURSOR: return "Cursor preparation";
    case SSR_STAGE_PIXELS: return "Pixel preparation";
    case SSR_STAGE_NOTIFICATIONS: return "Notification preparation";
    case SSR_STAGE_COMMIT: return "Scale publication";
    default: return "Scale preparation";
    }
}

static inline void ScaleAttemptFormat(const struct ScaleAttempt *attempt,
                                      char *text, size_t size)
{
    const struct ScreenScaleResult *result = &attempt->result;
    char cause[512], state[80], detail[320];
    char caption[sizeof(result->ssr_WindowTitle)];
    char window[192];
    size_t i;
    for (i = 0; i + 1 < sizeof(caption) && result->ssr_WindowTitle[i]; ++i)
    {
        UBYTE value = result->ssr_WindowTitle[i];
        /* C0 and C1 control bytes and quotes would obscure the one-line
         * diagnostic; 0x9B is CSI on the AROS console. Percent signs are
         * data: every caller uses a constant format. */
        caption[i] = value < 32 || (value >= 127 && value <= 159) || value == '"' ? ' ' : value;
    }
    caption[i] = 0;
    if (caption[0] && result->ssr_Window)
        snprintf(window, sizeof(window), "Window \"%s\" [%p]", caption, (void *)result->ssr_Window);
    else if (caption[0])
        snprintf(window, sizeof(window), "Window \"%s\"", caption);
    else if (result->ssr_Window)
        snprintf(window, sizeof(window), "Window %p", (void *)result->ssr_Window);
    else
        snprintf(window, sizeof(window), "Unidentified window");
    const char *stage = ScaleResultStage(result->ssr_Stage);
    if (attempt->actual_known)
        snprintf(state, sizeof(state), "Current scale: %lu%%.", (unsigned long)attempt->actual);
    else
        snprintf(state, sizeof(state), "Current scale could not be read.");
    if (!attempt->screen_available)
        snprintf(cause, sizeof(cause), "Workbench is unavailable.");
    else if (!attempt->api_available)
        snprintf(cause, sizeof(cause), "Display scaling requires intuition.library 50.20 or newer; no change was requested.");
    else if (result->ssr_Status == SSR_UNREPORTED)
        snprintf(cause, sizeof(cause), "Intuition did not report SA_ScaleResult for this attempt.");
    else if (result->ssr_Status != SSR_FAILED)
    {
        char read_back[32];
        if (attempt->actual_known && !attempt->actual_reported)
            snprintf(read_back, sizeof(read_back), "%lu%%", (unsigned long)attempt->actual);
        else
            snprintf(read_back, sizeof(read_back), "unavailable");
        snprintf(cause, sizeof(cause), "The requested scale could not be confirmed: Intuition reported result %lu with applied scale %lu%%; read-back scale: %s.",
            (unsigned long)result->ssr_Status, (unsigned long)result->ssr_AppliedScale, read_back);
    }
    else if (result->ssr_Reason == SSR_REASON_GEOMETRY &&
             result->ssr_Domain != SSR_DOMAIN_NONE)
    {
        const char *units = result->ssr_Domain == SSR_DOMAIN_LOGICAL ?
            "logical pixels (legacy window)" : "physical pixels";
        const char *axis = result->ssr_Axis == SSR_AXIS_WIDTH ? "width" : "height";
        LONG minimum = result->ssr_Axis == SSR_AXIS_WIDTH ? result->ssr_MinWidth : result->ssr_MinHeight;
        LONG available = result->ssr_Axis == SSR_AXIS_WIDTH ? result->ssr_AvailableWidth : result->ssr_AvailableHeight;
        if (result->ssr_Axis != SSR_AXIS_NONE && minimum > available)
            snprintf(cause, sizeof(cause), "%s needs a minimum %s of %ld; only %ld %s are available.",
                window, axis, (long)minimum, (long)available, units);
        else
            snprintf(cause, sizeof(cause), "%s cannot fit: minimum %ld x %ld, maximum %ld x %ld, available %ld x %ld %s.",
                window, (long)result->ssr_MinWidth, (long)result->ssr_MinHeight,
                (long)result->ssr_MaxWidth, (long)result->ssr_MaxHeight,
                (long)result->ssr_AvailableWidth, (long)result->ssr_AvailableHeight, units);
    }
    else
    {
        const char *reason;
        switch (result->ssr_Reason)
        {
        case SSR_REASON_INVALID_REQUEST: reason = "the scale request is invalid"; break;
        case SSR_REASON_INVALID_CONTEXT:
            reason = result->ssr_Stage == SSR_STAGE_GEOMETRY ?
                "window geometry is invalid or out of range" : "the screen context is invalid";
            break;
        case SSR_REASON_UNSUPPORTED: reason = "this screen does not support the requested change"; break;
        case SSR_REASON_NO_MEMORY: reason = "memory allocation failed"; break;
        case SSR_REASON_COMMIT_FAILED: reason = "the prepared change could not be published"; break;
        case SSR_REASON_PREPARE_FAILED: reason = "preparation was refused"; break;
        default: reason = "the change was refused without a more specific reason"; break;
        }
        /* The exact refusing check, in the words of Intuition's log line. */
        if (ScaleDetailFormat(result, detail, sizeof(detail))) reason = detail;
        if (result->ssr_Window)
            snprintf(cause, sizeof(cause), "%s for %s: %s.", stage, window, reason);
        else
            snprintf(cause, sizeof(cause), "%s: %s.", stage, reason);
    }
    snprintf(text, size, "Cannot apply %lu%%. %s %s", (unsigned long)attempt->requested, cause, state);
}
#endif
