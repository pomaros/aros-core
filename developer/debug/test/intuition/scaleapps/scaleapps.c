/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    Real application display-scale measurement, not a gate verdict.
    Usage: scaleapps HOLD/N/K,WAIT/N/K,NOAPPS/S,EACH/S
    HOLD seconds per screenshot stage (default 5); WAIT app startup timeout
    (default 20). NOAPPS measures applications already opened by a harness.
    EACH presents every added application window for a separate capture.
    The five launched apps remain open after reset for further inspection.
*/
#define MUIMASTER_YES_INLINE_STDARG
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/mui.h>
#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <stdio.h>
#include <stdarg.h>
#include <aros/debug.h>
#include <string.h>

static void scale_report(const char *format, ...)
{
    char line[2048];
    va_list args;
    int length;
    va_start(args, format);
    length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length < 0 || (size_t)length >= sizeof(line))
    {
        const char *failure = "[SCALE-APPS-RESULT] result=FAIL reason=report-format\n";
        bug("%s", failure);
        fputs(failure, stdout);
        fflush(stdout);
        return;
    }
    bug("%s", line);
    fputs(line, stdout);
    fflush(stdout);
}

#define CAPACITY 64

struct WindowMetric
{
    struct Window *identity;
    struct Task *task;
    unsigned ordinal;
    WORD x, y, width, height, inner_width, inner_height;
    UWORD font, border_top;
    ULONG idcmp;
    char title[96];
};

static Object *control_app;
static BOOL interrupted;

static void pump(LONG ticks)
{
    LONG elapsed;
    for (elapsed = 0; elapsed < ticks && !interrupted; elapsed += 5)
    {
        ULONG signals = 0;
        if (control_app)
            DoMethod(control_app, MUIM_Application_NewInput, (IPTR)&signals);
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            interrupted = TRUE;
        Delay(5);
    }
}

static int snapshot(struct Screen *screen, struct WindowMetric *metrics)
{
    struct Window *window;
    int count = 0;
    ULONG lock = LockIBase(0);
    for (window = screen->FirstWindow; window && count < CAPACITY; window = window->NextWindow)
    {
        struct WindowMetric *metric = &metrics[count++];
        size_t i = 0;
        memset(metric, 0, sizeof(*metric));
        metric->identity = window;
        metric->task = window->UserPort ? window->UserPort->mp_SigTask : NULL;
        /* Distinguish multiple windows with the same title in one task. */
        metric->ordinal = 0;
        metric->x = window->LeftEdge;
        metric->y = window->TopEdge;
        metric->width = window->Width;
        metric->height = window->Height;
        metric->inner_width = window->Width - window->BorderLeft - window->BorderRight;
        metric->inner_height = window->Height - window->BorderTop - window->BorderBottom;
        metric->border_top = window->BorderTop;
        metric->font = window->RPort->Font ? window->RPort->Font->tf_YSize : 0;
        metric->idcmp = window->IDCMPFlags;
        if (window->Title)
            for (; i < sizeof(metric->title) - 1 && window->Title[i]; ++i)
            {
                UBYTE ch = window->Title[i];
                metric->title[i] = ch >= 32 && ch != '"' ? ch : '_';
            }
        metric->title[i] = 0;
        for (i = 0; i + 1 < (size_t)count; ++i)
            if (metrics[i].task == metric->task && !strcmp(metrics[i].title, metric->title))
                ++metric->ordinal;
    }
    UnlockIBase(lock);
    return count;
}

static int find_application(const struct WindowMetric *metrics, int count,
    const struct WindowMetric *window)
{
    int i;
    for (i = 0; i < count; ++i)
        if ((window->task || metrics[i].identity == window->identity) && metrics[i].task == window->task &&
            metrics[i].ordinal == window->ordinal && !strcmp(metrics[i].title, window->title))
            return i;
    return -1;
}

static int dump(const char *stage, const char *name, struct Screen *screen,
    const struct WindowMetric *baseline, int baseline_count)
{
    struct WindowMetric *metrics = AllocVec(sizeof(*metrics) * CAPACITY, MEMF_CLEAR);
    int count, i, survived = 0;
    if (!metrics) return -1;
    count = snapshot(screen, metrics);
    IPTR scale = 0;
    GetAttr(SA_Scale, (Object *)screen, &scale);
    scale_report("[SCALE-APPS] stage=%s screen=%s scale=%u windows=%d\n",
        stage, name, (unsigned)scale, count);
    for (i = 0; i < count; ++i)
    {
        struct WindowMetric *m = &metrics[i];
        int old = find_application(baseline, baseline_count, m);
        if (old >= 0) ++survived;
        scale_report("[SCALE-WINDOW] stage=%s screen=%s id=%p task=%p ordinal=%u title=\"%s\" xy=%d,%d size=%d,%d inner=%d,%d font=%u border-top=%u change-events=%u baseline=%d\n",
            stage, name, m->identity, m->task, m->ordinal, m->title, m->x, m->y, m->width, m->height,
            m->inner_width, m->inner_height, m->font, m->border_top,
            (unsigned)((m->idcmp & IDCMP_CHANGESCALE) != 0), old);
    }
    scale_report("[SCALE-IDENTITY] stage=%s screen=%s baseline=%d app-windows-survived=%d\n",
        stage, name, baseline_count, survived);
    fflush(stdout);
    if (baseline_count)
    {
        int matched = 0, font_changed = 0, geometry_changed = 0, native_reopened = 0;
        for (i = 0; i < count; ++i)
        {
            int old = find_application(baseline, baseline_count, &metrics[i]);
            if (old < 0) continue;
            ++matched;
            font_changed += metrics[i].font != baseline[old].font;
            geometry_changed += metrics[i].width != baseline[old].width || metrics[i].height != baseline[old].height;
            native_reopened += metrics[i].identity != baseline[old].identity;
        }
        scale_report("[SCALE-RELAYOUT] stage=%s screen=%s matched=%d font-changed=%d geometry-changed=%d native-reopened=%d\n",
            stage, name, matched, font_changed, geometry_changed, native_reopened);
        fflush(stdout);
    }
    FreeVec(metrics);
    return survived;
}

static BOOL present_each(const char *stage, struct Screen *screen,
    const struct WindowMetric *initial, int initial_count, LONG hold)
{
    struct WindowMetric *metrics = AllocVec(sizeof(*metrics) * CAPACITY, MEMF_CLEAR);
    int count, i;
    if (!metrics) return FALSE;
    count = snapshot(screen, metrics);
    for (i = 0; i < count && !interrupted; ++i)
    {
        if (find_application(initial, initial_count, &metrics[i]) >= 0) continue;
        WindowToFront(metrics[i].identity);
        ActivateWindow(metrics[i].identity);
        pump(25);
        scale_report("[SCALE-CAPTURE] stage=%s window=%p title=\"%s\" hold=%d\n",
            stage, metrics[i].identity, metrics[i].title, (int)hold);
        fflush(stdout);
        pump(hold * 50);
    }
    FreeVec(metrics);
    return !interrupted;
}

static BOOL set_scale(struct Screen *screen, ULONG scale)
{
    IPTR got = 0;
    SetAttrs((Object *)screen, SA_Scale, scale, TAG_DONE);
    GetAttr(SA_Scale, (Object *)screen, &got);
    pump(100);
    return got == scale && !interrupted;
}

int main(void)
{
    static const char *commands[] = {
        "SYS:Prefs/Settings PAGE ScreenModeEditor.mcc", "SYS:Tools/Calculator", "SYS:Utilities/Clock",
        "SYS:Prefs/Zune", "SYS:Prefs/Font"
    };
    IPTR args[4] = {0};
    struct RDArgs *rda;
    struct Screen *workbench = NULL, *control = NULL;
    struct WindowMetric *storage = NULL, *initial, *before, *other, *check;
    Object *window = NULL;
    IPTR original = 100;
    LONG hold = 5, wait = 20;
    int initial_count, count = 0, other_count = 0, i, tick, failures = 0;
    BOOL scale_touched = FALSE;

    rda = ReadArgs("HOLD/N/K,WAIT/N/K,NOAPPS/S,EACH/S", args, NULL);
    if (!rda) return RETURN_ERROR;
    if (args[0]) hold = *(LONG *)args[0];
    if (args[1]) wait = *(LONG *)args[1];
    if (hold < 0 || hold > 60 || wait < 1 || wait > 60)
    { FreeArgs(rda); return RETURN_ERROR; }
    storage = AllocVec(4 * CAPACITY * sizeof(*storage), MEMF_CLEAR);
    if (!storage) { ++failures; goto cleanup; }
    initial = storage;
    before = initial + CAPACITY;
    other = before + CAPACITY;
    check = other + CAPACITY;
    workbench = LockPubScreen("Workbench");
    if (!workbench) { ++failures; goto cleanup; }
    GetAttr(SA_Scale, (Object *)workbench, &original);
    scale_touched = TRUE;
    if (!set_scale(workbench, 100)) { ++failures; goto cleanup; }
    control = OpenScreenTags(NULL, SA_LikeWorkbench, TRUE, SA_Scale, 100,
        SA_Behind, TRUE, TAG_DONE);
    if (!control) { ++failures; goto cleanup; }
    control_app = ApplicationObject,
        MUIA_Application_Title, (IPTR)"Scale isolation control",
        MUIA_Application_Base, (IPTR)"SCALECONTROL",
        SubWindow, (IPTR)(window = WindowObject,
            MUIA_Window_Title, (IPTR)"Other screen: keep 100%",
            MUIA_Window_Screen, (IPTR)control,
            MUIA_Window_ID, MAKE_ID('S','C','T','L'),
            WindowContents, (IPTR)VGroup,
                GroupFrame,
                Child, (IPTR)TextObject, MUIA_Text_Contents, (IPTR)"Independent screen control", End,
                Child, (IPTR)SliderObject, MUIA_Numeric_Min, 0, MUIA_Numeric_Max, 100, MUIA_Numeric_Value, 50, End,
                Child, (IPTR)SimpleButton("Unchanged at 100%"),
            End,
        End),
    End;
    if (!control_app) { ++failures; goto cleanup; }
    set(window, MUIA_Window_Open, TRUE);
    pump(50);
    other_count = snapshot(control, other);
    if (other_count != 1) { ++failures; goto cleanup; }
    initial_count = snapshot(workbench, initial);
    if (!args[2])
    {
        for (i = 0; i < (int)(sizeof(commands) / sizeof(commands[0])); ++i)
        {
            LONG result = SystemTags((STRPTR)commands[i], SYS_Asynch, TRUE,
                SYS_Input, BNULL, SYS_Output, BNULL, SYS_Error, BNULL, TAG_DONE);
            scale_report("[SCALE-LAUNCH] command=\"%s\" dispatch=%d\n", commands[i], (int)result);
            if (result < 0) ++failures;
            pump(25);
        }
        for (tick = 0; tick < wait * 10 && !interrupted; ++tick)
        {
            if (snapshot(workbench, before) >= initial_count + 5) break;
            pump(5);
        }
        if (snapshot(workbench, before) < initial_count + 5) ++failures;
    }
    ScreenToFront(control);
    pump(25);
    scale_report("[SCALE-CAPTURE] stage=control100 hold=%d\n", (int)hold); fflush(stdout);
    pump(hold * 50);
    ScreenToFront(workbench);
    pump(25);
    count = snapshot(workbench, before);
    ScreenToFront(workbench);
    if (dump("100", "Workbench", workbench, before, count) != count) ++failures;
    if (dump("100", "Control", control, other, other_count) != other_count) ++failures;
    scale_report("[SCALE-CAPTURE] stage=100 hold=%d\n", (int)hold); fflush(stdout);
    pump(hold * 50);
    if (args[3] && !present_each("100", workbench, initial, initial_count, hold)) ++failures;
    if (!set_scale(workbench, 200)) { ++failures; goto cleanup; }
    if (dump("200", "Workbench", workbench, before, count) != count) ++failures;
    if (dump("200", "Control", control, other, other_count) != other_count) ++failures;
    if (snapshot(control, check) != other_count || memcmp(check, other, sizeof(other[0]) * other_count))
        ++failures;
    scale_report("[SCALE-CAPTURE] stage=200 hold=%d\n", (int)hold); fflush(stdout);
    pump(hold * 50);
    if (args[3] && !present_each("200", workbench, initial, initial_count, hold)) ++failures;
    ScreenToFront(control);
    pump(25);
    scale_report("[SCALE-CAPTURE] stage=control-after200 hold=%d\n", (int)hold); fflush(stdout);
    pump(hold * 50);
    ScreenToFront(workbench);
    if (!set_scale(workbench, 100)) { ++failures; goto cleanup; }
    if (dump("reset", "Workbench", workbench, before, count) != count) ++failures;
    if (dump("reset", "Control", control, other, other_count) != other_count) ++failures;
    if (snapshot(control, check) != other_count || memcmp(check, other, sizeof(other[0]) * other_count))
        ++failures;
    scale_report("[SCALE-CAPTURE] stage=reset hold=%d\n", (int)hold); fflush(stdout);
    pump(hold * 50);
    if (args[3] && !present_each("reset", workbench, initial, initial_count, hold)) ++failures;
cleanup:
    if (workbench && scale_touched)
    {
        if (!set_scale(workbench, original)) ++failures;
        ScreenToFront(workbench);
    }
    if (control_app) { MUI_DisposeObject(control_app); control_app = NULL; }
    if (control && !CloseScreen(control)) ++failures;
    if (workbench) UnlockPubScreen(NULL, workbench);
    FreeArgs(rda);
    scale_report("[SCALE-APPS-RESULT] result=%s windows=%d failures=%d interrupted=%d scope=application-task-identity-and-other-screen-isolation screenshots-required=1\n",
        failures || interrupted ? "FAIL" : "CHECK", count, failures, (int)interrupted);
    return failures || interrupted ? RETURN_FAIL : RETURN_OK;
}
