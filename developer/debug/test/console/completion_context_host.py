#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual completion capture/open/reveal helpers, raced contexts and refusals."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[4]
source=(root/'rom/filesys/console_handler/completion.c').read_text()
def function(marker):
    a=source.index(marker);b=source.index('{',a);end=b+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[a:end]
actual='\n'.join(function(x) for x in ['static LONG completion_logical_coord(','static APTR completion_visual_info(','static struct Window *completion_open_window(','static BOOL completion_publish_window('])
case=source.split('case IDCMP_CHANGESCALE:',1)[1].split('case IDCMP_CLOSEWINDOW:',1)[0]
actual+='\nstatic void actual_scale_event(int code,BOOL *cancelled,BOOL *accepted){BOOL scalechanged=FALSE,done=FALSE,doit=TRUE;switch(code){case IDCMP_CHANGESCALE:'+case+'default:break;}*cancelled=done&&scalechanged;*accepted=doit;}\n'
assert 'ng.ng_LeftEdge = win->BorderLeft + BORDER_X;' in source
assert 'ng.ng_TopEdge = win->BorderTop + BORDER_Y;' in source
assert 'WBor' not in function('static BOOL DoChooseReq(')
assert 'GetScreenDrawInfo' not in source and 'FreeScreenDrawInfo' not in source
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
typedef int BOOL;typedef int32_t LONG;typedef uint32_t ULONG;typedef int64_t QUAD;typedef uintptr_t IPTR;typedef uint16_t UWORD;typedef void *APTR;typedef void Object;
#define TRUE 1
#define FALSE 0
#define TAG_DONE 0
#define GTVI_RenderContext 1
#define GTVI_Logical 2
#define SA_RenderContext 164
#define IDCMP_CHANGESCALE 1
#define IDCMP_CLOSEWINDOW 2
#define IDCMP_RAWKEY 4
#define IDCMP_VANILLAKEY 8
#define LISTVIEWIDCMP 16
#define BUTTONIDCMP 32
#define WINDOW_FRONTMOST ((struct Window*)1)
enum {WA_CustomScreen=1,WA_Title,WA_CloseGadget,WA_DragBar,WA_DepthGadget,WA_Activate,WA_AutoAdjust,WA_Left,WA_Top,WA_InnerWidth,WA_InnerHeight,WA_IDCMP,WA_Hidden,WA_ScaleAware,WA_RenderContext};
struct TagItem {ULONG ti_Tag;IPTR ti_Data;};
struct Screen;struct DrawInfo {struct Screen *dri_Screen;int font;};
struct ScreenRenderContext {struct DrawInfo *src_LogicalDrawInfo,*src_PhysicalDrawInfo;UWORD src_LegacyScaleFactor;};
struct Screen {const struct ScreenRenderContext *current;};
struct Window {struct Screen *WScreen;const struct ScreenRenderContext *expected;int visible;};
struct Gadget {int unused;};struct filehandle {struct Window *window;};
struct VI {const struct ScreenRenderContext *context;};
static int live,queries,opens,shows,activated,refreshes,fault,race;
static struct Screen *subject;static const struct ScreenRenderContext *a,*b;
static BOOL GetAttr(ULONG tag,Object *object,IPTR *value){assert(tag==SA_RenderContext);queries++;*value=(IPTR)((struct Screen*)object)->current;return fault!=1;}
static APTR GetVisualInfoA(struct Screen *screen,struct TagItem *tags){assert(tags[0].ti_Tag==GTVI_RenderContext&&tags[0].ti_Data==(IPTR)a);assert(tags[1].ti_Tag==GTVI_Logical&&tags[1].ti_Data==TRUE);if(fault==2)return NULL;struct VI *vi=malloc(sizeof *vi);live++;vi->context=(void*)tags[0].ti_Data;if(race==1)screen->current=b;return vi;}
static void FreeVisualInfo(APTR info){assert(info);free(info);live--;}
static struct Window *OpenWindowTagList(void *unused,struct TagItem *tags){
    (void)unused;IPTR hidden=0,aware=99,activate=99,idcmp=0;struct Screen *screen=NULL;const struct ScreenRenderContext *ctx=NULL;
    for(unsigned i=0;tags[i].ti_Tag;i++)switch(tags[i].ti_Tag){case WA_CustomScreen:screen=(void*)tags[i].ti_Data;break;case WA_RenderContext:ctx=(void*)tags[i].ti_Data;break;case WA_Hidden:hidden=tags[i].ti_Data;break;case WA_ScaleAware:aware=tags[i].ti_Data;break;case WA_Activate:activate=tags[i].ti_Data;break;case WA_IDCMP:idcmp=tags[i].ti_Data;break;default:break;}
    assert(hidden&&aware==FALSE&&activate==FALSE&&(idcmp&IDCMP_CHANGESCALE)&&ctx==a&&screen==subject);
    opens++;if(fault==3||ctx!=screen->current)return NULL;
    struct Window *w=calloc(1,sizeof *w);live++;w->expected=ctx;w->WScreen=screen;return w;
}
static void CloseWindow(struct Window *w){assert(w);free(w);live--;}
static UWORD AddGList(struct Window *w,struct Gadget *g,UWORD pos,UWORD count,void *r){assert(w&&g&&pos==(UWORD)-1&&count==(UWORD)-1&&!r);return fault==4?(UWORD)-1:0;}
static void RefreshGList(struct Gadget *g,struct Window *w,void *r,UWORD count){(void)r;assert(g&&w&&count==(UWORD)-1);refreshes++;if(race==2)w->WScreen->current=b;}
static void GT_RefreshWindow(struct Window *w,void *r){assert(w&&!r);}
static BOOL ShowWindow(struct Window *w,struct Window *other){assert(!other);shows++;if(fault==5||w->expected!=w->WScreen->current)return FALSE;w->visible=TRUE;return TRUE;}
static void ActivateWindow(struct Window *w){assert(w->visible&&w->expected==w->WScreen->current);activated++;}
'''
suffix=r'''
int main(void){
    struct Screen screen={0},other={0};struct DrawInfo la={&screen,13},pa={&screen,26};
    struct ScreenRenderContext ca={&la,&pa,2},cb={&la,&pa,3};a=&ca;b=&cb;subject=&screen;
    struct Window owner={.WScreen=&screen,.expected=&ca},control={.WScreen=&other,.expected=&cb};struct filehandle fh={&owner};struct Gadget gadget={0};
    for(fault=0;fault<=5;fault++)for(race=0;race<=2;race++){
        const struct ScreenRenderContext *captured=(void*)1;screen.current=a;queries=opens=shows=activated=refreshes=0;
        APTR vi=completion_visual_info(&fh,&screen,&captured);
        assert(queries==1);
        if(!vi){assert(captured==NULL&&(fault==1||fault==2)&&!live);continue;}
        assert(captured==a&&captured->src_LogicalDrawInfo->font==13);
        struct Window *w=completion_open_window(&fh,&screen,captured,10,20,200,100);
        if(w){BOOL shown=completion_publish_window(&fh,w,&gadget);assert(shown==(fault!=4&&fault!=5&&race!=2));assert(activated==shown);CloseWindow(w);}
        else assert(fault==3||race==1);
        FreeVisualInfo(vi);assert(!live&&owner.expected==a&&fh.window==&owner&&control.expected==b);
    }
    for(ULONG factor=1;factor<=4;factor++){
        assert(completion_logical_coord(99,factor)==99/(LONG)factor);
        assert(completion_logical_coord(-1,factor)==-1);
        assert(completion_logical_coord(-(LONG)factor-1,factor)==-2);
    }
    BOOL cancelled=FALSE,accepted=TRUE;actual_scale_event(IDCMP_CHANGESCALE,&cancelled,&accepted);assert(cancelled&&!accepted);
    puts("completion-context-host PASS one-logical-snapshot=1 hidden-bound-open=1 exact-NULL-reveal=1 stale-open/show-refusal=1 allocation/add-failure=1 scale-cancel=1 actual-window-borders=1 parent/control-stable=1 no-borrowed-FreeScreenDrawInfo=1");
}
'''
variants=[('production',actual,True),('logical-view',actual.replace('{ GTVI_Logical, TRUE }','{ GTVI_Logical, FALSE }'),False),('visible-construction',actual.replace('{ WA_Hidden, TRUE }','{ WA_Hidden, FALSE }'),False),('wrong-domain',actual.replace('{ WA_ScaleAware, FALSE }','{ WA_ScaleAware, TRUE }'),False),('no-subscription',actual.replace('IDCMP_CHANGESCALE | IDCMP_CLOSEWINDOW','IDCMP_CLOSEWINDOW'),False),('frontmost-bound-show',actual.replace('ShowWindow(window, NULL)','ShowWindow(window, WINDOW_FRONTMOST)'),False),('stale-activate',actual.replace('if (!ShowWindow(window, NULL)) return FALSE;', '(void)ShowWindow(window, NULL);'),False),('pointer-physical',actual.replace('physical / factor;', 'physical;'),False),('scale-still-accepts',actual.replace('doit = FALSE;', 'doit = TRUE;'),False)]
with tempfile.TemporaryDirectory(prefix='completion-context-') as temporary:
    path=Path(temporary);unit=path/'fixture.c';binary=path/'fixture'
    for name,body,success in variants:
        unit.write_text(prefix+body+suffix)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
        r=subprocess.run([str(binary)],capture_output=True,text=True)
        if success:assert r.returncode==0,r.stderr;print(r.stdout,end='')
        else:assert r.returncode!=0,name+' escaped'
    print('completion-context-host mutants=8 rejected=8')
