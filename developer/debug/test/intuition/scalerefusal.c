/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.

    Bounded SA_ScaleResult runtime oracle on its own private screen only.
    A normally titled legacy window is refused, retitled through the public
    SetWindowTitles call and refused again, so each caption is Intuition's
    own copy of the title set by OpenWindow or SetWindowTitles.
    Its screen and window are closed through the ordinary public APIs.
    Workbench scale and preferences retain their existing values.
*/
#include <exec/libraries.h>
#include <intuition/intuitionbase.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <dos/dos.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>

static const char title[] = "ScaleRefusalLegacy";
static const char retitle[] = "ScaleRefusalRetitled";

static BOOL attempt(struct Screen *screen, ULONG wanted, BOOL resultfirst,
    struct ScreenScaleResult *result, const char *phase, ULONG *actual)
{
    struct TagItem tags[3];
    IPTR observed = 0;
    tags[0].ti_Tag = resultfirst ? SA_ScaleResult : SA_Scale;
    tags[0].ti_Data = resultfirst ? (IPTR)result : wanted;
    tags[1].ti_Tag = resultfirst ? SA_Scale : SA_ScaleResult;
    tags[1].ti_Data = resultfirst ? wanted : (IPTR)result;
    tags[2].ti_Tag = TAG_DONE; tags[2].ti_Data = 0;
    SetAttrsA((Object *)screen, tags);
    if (!GetAttr(SA_Scale, (Object *)screen, &observed)) return FALSE;
    *actual = (ULONG)observed;
    if (!memchr(result->ssr_WindowTitle, 0, sizeof(result->ssr_WindowTitle))) return FALSE;
    if (printf("[SCALE-REFUSAL] phase=%s status=%lu reason=%lu stage=%lu domain=%lu axis=%lu requested=%lu applied=%lu window=%p min=%ld,%ld max=%ld,%ld available=%ld,%ld detail=%lu method=%lu decor=%lu title=%s readback=%lu result=CHECK\n",
        phase, (unsigned long)result->ssr_Status, (unsigned long)result->ssr_Reason,
        (unsigned long)result->ssr_Stage, (unsigned long)result->ssr_Domain,
        (unsigned long)result->ssr_Axis, (unsigned long)result->ssr_RequestedScale,
        (unsigned long)result->ssr_AppliedScale, (void *)result->ssr_Window,
        (long)result->ssr_MinWidth, (long)result->ssr_MinHeight,
        (long)result->ssr_MaxWidth, (long)result->ssr_MaxHeight,
        (long)result->ssr_AvailableWidth, (long)result->ssr_AvailableHeight,
        (unsigned long)result->ssr_Detail, (unsigned long)result->ssr_DetailMethod,
        (unsigned long)result->ssr_DecorRefusal, (const char *)result->ssr_WindowTitle, (unsigned long)*actual) < 0 ||
        fflush(stdout) != 0) return FALSE;
    return TRUE;
}

/* A geometry refusal and a success carry no frame refusal detail. */
static BOOL no_detail(const struct ScreenScaleResult *result)
{
    return result->ssr_Detail == SSR_DETAIL_NONE && !result->ssr_DetailMethod &&
        !result->ssr_DecorRefusal && !result->ssr_DetailValue[0] &&
        !result->ssr_DetailValue[1] && !result->ssr_DetailValue[2] && !result->ssr_DetailName[0];
}

static BOOL refusal(const struct ScreenScaleResult *result, struct Window *window,
    LONG availablewidth, LONG availableheight, LONG minimumheight, ULONG actual,
    const char *caption)
{
    LONG minimumwidth = window->MinWidth;
    LONG framewidth = window->BorderLeft + window->BorderRight + 1;
    LONG frameheight = window->BorderTop + window->BorderBottom + 1;
    if (minimumwidth < framewidth) minimumwidth = framewidth;
    if (minimumheight < frameheight) minimumheight = frameheight;
    return result->ssr_Status == SSR_FAILED && result->ssr_Reason == SSR_REASON_GEOMETRY &&
        result->ssr_Stage == SSR_STAGE_GEOMETRY && result->ssr_Domain == SSR_DOMAIN_LOGICAL &&
        result->ssr_Axis == SSR_AXIS_HEIGHT && result->ssr_Window == (IPTR)window &&
        result->ssr_RequestedScale == 200 && result->ssr_AppliedScale == 100 && actual == 100 &&
        result->ssr_MinWidth == minimumwidth && result->ssr_MinHeight == minimumheight &&
        result->ssr_MaxWidth == availablewidth && result->ssr_MaxHeight == availableheight &&
        result->ssr_AvailableWidth == availablewidth && result->ssr_AvailableHeight == availableheight &&
        window->Title == (UBYTE *)caption && no_detail(result) &&
        !strcmp((const char *)result->ssr_WindowTitle, (const char *)window->Title);
}

static BOOL clean_result(const struct ScreenScaleResult *result, ULONG status, ULONG wanted,
    ULONG actual)
{
    return result->ssr_Status == status && result->ssr_Reason == SSR_REASON_NONE &&
        result->ssr_Stage == SSR_STAGE_NONE && result->ssr_Domain == SSR_DOMAIN_NONE &&
        result->ssr_Axis == SSR_AXIS_NONE && !result->ssr_Window && !result->ssr_WindowTitle[0] &&
        !result->ssr_MinWidth && !result->ssr_MinHeight && !result->ssr_MaxWidth && !result->ssr_MaxHeight &&
        !result->ssr_AvailableWidth && !result->ssr_AvailableHeight && no_detail(result) &&
        result->ssr_RequestedScale == wanted && result->ssr_AppliedScale == wanted && actual == wanted;
}

int main(void)
{
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    struct ScreenScaleResult result = {0}, failed;
    IPTR queried = 0;
    ULONG actual = 0, checks = 0;
    LONG minimumheight, availablewidth, availableheight;
    const char *failure = "unsupported-intuition";
    int rc = RETURN_FAIL;

    if (!IntuitionBase || IntuitionBase->LibNode.lib_Version < 50 ||
        (IntuitionBase->LibNode.lib_Version == 50 && IntuitionBase->LibNode.lib_Revision < 20))
        goto done;
    failure = "initial-status";
    if (result.ssr_Status != SSR_UNREPORTED) goto done;
    failure = "private-screen";
    screen = OpenScreenTags(NULL, SA_LikeWorkbench, TRUE, SA_Type, CUSTOMSCREEN,
        SA_Quiet, TRUE, SA_Title, (IPTR)"ScaleRefusalOracle", SA_Scale, 100, TAG_DONE);
    if (!screen) goto done;
    failure = "screen-geometry";
    if (screen->Width < 128 || screen->Height < 128 ||
        !GetAttr(SA_Scale, (Object *)screen, &queried) || queried != 100) goto done;
    availablewidth = screen->Width / 2;
    availableheight = screen->Height / 2;
    minimumheight = availableheight + 1;
    failure = "private-window";
    window = OpenWindowTags(NULL, WA_CustomScreen, (IPTR)screen,
        WA_Title, (IPTR)title, WA_Left, 0, WA_Top, 0,
        WA_Width, availablewidth < 320 ? availablewidth : 320,
        WA_Height, minimumheight, WA_MinWidth, 1, WA_MinHeight, minimumheight,
        WA_MaxWidth, (ULONG)-1, WA_MaxHeight, (ULONG)-1,
        WA_Flags, WFLG_SMART_REFRESH | WFLG_DRAGBAR | WFLG_DEPTHGADGET, TAG_DONE);
    if (!window || window->MinHeight != minimumheight) goto done;
    failure = "titled-window";
    if ((window->Flags & WFLG_BORDERLESS) || window->Title != (UBYTE *)title ||
        window->BorderTop <= 0) goto done;
    failure = "refusal-result-first";
    if (!attempt(screen, 200, TRUE, &result, "refuse-result-first", &actual) ||
        !refusal(&result, window, availablewidth, availableheight, minimumheight, actual,
            title)) goto done;
    ++checks;
    memset(&result, 0, sizeof(result));
    SetWindowTitles(window, (CONST_STRPTR)retitle, (CONST_STRPTR)~0);
    failure = "refusal-scale-first";
    if (!attempt(screen, 200, FALSE, &result, "refuse-scale-first", &actual) ||
        !refusal(&result, window, availablewidth, availableheight, minimumheight, actual,
            retitle)) goto done;
    ++checks;
    failed = result;
    failure = "lower-minimum";
    if (!WindowLimits(window, 1, 32, (UWORD)-1, (UWORD)-1)) goto done;
    failure = "success-after-refusal";
    if (!attempt(screen, 200, TRUE, &result, "success-after-refusal", &actual) ||
        !clean_result(&result, SSR_SUCCESS, 200, actual)) goto done;
    ++checks;
    result = failed;
    failure = "unchanged-clears-refusal";
    if (!attempt(screen, 200, FALSE, &result, "unchanged-clears-refusal", &actual) ||
        !clean_result(&result, SSR_UNCHANGED, 200, actual)) goto done;
    ++checks;
    result = failed;
    failure = "reset-clears-refusal";
    if (!attempt(screen, 100, TRUE, &result, "reset-clears-refusal", &actual) ||
        !clean_result(&result, SSR_SUCCESS, 100, actual)) goto done;
    ++checks;
    rc = RETURN_OK;
done:
    if (window) CloseWindow(window);
    if (screen && !CloseScreen(screen)) { rc = RETURN_FAIL; failure = "close-private-screen"; }
    if (printf("[SCALE-REFUSAL] result=%s checks=%lu reason=%s\n",
        rc == RETURN_OK ? "PASS" : "FAIL", (unsigned long)checks,
        rc == RETURN_OK ? "none" : failure) < 0 ||
        fflush(stdout) != 0) return RETURN_FAIL;
    return rc;
}
