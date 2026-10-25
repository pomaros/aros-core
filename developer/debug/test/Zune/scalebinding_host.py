#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Compile actual expected-context metadata/Open/Show guards and real MUI tags.

Graphics/Exec/native layer construction remain external mocks. This exercises
actual publication/reveal actions and lock ordering, not only MUI show counters.
"""
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[4]
def function(text,name):
    # Match a definition, never its forward declaration; formatting is not API.
    matches=list(re.finditer(r'^[ \t]*[^;\n{}]*\b'+re.escape(name)+r'\s*\([^;{}]*?\)\s*\{',text,re.M))
    if len(matches)!=1:raise AssertionError((name,'expected one definition',len(matches)))
    a=matches[0].start();b=text.index('{',matches[0].start());n=1;i=b+1
    while n:n+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[a:i]
assert function('static VOID probe(void);\nstatic VOID probe(\n void\n)\n{ return; }','probe').count('probe')==1
opened=(root/'rom/intuition/openwindow.c').read_text()
show=(root/'rom/intuition/showwindow.c').read_text()
window=(root/'workbench/libs/muimaster/classes/window.c').read_text()
binding=(root/'rom/intuition/rendercontextbinding.h').read_text()
metadata=opened[opened.index('    IW(w)->ExpectedRenderContext = expectedcontext;'):opened.index('    IPTR        userbuffersize;')]
validation=opened[opened.index('    if (hasrendercontext &&'):opened.index('    if (visualoverlay &&')]
wrapper=function(opened,'int_openwindow')
show_action=function(show,'int_showboundwindow')
show_public=show.split('    AROS_LIBFUNC_INIT',1)[1].split('    AROS_LIBFUNC_EXIT',1)[0]
tags=window[window.index('        WA_Flags,'):window.index('        data->wd_Title ?',window.index('        WA_Flags,'))]
head=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG;typedef int32_t LONG;typedef uint16_t UWORD;typedef uintptr_t IPTR;typedef int BOOL;typedef void VOID;typedef void *APTR;
#define TRUE 1
#define FALSE 0
#define RESOURCE_SCREEN 2
#define RESOURCE_WINDOW 1
#define WFLG_ACTIVATE 4096
#define WA_Flags 1
#define WA_Hidden 2
#define WA_RenderContext 3
#define DEBUG_SHOWWINDOW(...)
#define dprintf(...)
#define SANITY_CHECKR(w,r) if(!(w))return(r);
struct Screen;struct Window;
struct TextFont {unsigned height;};
struct IntDrawInfo {struct Screen *dri_Screen;struct TextFont *dri_Font;};
struct ScreenRenderContext {struct IntDrawInfo *src_LogicalDrawInfo,*src_PhysicalDrawInfo;};
struct Screen {const struct ScreenRenderContext *current;struct IntDrawInfo DInfo;};
struct Layer {BOOL visible;};
struct Requester {struct Requester *OlderRequest;struct Layer *ReqLayer;};
struct Window {struct Screen *WScreen;ULONG Flags;struct Layer *layer;struct Requester *FirstRequest;};
struct IntWindow {struct Window window;const struct ScreenRenderContext *ExpectedRenderContext;IPTR BoundWindowGeneration;BOOL ScaleAware;const struct ScreenRenderContext*FrameContext;struct IntDrawInfo FrameInfo;};
struct IntuitionBase {IPTR NextBoundWindowGeneration;void *LayersBase;};
#define IW(w) ((struct IntWindow *)(w))
#define IS(s) (s)
#define GetPrivIBase(b) (b)
#define WLAYER(w) ((w)->layer)
#define BLAYER(w) ((struct Layer*)NULL)
struct IntuiActionMsg {int unused;};
struct OpenWindowActionMsg {struct IntuiActionMsg msg;struct Window *window;BOOL invisible,success;};
struct ShowWindowActionMsg {struct IntuiActionMsg msg;struct Window *window,*other;const struct ScreenRenderContext *expected;IPTR generation;BOOL performed;};
struct LayersBase {int unused;};
static unsigned ibase_depth,layer_depth,constructed,revealed,checks,active,queued,synchronous;
static BOOL screen_exists=TRUE,window_exists=TRUE,fail_dispatch,pending_scale,fail_frame_init;
static int action_race;
static struct Screen *subject;static const struct ScreenRenderContext *next_context;
static void install_context(struct Screen *s,const struct ScreenRenderContext *ctx){s->current=ctx;s->DInfo=*ctx->src_PhysicalDrawInfo;}
static ULONG LockIBase(ULONG flags){assert(!flags);return ++ibase_depth;}
static void UnlockIBase(ULONG token){assert(token==ibase_depth&&ibase_depth&&!layer_depth);--ibase_depth;if(!ibase_depth&&pending_scale){pending_scale=FALSE;install_context(subject,next_context);}}
static void lock_screen(struct Screen *s){assert(s&&ibase_depth);++layer_depth;}
static void unlock_screen(struct Screen *s){assert(s&&ibase_depth&&layer_depth);--layer_depth;}
#define LOCK_REFRESH(s) ((void)LayersBase,lock_screen(s))
#define UNLOCK_REFRESH(s) ((void)LayersBase,unlock_screen(s))
static BOOL ResourceExisting(void *resource,ULONG type,struct IntuitionBase *base){assert(base&&ibase_depth);return resource&&(type==RESOURCE_SCREEN?screen_exists:window_exists);}
static const struct ScreenRenderContext *CurrentScreenRenderContext(struct Screen *s){assert(ibase_depth);return s->current;}
static const struct ScreenRenderContext *WindowFrameContext(struct Window*w){assert(ibase_depth&&layer_depth);return IW(w)->FrameContext;}
/* Explicit dependency boundary: actual frame allocation/cache/provider logic
 * is exercised by windowframe_host.py, not reproduced by this adapter. */
static BOOL InitializeWindowFrame(struct Window*w,struct Screen*s,const struct ScreenRenderContext*c,struct IntuitionBase*b){assert(b&&ibase_depth&&c);if(IW(w)->ExpectedRenderContext)assert(layer_depth);if(fail_frame_init)return FALSE;w->WScreen=s;IW(w)->FrameContext=c;IW(w)->FrameInfo=*(IW(w)->ScaleAware?c->src_PhysicalDrawInfo:c->src_LogicalDrawInfo);return TRUE;}
static struct IntDrawInfo *LogicalScreenDrawInfo(struct Screen *s){assert(ibase_depth);return s->current->src_LogicalDrawInfo;}
static struct TextFont *SafeReopenFont(struct IntuitionBase *base,struct TextFont **font){assert(base&&ibase_depth&&layer_depth);return *font;}
static struct Layer *WindowCompositionLayer(struct Window *w){return w->layer;}
static LONG host_visibility(struct Layer *layer,BOOL visible){assert(layer&&ibase_depth&&layer_depth);layer->visible=visible;++revealed;if(action_race==4){assert(subject->current!=next_context);pending_scale=TRUE;action_race=0;}return TRUE;}
#define ChangeLayerVisibility(l,v) ((void)LayersBase,host_visibility(l,v))
static void CheckLayers(struct Screen *s,struct IntuitionBase *base){assert(s&&base&&ibase_depth&&layer_depth);++checks;}
static BOOL IsWindowVisible(struct Window *w){assert(ibase_depth&&layer_depth);return w->layer->visible;}
static void int_showwindow(struct ShowWindowActionMsg *msg,struct IntuitionBase *base){(void)msg;(void)base;abort();}
static void int_openwindow_body(struct OpenWindowActionMsg *msg,struct IntuitionBase *IntuitionBase){(void)IntuitionBase;assert(ibase_depth&&layer_depth);++constructed;msg->success=TRUE;msg->window->layer->visible=!msg->invisible;if(msg->window->Flags&WFLG_ACTIVATE)++active;}
static void DoSyncAction(APTR raw,struct IntuiActionMsg *msg,struct IntuitionBase *base){
 assert(!ibase_depth&&!layer_depth);++synchronous;if(fail_dispatch)return;
 struct ShowWindowActionMsg *show=(void*)msg;
 if(action_race==1){install_context(subject,next_context);action_race=0;}
 else if(action_race==2){window_exists=FALSE;memset(IW(show->window),0xa5,sizeof(struct IntWindow));action_race=0;}
 else if(action_race==3){++IW(show->window)->BoundWindowGeneration;action_race=0;}
 ((void(*)(struct ShowWindowActionMsg*,struct IntuitionBase*))raw)((void*)msg,base);
}
static BOOL DoASyncAction(APTR action,struct IntuiActionMsg *msg,size_t bytes,struct IntuitionBase *base){assert(action==(APTR)int_showwindow&&msg&&bytes==sizeof(struct ShowWindowActionMsg)&&base);assert(!ibase_depth&&!layer_depth);++queued;return TRUE;}
'''
# The production declaration is external; the fixture supplies that external OS service.
binding=binding.replace('const struct ScreenRenderContext *CurrentScreenRenderContext(struct Screen *screen);','')
bridge=r'''
static BOOL valid_inputs(const struct ScreenRenderContext *expectedcontext,BOOL hidden,ULONG flags){BOOL hasrendercontext=TRUE;BOOL windowinvisible=hidden;struct {ULONG Flags;} nw={flags};(void)nw;(void)windowinvisible;
'''+validation+r'''
 return TRUE;
exit:return FALSE;
}
static BOOL capture(struct IntWindow *window,struct Screen *screen,const struct ScreenRenderContext *expectedcontext,struct IntuitionBase *IntuitionBase){struct Window *w=&window->window;struct {struct Screen *Screen;} nw={screen};BOOL scaleaware=TRUE;window->ScaleAware=scaleaware;struct LayersBase*LayersBase=GetPrivIBase(IntuitionBase)->LayersBase;
'''+metadata+r'''
 return TRUE;
failexit:return FALSE;
}
static BOOL public_show(struct Window *window,struct Window *other,struct IntuitionBase *IntuitionBase){
'''+show_public+r'''
}
static void native_flags(struct Window *w,const struct ScreenRenderContext *context){ULONG flags=WFLG_ACTIVATE;struct {struct {const struct ScreenRenderContext *mri_RenderContext;}wd_RenderInfo;} instance={.wd_RenderInfo.mri_RenderContext=context};void *data=&instance;
'''
# The actual tag expression's private type is tiny here; preserve expressions verbatim.
bridge=bridge.replace('void *data=&instance;','__typeof__(instance) *data=&instance;')
bridge+='IPTR native_tags[]={'+tags+r'''0};assert(native_tags[0]==WA_Flags&&native_tags[2]==WA_Hidden&&native_tags[3]&&native_tags[4]==WA_RenderContext&&native_tags[5]==(IPTR)context);w->Flags=native_tags[1];assert(!(w->Flags&WFLG_ACTIVATE));}
'''
main=r'''
static void reset(void){assert(!ibase_depth&&!layer_depth);constructed=revealed=checks=active=queued=synchronous=0;screen_exists=window_exists=TRUE;fail_dispatch=pending_scale=fail_frame_init=FALSE;action_race=0;}
int main(void){
 struct IntuitionBase base={0};struct TextFont fontA={26},fontB={34};struct Screen screen={0},other={0};
 struct IntDrawInfo la={&screen,&fontA},pa={&screen,&fontA},lb={&screen,&fontB},pb={&screen,&fontB},foreign={&other,&fontA};
 struct ScreenRenderContext a={&la,&pa},b={&lb,&pb},bad={&foreign,&pa};subject=&screen;next_context=&b;
 assert(valid_inputs(&a,TRUE,0)&&!valid_inputs(NULL,TRUE,0)&&!valid_inputs(&a,FALSE,0)&&!valid_inputs(&a,TRUE,WFLG_ACTIVATE));
 reset();install_context(&screen,&a);struct Layer layer={0};struct IntWindow win={.window={.WScreen=&screen,.layer=&layer}};native_flags(&win.window,&a);assert(capture(&win,&screen,&a,&base));assert(win.FrameInfo.dri_Font==&fontA&&win.BoundWindowGeneration==1);
 struct OpenWindowActionMsg open={.window=&win.window,.invisible=TRUE};int_openwindow(&open,&base);assert(open.success&&constructed==1&&!layer.visible&&!active);
 assert(public_show(&win.window,NULL,&base));assert(layer.visible&&revealed==1&&checks==1&&synchronous==1&&!queued);assert(WindowBoundRenderContext(&win.window)==&a);
 reset();layer.visible=FALSE;install_context(&screen,&b);assert(!capture(&win,&screen,&a,&base));install_context(&screen,&a);assert(capture(&win,&screen,&a,&base));install_context(&screen,&b);open.success=FALSE;int_openwindow(&open,&base);assert(!open.success&&!constructed&&!layer.visible);install_context(&screen,&a);open.success=FALSE;int_openwindow(&open,&base);assert(open.success&&win.FrameInfo.dri_Font==&fontA&&!layer.visible);
 reset();layer.visible=FALSE;install_context(&screen,&a);assert(capture(&win,&screen,&a,&base));win.FrameContext=&b;open.success=FALSE;int_openwindow(&open,&base);assert(!open.success&&!constructed);win.FrameContext=&a;win.ExpectedRenderContext=&b;open.success=FALSE;int_openwindow(&open,&base);assert(!open.success&&!constructed);install_context(&screen,&b);win.FrameContext=&b;win.ExpectedRenderContext=&a;open.success=FALSE;int_openwindow(&open,&base);assert(!open.success&&!constructed);install_context(&screen,&a);win.ScaleAware=FALSE;win.ExpectedRenderContext=&a;win.FrameContext=&b;open.success=FALSE;int_openwindow(&open,&base);assert(open.success&&constructed==1);win.ScaleAware=TRUE;
 reset();install_context(&screen,&a);fail_frame_init=TRUE;assert(!capture(&win,&screen,&a,&base)&&!constructed);fail_frame_init=FALSE;
 reset();layer.visible=FALSE;assert(!capture(&win,&screen,&bad,&base));assert(capture(&win,&screen,&a,&base));IPTR sequence=win.BoundWindowGeneration;assert(!public_show(&win.window,(void*)1,&base)&&!synchronous);
 for(int race=1;race<=4;++race){reset();memset(&win,0,sizeof(win));win.window.WScreen=&screen;win.window.layer=&layer;layer.visible=FALSE;install_context(&screen,&a);assert(capture(&win,&screen,&a,&base));action_race=race;BOOL shown=public_show(&win.window,NULL,&base);
  if(race<4)assert(!shown&&!revealed&&!checks);else assert(shown&&layer.visible&&revealed==1&&checks==1&&screen.current==&b&&win.ExpectedRenderContext==&a);
 }
 reset();memset(&win,0,sizeof(win));win.window.WScreen=&screen;win.window.layer=&layer;install_context(&screen,&a);assert(capture(&win,&screen,&a,&base));fail_dispatch=TRUE;assert(!public_show(&win.window,NULL,&base)&&!revealed);
 reset();win.ExpectedRenderContext=NULL;assert(public_show(&win.window,NULL,&base)&&queued==1&&!synchronous);
 reset();base.NextBoundWindowGeneration=(IPTR)-1;assert(!capture(&win,&screen,&a,&base));assert(base.NextBoundWindowGeneration==(IPTR)-1);(void)sequence;
 puts("PASS actual108+111 binding: native hidden/nonactivating tags, constructor metadata generation, A/B/A, independent Frame/Expected Open guards and frame-preparation refusal, synchronous exact-context reveal, stale/closed/reused-address rejection, lock ordering through reveal, ordinary async preserved, failure/counter-wrap refusal");
}
'''
text=head+binding+'\n'+wrapper+'\n'+show_action+'\n'+bridge+'\n'+main
mutants=[('constructor-metadata','!BoundScreenRenderContext(expectedcontext, nw.Screen)','0'),('open-publication','(!expected || BoundScreenRenderContext(expected, screen))','TRUE'),('frame-publication','WindowFrameContext(window) == CurrentScreenRenderContext(screen)','TRUE'),('show-current','if (BoundScreenRenderContext(msg->expected, screen))','if (1)'),('show-generation','IW(window)->BoundWindowGeneration == msg->generation','1'),('hidden-input','!windowinvisible','0'),('activate-input','(nw.Flags & WFLG_ACTIVATE)','0'),('native-flags','flags & ~WFLG_ACTIVATE','flags'),('native-hidden','WA_Hidden, (IPTR) TRUE','WA_Hidden, (IPTR) FALSE'),('missing-IBase','ULONG lock = LockIBase(0);','ULONG lock = 0;')]
with tempfile.TemporaryDirectory(prefix='scalebinding-') as d:
    d=Path(d)
    for name,a,b in mutants:assert a in text,name+' no longer matches actual source'
    for name,program in [('production',text)]+[(n,text.replace(a,b)) for n,a,b in mutants]:
        c=d/(name+'.c');exe=d/name;c.write_text(program)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
        p=subprocess.run([str(exe)],capture_output=True,text=True)
        if name=='production':assert p.returncode==0,p.stdout+p.stderr;print(p.stdout,end='')
        else:assert p.returncode!=0,name+' survived';print('REJECT mutant '+name)
