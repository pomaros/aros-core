/* Copyright (C) 2026 John KNIPPER. AROS Public License 1.1.
 * Disposable VM only: real SettingsWindow BootUndo, five settings across
 * Input/ASL/Zune, observed through IPrefs, real ASL windows and an already
 * running Zune application. No notification receipt or editor readback is
 * substituted for live application. Link with settingswindow.c/catalogue.c.
 */
#define MUIMASTER_YES_INLINE_STDARG
#include <string.h>
#include <intuition/intuitionbase.h>
#include <intuition/preferences.h>
#include <libraries/asl.h>
#include <libraries/mui.h>
#include <libraries/settings.h>
#include <prefs/asl.h>
#include <zune/prefseditor.h>
#include <proto/alib.h>
#include <proto/asl.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/settings.h>
#include "../../../../workbench/prefs/settings/settingswindow.h"
#define PROBE_TAG "BOOTUNDOPROBE"
#include "../afsprobe_report.h"

struct Library *SettingsBase;
static struct catalogue catalogue;
static const char *paths[] = { "Sys/input.prefs", "Sys/Asl.prefs", "Zune/global.prefs" };
static struct FileRequester *requester;
static struct Task *parent;

static BOOL copy(const char *from, const char *to)
{
    UBYTE b[4096];
    LONG n;
    BOOL ok = TRUE;
    BPTR in = Open(from, MODE_OLDFILE), out;
    if (!in) return FALSE;
    out = Open(to, MODE_NEWFILE);
    if (!out) { Close(in); return FALSE; }
    while ((n = Read(in, b, sizeof(b))) > 0)
        if (Write(out, b, n) != n) { ok = FALSE; break; }
    if (n < 0 || !Flush(out)) ok = FALSE;
    if (!Close(out)) ok = FALSE;
    Close(in);
    return ok;
}

/* Group__OM_GET forwards unknown attributes to its children. Only the
 * concrete editor class owns this path; a forwarding container is a proxy. */
static Object *editor_in(Object *o, const Class *expected, const char *expected_path)
{
    IPTR path = 0;
    struct List *children = NULL;
    APTR state;
    Object *child, *result;
    if (OCLASS(o) == expected && GetAttr(MUIA_PrefsEditor_Path, o, &path) && path &&
        strcmp((const char *)path, expected_path) == 0) return o;
    if (!GetAttr(MUIA_Group_ChildList, o, (IPTR *)&children) || !children) return NULL;
    state = children->lh_Head;
    while ((child = NextObject(&state)))
        if ((result = editor_in(child, expected, expected_path))) return result;
    return NULL;
}

/* Bounded diagnostic of real object identities; never replaces an oracle. */
static void gadget_details(Object *o, ULONG editor, ULONG depth, ULONG *count)
{
    struct List *children = NULL;
    APTR state;
    Object *child;
    ULONG id = XGET(o, MUIA_ObjectID);
    IPTR maximum = 0;
    if (depth > 24 || *count >= 80) return;
    if (id) {
        (*count)++;
        say("detail=gadget editor=%lu id=%08lx class=%s numeric-max=%ld",
            (unsigned long)editor, (unsigned long)id, OCLASS(o)->cl_ID,
            GetAttr(MUIA_Numeric_Max,o,&maximum) ? (long)maximum : -1L);
    }
    if (!GetAttr(MUIA_Group_ChildList,o,(IPTR *)&children) || !children) return;
    state = children->lh_Head;
    while ((child=NextObject(&state))) gadget_details(child,editor,depth+1,count);
}

static Object *window_spacing(Object *o)
{
    struct List *children;
    APTR state;
    Object *child, *result;
    IPTR numeric;
    ULONG id = XGET(o, MUIA_ObjectID);
    if ((id >> 16) == (('Z' << 8) | 'B') && GetAttr(MUIA_Numeric_Max, o, &numeric)) return o;
    if (!GetAttr(MUIA_Group_ChildList, o, (IPTR *)&children)) return NULL;
    state = children->lh_Head;
    while ((child = NextObject(&state)))
        if ((result = window_spacing(child))) return result;
    return NULL;
}

static void pump(Object *a, Object *b)
{
    ULONG signals = 0;
    DoMethod(a, MUIM_Application_NewInput, (IPTR)&signals);
    signals = 0;
    DoMethod(b, MUIM_Application_NewInput, (IPTR)&signals);
    Delay(2);
}

static BOOL same_input(const struct Preferences *a, const struct Preferences *b)
{
    return a->KeyRptDelay.tv_secs == b->KeyRptDelay.tv_secs &&
        a->KeyRptDelay.tv_micro == b->KeyRptDelay.tv_micro &&
        a->DoubleClick.tv_secs == b->DoubleClick.tv_secs &&
        a->DoubleClick.tv_micro == b->DoubleClick.tv_micro;
}

static void request_task(void)
{
    AslRequestTags(requester, ASLFR_TitleText, (IPTR)"BootUndo live requester", TAG_DONE);
    Signal(parent, SIGBREAKF_CTRL_F);
}

/* Exercise allocation in the running asl.library, not its file parser. */
static BOOL geometry(struct IBox *box)
{
    int tries;
    BOOL found = FALSE;
    requester = AllocAslRequestTags(ASL_FileRequest, TAG_DONE);
    if (!requester) return FALSE;
    parent = FindTask(NULL);
    SetSignal(0, SIGBREAKF_CTRL_F);
    if (!CreateNewProcTags(NP_Entry, (IPTR)request_task, NP_Name,
        (IPTR)"BootUndo requester", NP_Output, (IPTR)Output(), NP_CloseOutput, FALSE, TAG_DONE))
    { FreeAslRequest(requester); return FALSE; }
    for (tries = 0; tries < 250 && !found; tries++) {
        struct Screen *s;
        ULONG key;
        Delay(2);
        key = LockIBase(0);
        for (s = IntuitionBase->FirstScreen; s && !found; s = s->NextScreen) {
            struct Window *w;
            for (w = s->FirstWindow; w; w = w->NextWindow)
                if (w->Title && !strcmp((char *)w->Title, "BootUndo live requester")) {
                    box->Left = w->LeftEdge; box->Top = w->TopEdge;
                    box->Width = w->Width; box->Height = w->Height; found = TRUE; break;
                }
        }
        UnlockIBase(key);
    }
    AbortAslRequest(requester);
    Wait(SIGBREAKF_CTRL_F);
    FreeAslRequest(requester);
    return found;
}

int main(void)
{
    IPTR args[1] = {0};
    struct RDArgs *rda = ReadArgs("DISPOSABLEVM/S", args, NULL);
    Object *app = NULL, *window, *editors[3], *delay, *doubleclick, *left, *top, *spacing;
    Object *observer = NULL, *observerwindow, *rectangle;
    struct Preferences boot, changed, now;
    struct IBox before, after, restored;
    struct DateStamp begin, end;
    BPTR lock, old_archive = BNULL;
    ULONG i, tries;
    LONG boot_left, changed_left;
    BOOL original[3] = {FALSE}, captured = FALSE, reassigned = FALSE;
    BOOL input_live=FALSE, asl_live=FALSE, zune_live=FALSE;
    LONG elapsed=0;
    char from[160], to[160], value[64];
    if (!rda || !args[0]) { say("result=FAIL reason=DISPOSABLEVM-required"); return RETURN_FAIL; }
    FreeArgs(rda);
    lock = Lock("RAM:Settings-Boot-State", SHARED_LOCK);
    if (lock) { UnLock(lock); say("result=FAIL reason=existing-boot-capture"); return RETURN_FAIL; }
    lock = Lock("ENV:Zune/SET5OBSERVER.prefs", SHARED_LOCK);
    if (lock) { UnLock(lock); say("result=FAIL reason=observer-local-override"); return RETURN_FAIL; }
    lock = Lock("ENVARC:Zune/SET5OBSERVER.prefs", SHARED_LOCK);
    if (lock) { UnLock(lock); say("result=FAIL reason=observer-archive-override"); return RETURN_FAIL; }
    lock = Lock("RAM:Set5Probe", SHARED_LOCK);
    if (lock) { UnLock(lock); say("result=FAIL reason=existing-probe-fixture"); return RETURN_FAIL; }
    SettingsBase = OpenLibrary("settings.library", 1);
    CHECK(SettingsBase && AslBase && MUIMasterBase, "real libraries present");
    if (!SettingsBase || !AslBase || !MUIMasterBase) goto done;
    /* A minimal catalogue instantiates the actual three shared editors. */
    catalogue.group_count = 1; catalogue.row_count = 3;
    strcpy(catalogue.groups[0].id, "probe"); strcpy(catalogue.groups[0].name, "Probe");
    for (i = 0; i < 3; i++) {
        static const char *classes[] = {"InputEditor.mcc", "AslEditor.mcc", "ZuneEditor.mcc"};
        strcpy(catalogue.rows[i].group, "probe"); strcpy(catalogue.rows[i].class_name, classes[i]);
        strcpy(catalogue.rows[i].name, classes[i]);
    }
    app = ApplicationObject, MUIA_Application_Base, (IPTR)"SET5CONTROL",
        SubWindow, (IPTR)(window = SettingsWindowObject,
            MUIA_SettingsWindow_Catalogue, (IPTR)&catalogue, End), End;
    CHECK(app != NULL, "actual SettingsWindow instantiated");
    if (!app) goto done;
    SET(window, MUIA_Window_Open, TRUE);
    for (i = 0; i < 3; i++) {
        static const char *editor_paths[] = {"SYS/input.prefs", "SYS/Asl.prefs", "Zune/global.prefs"};
        Class *expected;
        DoMethod(window, MUIM_SettingsWindow_OpenRow, i);
        expected = MUI_GetClass((ClassID)catalogue.rows[i].class_name);
        editors[i] = expected ? editor_in((Object *)XGET(window,MUIA_Window_RootObject),
            expected, editor_paths[i]) : NULL;
        if (expected) MUI_FreeClass(expected);
        CHECK(editors[i] != NULL, "editor %lu opened", (unsigned long)i);
        if (!editors[i]) goto done;
        say("detail=editor row=%lu pointer=%p class=%s path=%s", (unsigned long)i,
            editors[i], OCLASS(editors[i])->cl_ID,
            (char *)XGET(editors[i],MUIA_PrefsEditor_Path));
    }
    delay = find_id(editors[0], MAKE_ID('I','P','R','D'));
    doubleclick = find_id(editors[0], MAKE_ID('I','P','D','C'));
    left = find_id(editors[1], MAKE_ID('A','S','O','X'));
    top = find_id(editors[1], MAKE_ID('A','S','O','Y'));
    spacing = window_spacing(editors[2]);
    say("detail=gadget-presence delay=%d doubleclick=%d left=%d top=%d spacing=%d",
        delay!=NULL,doubleclick!=NULL,left!=NULL,top!=NULL,spacing!=NULL);
    CHECK(delay && doubleclick && left && top && spacing, "five setting gadgets found");
    if (!delay || !doubleclick || !left || !top || !spacing) {
        for(i=0;i<3;i++) { ULONG count=0; gadget_details(editors[i],i,0,&count); }
        goto done;
    }
    /* Preserve pre-probe files; this command is restricted to disposable guests. */
    { static const char *dirs[] = {"RAM:Set5Probe", "RAM:Set5Probe/Boot", "RAM:Set5Probe/Original",
        "RAM:Set5Probe/Boot/Sys", "RAM:Set5Probe/Boot/Zune", "RAM:Set5Probe/Original/Sys",
        "RAM:Set5Probe/Original/Zune", "ENV:Sys", "ENV:Zune"};
      for (i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) { lock=CreateDir(dirs[i]); if(lock) UnLock(lock); } }
    for (i=0;i<3;i++) {
        snprintf(from,sizeof(from),"ENV:%s",paths[i]); snprintf(to,sizeof(to),"RAM:Set5Probe/Original/%s",paths[i]);
        lock=Lock(from,SHARED_LOCK); if(lock) { UnLock(lock); original[i]=TRUE; CHECK(copy(from,to),"saved original %lu",(unsigned long)i); }
    }
    if (failures) goto cleanup;
    SET(delay,MUIA_Numeric_Value,15); SET(doubleclick,MUIA_Numeric_Value,12);
    SET(find_id(editors[1],MAKE_ID('A','S','P','O')),MUIA_Cycle_Active,ASLPREFS_POS_TOPLEFTSCR);
    SET(left,MUIA_String_Integer,40); SET(top,MUIA_String_Integer,30); SET(spacing,MUIA_Numeric_Value,2);
    for(i=0;i<3;i++) {
        CHECK(DoMethod(editors[i],MUIM_PrefsEditor_Use),"baseline Use editor %lu",(unsigned long)i);
        snprintf(to,sizeof(to),"RAM:Set5Probe/Boot/%s",paths[i]);
        CHECK(DoMethod(editors[i],MUIM_PrefsEditor_Export,(IPTR)to),"capture source export %lu",(unsigned long)i);
    }
    old_archive=Lock("ENVARC:",SHARED_LOCK); lock=Lock("RAM:Set5Probe/Boot",SHARED_LOCK);
    reassigned=old_archive && lock && AssignLock("ENVARC",lock);
    CHECK(reassigned,"private boot source assign");
    if (!reassigned) { if(lock) UnLock(lock); goto cleanup; }
    captured=SystemTags("C:SettingsBoot CAPTURE QUIET",SYS_Asynch,FALSE,TAG_DONE)==RETURN_OK;
    CHECK(captured,"production SettingsBoot complete capture");
    if (!captured) goto cleanup;
    observer=ApplicationObject, MUIA_Application_Base,(IPTR)"SET5OBSERVER",
        MUIA_Application_SingleTask,TRUE,
        SubWindow,(IPTR)(observerwindow=WindowObject,
            WindowContents,(IPTR)VGroup, Child,(IPTR)(rectangle=RectangleObject,End),End,End),End;
    CHECK(observer!=NULL,"running Zune observer instantiated");
    if(!observer) goto cleanup;
    CHECK(!strcmp((const char *)XGET(observer,MUIA_Application_Base),"SET5OBSERVER"),
        "observer preference base matches guarded local path");
    if(failures) goto cleanup;
    SET(observerwindow,MUIA_Window_Open,TRUE);
    for(tries=0;tries<150;tries++) {
        pump(app,observer); GetPrefs(&boot,sizeof(boot));
        if(boot.KeyRptDelay.tv_secs==0 && boot.KeyRptDelay.tv_micro==320000 &&
            boot.DoubleClick.tv_secs==0 && boot.DoubleClick.tv_micro==260000) break;
    }
    CHECK(boot.KeyRptDelay.tv_secs==0 && boot.KeyRptDelay.tv_micro==320000 &&
        boot.DoubleClick.tv_secs==0 && boot.DoubleClick.tv_micro==260000,
        "IPrefs applied exact baseline values (320ms,260ms)");
    boot_left=XGET(rectangle,MUIA_LeftEdge);
    CHECK(geometry(&before),"baseline real ASL requester measured");
    CHECK(GetSettingA("zune/window/spacing-left",value,sizeof(value),NULL)==SETTING_OK && !strcmp(value,"2"),"selected Zune gadget is spacing-left");
    /* Exactly five selected settings across the three real editors. */
    SET(delay,MUIA_Numeric_Value,42); SET(doubleclick,MUIA_Numeric_Value,25);
    SET(left,MUIA_String_Integer,80); SET(top,MUIA_String_Integer,60); SET(spacing,MUIA_Numeric_Value,7);
    for(i=0;i<3;i++) CHECK(DoMethod(editors[i],MUIM_PrefsEditor_Use),"changed Use editor %lu",(unsigned long)i);
    for(tries=0;tries<150;tries++) {
        pump(app,observer); GetPrefs(&changed,sizeof(changed));
        if(!same_input(&boot,&changed) && (LONG)XGET(rectangle,MUIA_LeftEdge)!=boot_left) break;
    }
    changed_left=XGET(rectangle,MUIA_LeftEdge);
    CHECK(changed.KeyRptDelay.tv_secs==0 && changed.KeyRptDelay.tv_micro==860000 &&
        changed.DoubleClick.tv_secs==0 && changed.DoubleClick.tv_micro==520000,
        "IPrefs applied exact changed values (860ms,520ms)");
    CHECK(changed_left-boot_left==5,"existing Zune window layout changed by five pixels");
    CHECK(geometry(&after) && after.Left-before.Left==40 && after.Top-before.Top==30,"real ASL requester changed both offsets");
    DateStamp(&begin);
    CHECK(DoMethod(window,MUIM_SettingsWindow_BootUndo),"actual Settings BootUndo action");
    for(tries=0;tries<150;tries++) {
        pump(app,observer); GetPrefs(&now,sizeof(now));
        if(same_input(&boot,&now) && (LONG)XGET(rectangle,MUIA_LeftEdge)==boot_left) break;
    }
    input_live=same_input(&boot,&now);
    zune_live=(LONG)XGET(rectangle,MUIA_LeftEdge)==boot_left && changed_left!=boot_left;
    asl_live=geometry(&restored) && restored.Left==before.Left && restored.Top==before.Top;
    DateStamp(&end);
    elapsed=((end.ds_Days-begin.ds_Days)*86400+(end.ds_Minute-begin.ds_Minute)*60)*1000+
        (end.ds_Tick-begin.ds_Tick)*20;
    CHECK(input_live,"IPrefs restored both live input settings");
    CHECK(zune_live,"existing Zune window restored live spacing");
    CHECK(asl_live,"real ASL requester restored both offsets");
    DeleteFile("RAM:Settings-Boot-State");
    CHECK(!DoMethod(window,MUIM_SettingsWindow_BootUndo),"missing capture marker refuses actual UI action");
    /* Local-file precedence is an independent control. The existing
     * application must react to creation of its file, then retain it
     * across a global preference rewrite. */
    SET(spacing,MUIA_Numeric_Value,5);
    CHECK(DoMethod(editors[2],MUIM_PrefsEditor_Export,(IPTR)"ENV:Zune/SET5OBSERVER.prefs"),"local override exported");
    for(tries=0;tries<50;tries++) pump(app,observer);
    CHECK((LONG)XGET(rectangle,MUIA_LeftEdge)==boot_left+3,"running observer applied local override");
    SET(spacing,MUIA_Numeric_Value,9);
    CHECK(DoMethod(editors[2],MUIM_PrefsEditor_Use),"global precedence control published");
    for(tries=0;tries<50;tries++) pump(app,observer);
    CHECK((LONG)XGET(rectangle,MUIA_LeftEdge)==boot_left+3,"global rewrite retains local-file precedence");
cleanup:
    if(reassigned) { AssignLock("ENVARC",old_archive); old_archive=BNULL; }
    if(old_archive) UnLock(old_archive);
    for(i=0;i<3;i++) {
        snprintf(to,sizeof(to),"ENV:%s",paths[i]); snprintf(from,sizeof(from),"RAM:Set5Probe/Original/%s",paths[i]);
        if(original[i]) CHECK(copy(from,to),"original preferences restored %lu",(unsigned long)i); else DeleteFile(to);
    }
    if(captured) DeleteFile("RAM:Settings-Boot-State");
    {
        char line[220];
        int n=snprintf(line,sizeof(line),"[SET5] %s boot-undo: mechanism=copy restored-files=3 applied-live=%d elapsed-ms=%ld editors=3 settings=5\n",
            failures ? "FAIL" : "PASS",failures ? 0 : 1,(long)elapsed);
        if(n<0 || (size_t)n>=sizeof(line)) {afsprobe_debug_record(NULL,513);failures++;}
        else {afsprobe_debug_record(line,(size_t)n);printf("%s",line);}
    }
    {
        char json[320];
        BPTR report;
        snprintf(json,sizeof(json),"{\"gate\":\"SET5\",\"result\":\"%s\",\"mechanism\":\"copy\",\"restored_files\":3,\"editors\":3,\"settings\":5,\"input_live\":%d,\"asl_live\":%d,\"zune_live\":%d,\"elapsed_ms\":%ld}\n",
            failures ? "FAIL" : "PASS",input_live,asl_live,zune_live,(long)elapsed);
        report=Open("RAM:settings-boot-undo.json",MODE_NEWFILE);
        CHECK(report && Write(report,json,strlen(json))==(LONG)strlen(json),"settings-boot-undo.json artifact written");
        if(report) Close(report);
        say("artifact settings-boot-undo.json %s",json);
    }
done:
    if(observer) MUI_DisposeObject(observer);
    DeleteFile("ENV:Zune/SET5OBSERVER.prefs");
    if(app) MUI_DisposeObject(app);
    if(SettingsBase) CloseLibrary(SettingsBase);
    return PROBE_RESULT();
}
