#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise actual WindowOpen generation-race cleanup and bounded queued retry."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[4]
s=(root/'workbench/libs/muimaster/classes/window.c').read_text()
def function(start):
    a=s.index(start);b=s.index('{',a);n=1;i=b+1
    while n:
        n+=(s[i]=='{')-(s[i]=='}');i+=1
    return s[a:i]
helpers=function('static void CancelContextRetry(')+'\n'+function('static void QueueContextRetry(')
ratio=function('static LONG WindowScaleRequest(')
cleanup=function('static ULONG RetryWindowContext(')
tags=s[s.index('        WA_Flags,'):s.index('        data->wd_Title ?',s.index('        WA_Flags,'))]
opened=function('static ULONG WindowOpen(struct IClass *cl, Object *obj)\n{')
retry=function('static IPTR Window__MUIM_RetryRenderContext(')
# These lifecycle entry points must cancel their own queued callback before disposal/close.
assert 'CancelContextRetry(obj, data);' in function('IPTR Window__OM_DISPOSE(')
assert 'CancelContextRetry(obj, data);' in function('IPTR Window__OM_SET(')
assert s.index('ModifyIDCMP(win, data->wd_Events);') < s.index('static ULONG WindowOpen(struct IClass *cl, Object *obj)\n{')
head=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
typedef uintptr_t IPTR;typedef uint32_t ULONG;typedef int32_t LONG;typedef int64_t QUAD;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MUIWF_OPENED 1
#define MUIWF_DONTACTIVATE 2
#define WFLG_ACTIVATE 4096
enum {WA_Flags=1,WA_Hidden,WA_RenderContext};
#define MADF_DRAWOBJECT 2
#define D(...)
enum {MUIA_Application_Menustrip=1,MUIM_Window_Setup,MUIM_Window_Cleanup,MUIM_Cleanup,MUIA_Window_ActiveObject,MUIA_Window_Open,MUIM_Application_UnpushMethod,MUIM_Application_PushMethod,MUIM_Window_RetryRenderContext,MUIM_Window_ApplyScaleContext};
struct IClass {int unused;};
struct ScreenRenderContext {ULONG src_Scale;BOOL src_PreparedScaleWindow;};
struct Screen {const struct ScreenRenderContext *context;};
struct Window {LONG BorderLeft,BorderTop,Width,Height,BorderRight,BorderBottom;};
struct MUI_RenderInfo {struct Screen *mri_Screen;const struct ScreenRenderContext *mri_RenderContext;BOOL mri_ScaleAware;struct Window *mri_Window;};
struct MUI_WindowData {struct MUI_RenderInfo wd_RenderInfo;IPTR wd_ContextRetryToken,wd_ContextRecalcToken;unsigned char wd_ContextRetryBudget;BOOL wd_ContextRetrying;void *wd_RootObject,*wd_ChildMenustrip,*wd_Menustrip,*wd_Background,*wd_ActiveObject,*wd_OldActive;ULONG wd_Flags,wd_ID,wd_DisplayScale;LONG wd_ReqWidth,wd_ReqHeight,wd_Width,wd_Height;};
struct Prefs {BOOL publicscreen_pop_to_front;};
struct Global {struct Screen *mgi_CustomScreen;struct Prefs *mgi_Prefs;};
typedef struct Obj {struct MUI_WindowData data;struct Global global;} Object;
#define INST_DATA(cl,obj) ((void)(cl),&(obj)->data)
#define muiGlobalInfo(obj) (&(obj)->global)
#define _app(obj) (obj)
static struct Screen screen;
static struct ScreenRenderContext a={100,TRUE},b={200,TRUE};
static struct Window win={1,2,100,100,1,2};
static unsigned setups,rootsetups,cleanups,rootcleanups,nativeopens,nativecloses,shows,draws,installs,queueadds,queueremoves,reveals,activations;
static int change_mode,fail_queue,subscriptions;static Object *queued;
static const struct ScreenRenderContext *zune_screen_context(struct Screen *s){assert(subscriptions||!nativeopens);return s->context;}
static BOOL WindowScaleAware(Object *obj,struct MUI_WindowData *data,const struct ScreenRenderContext *context){(void)obj;(void)data;return context&&context->src_PreparedScaleWindow;}
static ULONG WindowOpen(struct IClass*,Object*);
static IPTR DoMethod(void *o,ULONG method,...){Object *obj=o;va_list ap;va_start(ap,method);IPTR result=1;
 switch(method){
 case MUIM_Window_Setup:++setups;obj->data.wd_RenderInfo.mri_Screen=&screen;obj->data.wd_RenderInfo.mri_RenderContext=screen.context;obj->data.wd_DisplayScale=screen.context->src_Scale;break;
 case MUIM_Window_Cleanup:++cleanups;assert(!obj->global.mgi_CustomScreen);obj->data.wd_RenderInfo.mri_RenderContext=NULL;break;
 case MUIM_Cleanup:++rootcleanups;break;
 case MUIM_Application_PushMethod:assert(va_arg(ap,IPTR)==(IPTR)obj);assert(va_arg(ap,int)==1);assert(va_arg(ap,int)==MUIM_Window_RetryRenderContext);++queueadds;if(fail_queue)result=0;else{assert(!queued);queued=obj;result=(IPTR)obj;}break;
 case MUIM_Application_UnpushMethod:assert(va_arg(ap,IPTR)==(IPTR)obj);assert(va_arg(ap,IPTR)==(IPTR)obj);assert(va_arg(ap,int)==MUIM_Window_RetryRenderContext);assert(queued==obj);queued=NULL;++queueremoves;break;
 default:assert(0);
 }va_end(ap);return result;
}
static void get(Object *obj,ULONG attr,void **out){(void)obj;assert(attr==MUIA_Application_Menustrip);*out=NULL;}
static void UndisplayWindow(Object*,struct MUI_WindowData*);
static void host_set(Object *obj,ULONG attr,IPTR value){if(attr==MUIA_Window_Open){if(value)WindowOpen(NULL,obj);else{UndisplayWindow(obj,&obj->data);obj->data.wd_Flags&=~MUIWF_OPENED;DoMethod(obj->data.wd_RootObject,MUIM_Cleanup);DoMethod(obj,MUIM_Window_Cleanup);}}else assert(attr==MUIA_Window_ActiveObject);}
#define set(o,a,v) host_set(o,a,(IPTR)(v))
static BOOL DoSetupMethod(void *root,struct MUI_RenderInfo *mri){(void)root;(void)mri;++rootsetups;return TRUE;}
static void WindowMinMax(Object *obj,struct MUI_WindowData *data){(void)obj;(void)data;}
static void WindowSelectDimensions(struct MUI_WindowData *data){data->wd_Width=data->wd_ReqWidth;data->wd_Height=data->wd_ReqHeight;}
static BOOL DisplayWindow(Object *obj,struct MUI_WindowData *data){(void)obj;ULONG flags=WFLG_ACTIVATE;IPTR native_tags[]={NATIVE_TAG_FRAGMENT 0};assert(native_tags[0]==WA_Flags&&!(native_tags[1]&WFLG_ACTIVATE)&&native_tags[2]==WA_Hidden&&native_tags[3]&&native_tags[4]==WA_RenderContext&&native_tags[5]==(IPTR)data->wd_RenderInfo.mri_RenderContext);++nativeopens;data->wd_RenderInfo.mri_Window=&win;subscriptions=1;if(change_mode==1){screen.context=&b;change_mode=0;}else if(change_mode==2)screen.context=screen.context==&a?&b:&a;return TRUE;}
static void UndisplayWindow(Object *obj,struct MUI_WindowData *data){(void)obj;assert((data->wd_Flags&MUIWF_OPENED)?data->wd_ID==99:data->wd_ID==0);++nativecloses;data->wd_RenderInfo.mri_Window=NULL;}
static BOOL ShowWindow(struct Window *w,struct Window *other){assert(w&&!other);if(change_mode==3){screen.context=&b;change_mode=0;return FALSE;}++reveals;return TRUE;}
static void ActivateWindow(struct Window *w){assert(w&&reveals);++activations;}
static void InstallBackbuffer(struct IClass *cl,Object *obj){(void)cl;(void)obj;++installs;}
static void WindowShow(struct IClass *cl,Object *obj){(void)cl;++shows;assert(obj->data.wd_RenderInfo.mri_RenderContext==screen.context);}
static void zune_imspec_draw(void *bg,struct MUI_RenderInfo *mri,LONG l,LONG t,LONG w,LONG h,LONG x,LONG y,int flags){(void)bg;(void)mri;(void)l;(void)t;(void)w;(void)h;(void)x;(void)y;(void)flags;++draws;if(change_mode==4){screen.context=&b;change_mode=0;}}
static void ScreenToFront(struct Screen *s){(void)s;}
static void MUI_Redraw(void *root,int flags){(void)root;(void)flags;}
'''
main=r'''
static Object make(void){static struct Prefs prefs;Object obj={0};obj.data.wd_RootObject=(void *)1;obj.global.mgi_CustomScreen=&screen;obj.global.mgi_Prefs=&prefs;obj.data.wd_ID=99;obj.data.wd_ContextRetryBudget=4;obj.data.wd_ReqWidth=100;obj.data.wd_ReqHeight=50;obj.data.wd_RenderInfo.mri_ScaleAware=TRUE;return obj;}
static void reset(void){screen.context=&a;setups=rootsetups=cleanups=rootcleanups=nativeopens=nativecloses=shows=draws=installs=queueadds=queueremoves=reveals=activations=0;subscriptions=0;queued=NULL;change_mode=fail_queue=0;}
static void drain(Object *obj){unsigned n=0;while(queued){assert(++n<=4);assert(queued==obj);queued=NULL;Window__MUIM_RetryRenderContext(NULL,obj);}}
int main(void){
 assert(WindowScaleRequest(21474837,100,200)==42949674);assert(WindowScaleRequest(INT32_MAX,100,400)==INT32_MAX);
 reset();Object obj=make();assert(WindowOpen(NULL,&obj));assert(shows==1&&installs==1&&!queueadds);
 reset();obj=make();change_mode=1;assert(!WindowOpen(NULL,&obj));assert(!shows&&!installs&&!draws&&nativecloses==1&&rootcleanups==1&&cleanups==1&&obj.global.mgi_CustomScreen==&screen&&obj.data.wd_ID==99);assert(obj.data.wd_ReqWidth==200&&obj.data.wd_ReqHeight==100);drain(&obj);assert(shows==1&&installs==1&&nativeopens==2&&obj.data.wd_RenderInfo.mri_RenderContext==&b&&!obj.data.wd_ContextRetrying);
 reset();obj=make();change_mode=3;assert(!WindowOpen(NULL,&obj));assert(!reveals&&!shows&&!installs&&!activations);drain(&obj);assert(reveals==1&&shows==1&&activations==1);
 reset();obj=make();change_mode=4;assert(!WindowOpen(NULL,&obj));assert(reveals==1&&shows==1&&!activations&&nativecloses==1);drain(&obj);assert(shows==2&&activations==1);
 reset();obj=make();change_mode=2;assert(!WindowOpen(NULL,&obj));drain(&obj);assert(nativeopens==5&&nativecloses==5&&!shows&&!installs&&!draws&&queueadds==4&&!obj.data.wd_ContextRetryToken);
 reset();obj=make();change_mode=1;assert(!WindowOpen(NULL,&obj));CancelContextRetry(&obj,&obj.data);assert(!queued&&!obj.data.wd_ContextRetryToken&&!obj.data.wd_ContextRetryBudget&&queueremoves==1);
 reset();obj=make();change_mode=1;fail_queue=1;assert(!WindowOpen(NULL,&obj));assert(!queued&&!obj.data.wd_ContextRetryToken&&!shows&&nativecloses==1&&cleanups==1&&rootcleanups==1);
 puts("PASS production WindowOpen: missed-subscription race, coherent retry, custom screen ownership, cancellation, bounded churn, queue failure");
}
'''
source=head.replace('NATIVE_TAG_FRAGMENT',tags)+helpers+'\n'+retry+'\n'+ratio+'\n'+cleanup+'\n'+opened+'\n'+main
mutants=[('native-activation','flags & ~WFLG_ACTIVATE','flags'),('native-hidden','WA_Hidden, (IPTR) TRUE','WA_Hidden, (IPTR) FALSE'),('ignore-bound-show-failure','!ShowWindow(data->wd_RenderInfo.mri_Window, NULL)','(ShowWindow(data->wd_RenderInfo.mri_Window, NULL), 0)'),('raw-request-overflow','(QUAD)request * newscale / oldscale','request * newscale / oldscale'),('skip-generation-check','if (zune_screen_context(data->wd_RenderInfo.mri_Screen) !=','if (zune_screen_context(data->wd_RenderInfo.mri_Screen) =='),('unbounded-retry','--data->wd_ContextRetryBudget;','/* no bound */'),('leak-root-setup','DoMethod(data->wd_RootObject, MUIM_Cleanup);','/* leak */'),('lose-custom-screen','muiGlobalInfo(obj)->mgi_CustomScreen = custom;','muiGlobalInfo(obj)->mgi_CustomScreen = ((void)custom, NULL);'),('leave-queued-on-cancel','token, MUIM_Window_RetryRenderContext);','0, 0);')]
with tempfile.TemporaryDirectory(prefix='scaleopen-') as d:
    d=Path(d)
    for name,text in [('production',source)]+[(n,source.replace(a,b)) for n,a,b in mutants]:
        c=d/(name+'.c');exe=d/name;c.write_text(text)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
        p=subprocess.run([str(exe)],capture_output=True,text=True)
        if name=='production':assert p.returncode==0,p.stderr;print(p.stdout,end='')
        else:assert p.returncode!=0,f'surviving mutant {name}'
print('PASS 9 production mutants rejected')
