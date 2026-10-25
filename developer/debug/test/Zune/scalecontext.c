/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Real MUI object trees, exact-class opt-in, legacy fixed pixels, live scale,
 * same-scale screen font replacement, and a second-screen isolation control.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/iprefs.h>
#include <libraries/mui.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/diskfont.h>
#include <diskfont/diskfont.h>
#include <clib/alib_protos.h>
#include <aros/debug.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct Library *MUIMasterBase, *DiskfontBase;
static struct MUI_CustomClass *awareClass, *legacyClass;
static Object *app;
static ULONG clicks[2], failures;
static BOOL inputRequired;
static ULONG holdSeconds = 5;
static ULONG previousSetups[2];
static void report(const char *format,...);
static void retain_screen(const char *reason)
{
    report("[ZUNE-CONTEXT-PUBLIC] result=FAIL reason=%s retained=1\n",reason);
    for(;;)Wait(0);
}

struct ProbeData { ULONG setups, cleanups, draws; BOOL aware; };
struct Control { struct Window *window; WORD x,y,w,h; ULONG setups,font; };

static void report(const char *format,...)
{
    char line[2048]; va_list args;
    va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
    fputs(line,stdout);fflush(stdout);bug("%s",line);
}
static LONG metric(Object *obj,LONG points)
{
    IPTR scale=100;GetAttr(MUIA_RenderScale,obj,&scale);
    return (points*scale+50)/100;
}
static struct ProbeData *probe_data(Object *obj)
{ return INST_DATA(OCLASS(obj),obj); }

BOOPSI_DISPATCHER(IPTR, ProbeDispatcher, cl, obj, msg)
{
    struct ProbeData *data;
    switch(msg->MethodID)
    {
    case OM_NEW:
        obj=(Object *)DoSuperMethodA(cl,obj,msg);
        if(obj){data=INST_DATA(cl,obj);data->aware=cl==awareClass->mcc_Class;}
        return (IPTR)obj;
    case MUIM_Setup:
        if(!DoSuperMethodA(cl,obj,msg))return FALSE;
        ++((struct ProbeData *)INST_DATA(cl,obj))->setups;
        return TRUE;
    case MUIM_Cleanup:
        ++((struct ProbeData *)INST_DATA(cl,obj))->cleanups;
        return DoSuperMethodA(cl,obj,msg);
    case MUIM_AskMinMax:
    {
        struct MUI_MinMax *m=((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        DoSuperMethodA(cl,obj,msg);data=INST_DATA(cl,obj);
        m->MinWidth+=data->aware?metric(obj,96):96;
        m->MinHeight+=data->aware?metric(obj,64):64;
        m->DefWidth=m->MinWidth;m->DefHeight=m->MinHeight;
        m->MaxWidth=MUI_MAXMAX;m->MaxHeight=MUI_MAXMAX;
        return TRUE;
    }
    case MUIM_Draw:
    {
        LONG x,y,unit;struct RastPort *rp;
        DoSuperMethodA(cl,obj,msg);
        if(!(((struct MUIP_Draw *)msg)->flags&(MADF_DRAWOBJECT|MADF_DRAWUPDATE)))return TRUE;
        data=INST_DATA(cl,obj);++data->draws;rp=_rp(obj);
        unit=data->aware?metric(obj,1):1;
        SetFont(rp,_font(obj));SetAPen(rp,_pens(obj)[MPEN_TEXT]);
        Move(rp,_mleft(obj)+(data->aware?metric(obj,4):4),
            _mtop(obj)+(data->aware?metric(obj,4):4)+_font(obj)->tf_Baseline);
        Text(rp,"Scale",5);
        /* Fixed legacy16x12 raster, or explicitly physical qualified raster. */
        for(y=0;y<12;++y)for(x=0;x<16;++x)
        {
            LONG x0=data->aware?metric(obj,x):x,y0=data->aware?metric(obj,y+32):y+32;
            LONG x1=data->aware?metric(obj,x+1):x+1,y1=data->aware?metric(obj,y+33):y+33;
            SetAPen(rp,_pens(obj)[((x^y)&1)?MPEN_SHINE:MPEN_SHADOW]);
            RectFill(rp,_mleft(obj)+x0,_mtop(obj)+y0,_mleft(obj)+x1-1,_mtop(obj)+y1-1);
        }
        (void)unit;return TRUE;
    }
    }
    return DoSuperMethodA(cl,obj,msg);
}
BOOPSI_DISPATCHER_END

static Object *make_probe(BOOL aware)
{
    return NewObject(aware?awareClass->mcc_Class:legacyClass->mcc_Class,NULL,
        MUIA_Frame,MUIV_Frame_Button,MUIA_Background,MUII_ButtonBack,
        MUIA_InputMode,MUIV_InputMode_RelVerify,MUIA_Font,MUIV_Font_Normal,TAG_DONE);
}
static void pump(ULONG ticks)
{
    while(ticks--)
    {
        ULONG signals=0;LONG id;
        do {
            id=DoMethod(app,MUIM_Application_NewInput,(IPTR)&signals);
            if(id==1001||id==1002)++clicks[id-1001];
            else if(id==MUIV_Application_ReturnID_Quit)++failures;
        } while(id && id!=MUIV_Application_ReturnID_Quit);
        Delay(1);
    }
}
static struct Window *native_window(Object *window)
{ IPTR value=0;GetAttr(MUIA_Window_Window,window,&value);return (struct Window *)value; }
static struct Control control_snapshot(Object *window,Object *probe)
{
    struct Control result;struct Window *native=native_window(window);
    memset(&result,0,sizeof(result));result.window=native;
    if(native){result.x=native->LeftEdge;result.y=native->TopEdge;result.w=native->Width;result.h=native->Height;}
    result.setups=probe_data(probe)->setups;result.font=_font(probe)?_font(probe)->tf_YSize:0;
    return result;
}
static void dump(const char *phase,const char *mode,Object *window,Object *probe,ULONG screenScale)
{
    struct Window *native=native_window(window);IPTR effective=0;
    struct ProbeData *data=probe_data(probe);ULONG factor;
    GetAttr(MUIA_RenderScale,probe,&effective);
    if(!native||!_font(probe)){++failures;report("[ZUNE-CONTEXT] phase=%s mode=%s result=FAIL reason=closed\n",phase,mode);return;}
    { IPTR actual=1;GetAttr(SA_LegacyScaleFactor,(Object *)native->WScreen,&actual);
      factor=data->aware?1:actual; }
    report("[ZUNE-CONTEXT] phase=%s mode=%s object=%p window=%p screen=%p effective=%lu screen-scale=%lu font=%lu setups=%lu cleanups=%lu draws=%lu x=%d y=%d width=%d height=%d pattern-x=%ld pattern-y=%ld pattern-w=%ld pattern-h=%ld click-x=%ld click-y=%ld clicks=%lu\n",
        phase,mode,(void *)probe,(void *)native,(void *)native->WScreen,
        (unsigned long)effective,(unsigned long)screenScale,(unsigned long)_font(probe)->tf_YSize,
        (unsigned long)data->setups,(unsigned long)data->cleanups,(unsigned long)data->draws,
        native->LeftEdge,native->TopEdge,native->Width,native->Height,
        (long)((native->LeftEdge+_mleft(probe))*factor),
        (long)((native->TopEdge+_mtop(probe)+(data->aware?metric(probe,32):32))*factor),
        (long)((data->aware?metric(probe,16):16)*factor),(long)((data->aware?metric(probe,12):12)*factor),
        (long)((native->LeftEdge+_mleft(probe)+_mwidth(probe)/2)*factor),
        (long)((native->TopEdge+_mtop(probe)+_mheight(probe)/2)*factor),
        (unsigned long)(data->aware?clicks[0]:clicks[1]));
}
static BOOL phase(const char *name,ULONG scale,Object *aw,Object *ap,Object *lw,Object *lp,
    Object *cw,Object *cp,const struct Control *control,ULONG hold)
{
    struct Control now;IPTR sa=0,as=0,ls=0;
    ULONG oldAwareClicks=clicks[0],oldLegacyClicks=clicks[1];
    if(!native_window(aw)||!native_window(lw)||!native_window(cw)){
        ++failures;report("[ZUNE-CONTEXT-CHECK] phase=%s result=FAIL reason=window-closed\n",name);return FALSE;
    }
    GetAttr(SA_Scale,(Object *)native_window(aw)->WScreen,&sa);
    GetAttr(MUIA_RenderScale,ap,&as);GetAttr(MUIA_RenderScale,lp,&ls);
    now=control_snapshot(cw,cp);
    if(sa!=scale||as!=scale||ls!=100||now.window!=control->window||now.x!=control->x||now.y!=control->y||now.w!=control->w||now.h!=control->h||now.setups!=control->setups||now.font!=control->font){++failures;report("[ZUNE-CONTEXT-CHECK] phase=%s result=FAIL reason=domain-or-other-screen\n",name);}
    if(probe_data(ap)->setups<=previousSetups[0]||probe_data(lp)->setups<=previousSetups[1]){
        ++failures;report("[ZUNE-CONTEXT-CHECK] phase=%s result=FAIL reason=missing-rebuild\n",name);
    }
    previousSetups[0]=probe_data(ap)->setups;previousSetups[1]=probe_data(lp)->setups;
    dump(name,"aware",aw,ap,scale);dump(name,"legacy",lw,lp,scale);dump(name,"control",cw,cp,100);
    report("[ZUNE-CONTEXT-CAPTURE] phase=%s screen=Workbench scale=%lu other-screen=unchanged\n",name,(unsigned long)scale);
    pump(hold*50);
    if(inputRequired&&(clicks[0]<=oldAwareClicks||clicks[1]<=oldLegacyClicks))++failures;
    report("[ZUNE-CONTEXT-INPUT] phase=%s required=%u aware=%lu legacy=%lu result=%s\n",name,
        (unsigned)inputRequired,(unsigned long)(clicks[0]-oldAwareClicks),(unsigned long)(clicks[1]-oldLegacyClicks),
        !inputRequired||(clicks[0]>oldAwareClicks&&clicks[1]>oldLegacyClicks)?"PASS":"FAIL");
    return failures==0;
}
static BOOL set_font(struct Screen *screen,const struct TextAttr *attr,ULONG scale)
{
    struct IFontPrefs prefs;struct TextFont *opened;memset(&prefs,0,sizeof(prefs));
    if(!attr->ta_Name||strlen((char *)attr->ta_Name)>=sizeof(prefs.fp_Name))return FALSE;
    strcpy((char *)prefs.fp_Name,(char *)attr->ta_Name);prefs.fp_TextAttr=*attr;
    prefs.fp_TextAttr.ta_Name=(STRPTR)prefs.fp_Name;prefs.fp_TextAttr.ta_Style&=~FSF_TAGGED;prefs.fp_ScrFont=TRUE;
    /* Ensure the requested different base actually exists. OpenFont alone may
     * select the same closest bitmap size, which is not a font-change test. */
    opened=OpenDiskFont(&prefs.fp_TextAttr);
    if(!opened)return FALSE;
    if(opened->tf_YSize!=attr->ta_YSize){
        struct DiskFontHeader *scaled=NewScaledDiskFont(opened,&prefs.fp_TextAttr);
        CloseFont(opened);opened=scaled?&scaled->dfh_TF:NULL;
        if(!opened)return FALSE;
    }
    if(!SetIPrefs(&prefs,sizeof(prefs),IPREFS_TYPE_FONT_V37)){CloseFont(opened);return FALSE;}
    CloseFont(opened);
    SetAttrs((Object *)screen,SA_Scale,scale,TAG_DONE);
    pump(30);return TRUE;
}
int main(void)
{
    struct Screen *screen=NULL,*other=NULL;Object *aw=NULL,*lw=NULL,*cw=NULL,*ap=NULL,*lp=NULL,*cp=NULL;
    Object *as=NULL,*ls=NULL,*cs=NULL;struct Control control;
    IPTR arguments[2]={0};struct RDArgs *rdargs=NULL;
    struct TextAttr original,changed={(STRPTR)"topaz.font",16,0,0};char originalName[128];
    ULONG baseAware=0,baseLegacy=0;BOOL originalValid=FALSE,fontChanged=FALSE;IPTR value;
    rdargs=ReadArgs("HOLD/N/K,CLICKS/S",arguments,NULL);
    if(!rdargs)goto done;
    if(arguments[0]){LONG requested=*(LONG *)arguments[0];if(requested<1||requested>120)goto done;holdSeconds=requested;}
    inputRequired=arguments[1]!=0;
    MUIMasterBase=OpenLibrary("muimaster.library",19);if(!MUIMasterBase)goto done;
    DiskfontBase=OpenLibrary("diskfont.library",0);if(!DiskfontBase)goto done;
    screen=LockPubScreen((CONST_STRPTR)"Workbench");if(!screen||!screen->Font)goto done;
    {
        struct Screen *current=LockPubScreen(NULL);BOOL same=current==screen;
        if(current)UnlockPubScreen(NULL,current);
        if(!same)goto done;
    }
    report("[ZUNE-CONTEXT-PUBLIC] result=PASS name=Workbench state=OPEN screen=%p default-lock=matched\n",(void *)screen);
    original=*screen->Font;if(strlen((char *)original.ta_Name)>=sizeof(originalName))goto done;
    strcpy(originalName,(char *)original.ta_Name);original.ta_Name=(STRPTR)originalName;originalValid=TRUE;
    SetAttrs((Object *)screen,SA_Scale,100,TAG_DONE);
    awareClass=MUI_CreateCustomClass(NULL,MUIC_Area,NULL,sizeof(struct ProbeData),ProbeDispatcher);
    legacyClass=MUI_CreateCustomClass(NULL,MUIC_Area,NULL,sizeof(struct ProbeData),ProbeDispatcher);
    if(!awareClass||!legacyClass||!MUI_SetClassCapabilities(awareClass->mcc_Class,MUI_CLASS_SCALE_AWARE))goto done;
    other=OpenScreenTags(NULL,SA_LikeWorkbench,TRUE,SA_Font,(IPTR)&original,SA_Scale,100,
        SA_PubName,(IPTR)"ZuneScaleControl",SA_Title,(IPTR)"Zune scale isolation control",TAG_DONE);
    if(!other||!(PubScreenStatus(other,0)&1))goto done;
    {
        struct Screen *named=LockPubScreen((CONST_STRPTR)"ZuneScaleControl");BOOL same=named==other;
        if(named)UnlockPubScreen(NULL,named);
        if(!same)goto done;
    }
    report("[ZUNE-CONTEXT-PUBLIC] result=PASS name=ZuneScaleControl state=OPEN screen=%p named-lock=matched\n",(void *)other);
    ScreenToBack(other);ScreenToFront(screen);
    ap=make_probe(TRUE);lp=make_probe(FALSE);cp=make_probe(TRUE);
    if(!ap||!lp||!cp)goto done;
    as=VGroup,Child,ap,End;ls=VGroup,Child,lp,End;cs=VGroup,Child,cp,End;
    if(!as||!ls||!cs)goto done;
    aw=WindowObject,MUIA_Window_Title,"Zune qualified",MUIA_Window_Screen,(IPTR)screen,MUIA_Window_LeftEdge,8,MUIA_Window_TopEdge,28,WindowContents,as,End;
    lw=WindowObject,MUIA_Window_Title,"Zune fixed legacy",MUIA_Window_Screen,(IPTR)screen,MUIA_Window_LeftEdge,160,MUIA_Window_TopEdge,28,WindowContents,ls,End;
    cw=WindowObject,MUIA_Window_Title,"Zune other screen",MUIA_Window_Screen,(IPTR)other,WindowContents,cs,End;
    if(!aw||!lw||!cw)goto done;
    app=ApplicationObject,MUIA_Application_Base,"SCALECONTEXT",SubWindow,aw,SubWindow,lw,SubWindow,cw,End;
    if(!app)goto done;
    /* Per-application ephemeral profile; no Save method or preference file. */
    DoMethod(app,MUIM_Application_SetConfigItem,MUICFG_Font_Normal,(IPTR)"");pump(5);
    DoMethod(ap,MUIM_Notify,MUIA_Pressed,FALSE,(IPTR)app,2,MUIM_Application_ReturnID,1001);
    DoMethod(lp,MUIM_Notify,MUIA_Pressed,FALSE,(IPTR)app,2,MUIM_Application_ReturnID,1002);
    set(aw,MUIA_Window_Open,TRUE);set(lw,MUIA_Window_Open,TRUE);set(cw,MUIA_Window_Open,TRUE);pump(30);
    if(!native_window(aw)||!native_window(lw)||!native_window(cw))goto done;
    control=control_snapshot(cw,cp);baseAware=_font(ap)->tf_YSize;baseLegacy=_font(lp)->tf_YSize;
    phase("base100",100,aw,ap,lw,lp,cw,cp,&control,holdSeconds);
    SetAttrs((Object *)screen,SA_Scale,200,TAG_DONE);pump(30);
    if(!native_window(aw)||!native_window(lw)||!_font(ap)||!_font(lp))goto done;
    if(_font(ap)->tf_YSize!=baseAware*2||_font(lp)->tf_YSize!=baseLegacy)++failures;
    phase("scale200",200,aw,ap,lw,lp,cw,cp,&control,holdSeconds);
    if(original.ta_YSize==changed.ta_YSize)changed.ta_YSize=8;
    if(!set_font(screen,&changed,200))goto done;fontChanged=TRUE;
    if(!native_window(aw)||!native_window(lw)||!_font(ap)||!_font(lp))goto done;
    if(_font(ap)->tf_YSize!=screen->Font->ta_YSize*2||_font(lp)->tf_YSize!=screen->Font->ta_YSize)++failures;
    phase("font200",200,aw,ap,lw,lp,cw,cp,&control,holdSeconds);
    SetAttrs((Object *)screen,SA_Scale,100,TAG_DONE);pump(30);
    phase("font100",100,aw,ap,lw,lp,cw,cp,&control,holdSeconds);
    if(!set_font(screen,&original,100))goto done;fontChanged=FALSE;
    if(!native_window(aw)||!native_window(lw)||!_font(ap)||!_font(lp))goto done;
    if(_font(ap)->tf_YSize!=baseAware||_font(lp)->tf_YSize!=baseLegacy)++failures;
    phase("reset100",100,aw,ap,lw,lp,cw,cp,&control,holdSeconds);
    report("[ZUNE-CONTEXT-CHECK] result=%s failures=%lu aware-relayout=%lu legacy-relayout=%lu other-screen=unchanged same-scale-font=checked input-required=%u aware-clicks=%lu legacy-clicks=%lu\n",
        failures?"FAIL":"PASS",(unsigned long)failures,(unsigned long)probe_data(ap)->setups,(unsigned long)probe_data(lp)->setups,(unsigned)inputRequired,(unsigned long)clicks[0],(unsigned long)clicks[1]);
    value=failures;goto cleanup;
done:
    ++failures;value=failures;report("[ZUNE-CONTEXT-CHECK] result=FAIL reason=setup-or-font failures=%lu\n",(unsigned long)failures);
cleanup:
    if(other){ULONG tick;
        for(tick=0;tick<250;++tick){if(PubScreenStatus(other,PSNF_PRIVATE)&1)break;Delay(1);}
        if(tick==250)retain_screen("control-visitor-timeout");
    }
    if(app)MUI_DisposeObject(app);
    if(originalValid&&screen){
        if(fontChanged){app=NULL; /* No application remains to pump after restoration. */
            struct IFontPrefs p;memset(&p,0,sizeof(p));p.fp_TextAttr=original;p.fp_ScrFont=TRUE;SetIPrefs(&p,sizeof(p),IPREFS_TYPE_FONT_V37);}
        SetAttrs((Object *)screen,SA_Scale,100,TAG_DONE);
    }
    if(other&&!CloseScreen(other))retain_screen("control-close-failed");
    if((awareClass&&!MUI_DeleteCustomClass(awareClass))||
       (legacyClass&&!MUI_DeleteCustomClass(legacyClass))){
        retain_screen("class-retained");
    }
    if(screen)UnlockPubScreen(NULL,screen);if(MUIMasterBase)CloseLibrary(MUIMasterBase);
    if(DiskfontBase)CloseLibrary(DiskfontBase);
    if(rdargs)FreeArgs(rdargs);
    return value?RETURN_FAIL:RETURN_OK;
}
