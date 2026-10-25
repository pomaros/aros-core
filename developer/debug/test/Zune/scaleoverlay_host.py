#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual managed-card lifecycle, pixel preparation and visual/input contract.

Exec/Intuition/Graphics services are mocked; this proves producer ownership and
API requests, not the global Layers transaction or a native display result.
"""
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[4]
drag=(root/'workbench/libs/muimaster/dragndrop.c').read_text()
for name in ['dragpixels.h','dragcard.h']:
    drag=drag.replace('#include "'+name+'"',(root/'workbench/libs/muimaster'/name).read_text())
drag=re.sub(r'^#include[^\n]*\n','',drag,flags=re.M)
policy=(root/'rom/intuition/visualoverlay.h').read_text()
which=(root/'rom/hyperlayers/whichlayer.c').read_text().split('    AROS_LIBFUNC_INIT',1)[1].split('    AROS_LIBFUNC_EXIT',1)[0]
openwindow=(root/'rom/intuition/openwindow.c').read_text()
layerflags=openwindow.split('    LONG layerflags = ',1)[1].split(';',1)[0]
activation=(root/'rom/intuition/activatewindow.c').read_text()
activation=activation[activation.index('    if (window && WLAYER(window)'):]
activation=activation[:activation.index('return;')+len('return;')]
assert openwindow.index('!ValidVisualOverlay(')<openwindow.index('    IW(w)->ScaleAware')
# External services use real public bit values and production list traversal.
public=(root/'compiler/include/intuition/intuition.h').read_text(errors='surrogateescape')
flags='\n'.join(re.findall(r'^#define WFLG_\w+\s+[^\n]+',public,re.M))
layerpublic=(root/'compiler/include/graphics/layers.h').read_text(errors='surrogateescape')
flags+='\n'+'\n'.join(re.findall(r'^#define (?:LAYERPROTECTED|LAYERINPUTTRANSPARENT)\s+[^\n]+',layerpublic,re.M))
support=(root/'workbench/libs/muimaster/support.c').read_text()
def function(text,start):
    a=text.index(start);b=text.index('{',a);n=1;i=b+1
    while n:n+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[a:i]
listfunctions=function(support,'void *Node_Next(')+'\n'+function(support,'void *List_First(')
head=r'''
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG;typedef int32_t LONG;typedef int16_t WORD;typedef uint16_t UWORD;typedef uint8_t UBYTE;typedef uint64_t UQUAD;typedef int64_t QUAD;typedef uintptr_t IPTR;typedef void *APTR;typedef int BOOL;typedef void VOID;
#define TRUE 1
#define FALSE 0
#define MEMF_CLEAR 1
#define MEMF_ANY 0
#define RECTFMT_ARGB 1
#define BMA_WIDTH 1
#define BMA_HEIGHT 2
#define BACKGROUNDPEN 0
#define SHINEPEN 1
#define SHADOWPEN 2
#define GUI_BitMap 1
#define GUI_Mask 2
#define GUI_LeftOffset 3
#define GUI_TopOffset 4
#define GUI_Width 5
#define GUI_Height 6
#define GUI_SourceAlpha 7
#define GUI_IntegerScale 8
#define D(...)
#define IS_VISIBLE(l) ((l)->visible)
#define WLAYER(w) ((w)->WLayer)
enum {TAG_DONE=0,WA_CustomScreen=1,WA_Left,WA_Top,WA_Width,WA_Height,WA_Borderless,WA_ToolBox,WA_SmartRefresh,WA_ScaleAware,WA_VisualOverlay,WA_Hidden,WA_Activate,WA_IDCMP,WA_RenderContext};
struct MinNode {struct MinNode *mln_Succ,*mln_Pred;};
struct MinList {struct MinNode *mlh_Head,*mlh_Tail,*mlh_TailPred;};
#define Node MinNode
#define List MinList
struct BitMap {ULONG width,height;UBYTE pixels[16];};
struct Window;struct Screen;
struct RastPort {struct BitMap *BitMap;struct Window *owner;ULONG pen;};
struct Region {LONG left,top,right,bottom;};struct Hook {int unused;};
struct Layer {struct Layer *back;struct Window *Window;struct Region *visibleshape;UWORD Flags;BOOL visible;};
struct Layer_Info {struct Layer *top_layer;};
struct ScreenRenderContext;
struct Screen {struct {void *ColorMap;} ViewPort;const struct ScreenRenderContext *context;struct Layer_Info LayerInfo;ULONG scene;};
struct DrawInfo {struct Screen *dri_Screen;UWORD *dri_Pens;};
struct ScreenRenderContext {struct DrawInfo *src_PhysicalDrawInfo;};
struct Window {struct RastPort *RPort;struct Layer *WLayer;LONG LeftEdge,TopEdge,Width,Height;UBYTE pixels[1024];struct RastPort rp;struct Layer layer;struct Region shape;struct Screen *screen;const struct ScreenRenderContext *expected;};
struct NewWindow {ULONG Flags,IDCMPFlags;void *FirstGadget;};
struct TagItem {ULONG ti_Tag;IPTR ti_Data;};
static unsigned allocations,live,failat,nativeopens,nativecloses,showcalls,hidecalls,movecalls,failopen,failshow;
static const struct ScreenRenderContext *client_context_override;
static unsigned writes;
static BOOL failread,failwrite;static unsigned rpdepth,contextqueries;
static void *AllocMem(size_t n,ULONG flags){(void)flags;if(++allocations==failat)return NULL;void *p=calloc(1,n);assert(p);++live;return p;}
static void *AllocVec(size_t n,ULONG flags){return AllocMem(n,flags);}
static void FreeVec(void *p){if(p){assert(live);--live;free(p);}}
static void FreeMem(void *p,size_t n){(void)n;FreeVec(p);}
static void NewList(struct MinList *l){l->mlh_Head=(struct MinNode*)&l->mlh_Tail;l->mlh_Tail=NULL;l->mlh_TailPred=(struct MinNode*)&l->mlh_Head;}
static void AddTail(struct MinList *l,struct MinNode *n){n->mln_Pred=l->mlh_TailPred;n->mln_Succ=(struct MinNode*)&l->mlh_Tail;n->mln_Pred->mln_Succ=n;l->mlh_TailPred=n;}
static void Remove(struct MinNode *n){n->mln_Pred->mln_Succ=n->mln_Succ;n->mln_Succ->mln_Pred=n->mln_Pred;}
static void *RemTail(struct MinList *l){struct MinNode *n=l->mlh_TailPred;if(!n->mln_Pred)return NULL;Remove(n);return n;}
static ULONG GetBitMapAttr(struct BitMap *b,ULONG a){return a==BMA_WIDTH?b->width:b->height;}
static void InitRastPort(struct RastPort *rp){memset(rp,0,sizeof(*rp));++rpdepth;}
static void DeinitRastPort(struct RastPort *rp){(void)rp;assert(rpdepth);--rpdepth;}
static ULONG ReadPixelArray(void *p,ULONG sx,ULONG sy,ULONG stride,struct RastPort *rp,ULONG x,ULONG y,ULONG w,ULONG h,ULONG fmt){assert(!sx&&!sy&&!x&&!y&&stride==w*4&&fmt==RECTFMT_ARGB&&rp->BitMap&&!rp->owner);if(failread)return 0;assert(w==2&&h==2);memcpy(p,rp->BitMap->pixels,16);return w*h;}
static struct TagItem *NextTagItem(struct TagItem **p){struct TagItem *t=*p;if(!t->ti_Tag)return NULL;++*p;return t;}
static const struct ScreenRenderContext *zune_screen_context(struct Screen *s){++contextqueries;return client_context_override?client_context_override:s->context;}
static void GetRGB32(void *cm,ULONG first,ULONG n,ULONG *rgb){assert(cm&&first==0&&n==1);rgb[0]=0x20202020;rgb[1]=0x40404040;rgb[2]=0x60606060;}
static void SetAPen(struct RastPort *rp,ULONG pen){assert(rp->owner);rp->pen=pen;}
static void RectFill(struct RastPort *rp,LONG l,LONG t,LONG r,LONG b){assert(rp->owner&&l>=0&&t>=0&&r<rp->owner->Width&&b<rp->owner->Height);}
static ULONG WritePixelArray(void *pixels,ULONG sx,ULONG sy,ULONG stride,struct RastPort *rp,ULONG x,ULONG y,ULONG w,ULONG h,ULONG fmt){assert(!sx&&!sy&&stride==w*4&&fmt==RECTFMT_ARGB&&rp->owner&&!rp->BitMap);++writes;if(failwrite)return 0;struct Window *win=rp->owner;assert(x+w<=(ULONG)win->Width&&y+h<=(ULONG)win->Height);for(ULONG row=0;row<h;++row)memcpy(win->pixels+((y+row)*win->Width+x)*4,(UBYTE*)pixels+row*stride,w*4);return w*h;}
static BOOL IsPointInRegion(struct Region *r,LONG x,LONG y){return x>=r->left&&x<r->right&&y>=r->top&&y<r->bottom;}
'''
service=r'''
static struct Layer *hit(struct Layer_Info *li,LONG x,LONG y){
'''+which+r'''
}
struct OpenWindowActionMsg {BOOL visualoverlay,tileoverlay;};
static ULONG production_layer_flags(struct OpenWindowActionMsg *msg){return '''+layerflags+r''';}
static BOOL can_activate(struct Window *window){
'''+activation+r'''
 return TRUE;
}
static struct Window *host_open(IPTR *tags){
 struct Screen *s=NULL;const struct ScreenRenderContext *expected=NULL;LONG width=0,height=0;struct NewWindow nw={0};BOOL aware=0,hidden=0,overlay=0;
 for(;*tags;tags+=2)switch(tags[0]){case WA_CustomScreen:s=(void*)tags[1];break;case WA_RenderContext:expected=(void*)tags[1];assert(expected==s->context);break;case WA_Width:width=tags[1];break;case WA_Height:height=tags[1];break;case WA_Borderless:if(tags[1])nw.Flags|=WFLG_BORDERLESS;break;case WA_ToolBox:if(tags[1])nw.Flags|=WFLG_TOOLBOX;break;case WA_SmartRefresh:assert(tags[1]);break;case WA_ScaleAware:aware=tags[1];break;case WA_Hidden:hidden=tags[1];break;case WA_VisualOverlay:overlay=tags[1];break;case WA_Activate:if(tags[1])nw.Flags|=WFLG_ACTIVATE;break;case WA_IDCMP:nw.IDCMPFlags=tags[1];break;case WA_Left:case WA_Top:assert(!tags[1]);break;default:assert(0);}
 assert(s&&overlay&&width*height*4<=1024&&ValidVisualOverlay(&nw,aware,hidden,NULL,NULL,NULL,FALSE));
 if(++nativeopens==failopen)return NULL;
 struct Window *w=calloc(1,sizeof(*w));assert(w);w->Width=width;w->Height=height;w->RPort=&w->rp;w->rp.owner=w;w->WLayer=&w->layer;w->screen=s;w->expected=expected;w->shape=(struct Region){0,0,width,height};
 struct OpenWindowActionMsg message={overlay,0};w->layer.Flags=production_layer_flags(&message);w->layer.Window=w;w->layer.visibleshape=&w->shape;w->layer.back=s->LayerInfo.top_layer;s->LayerInfo.top_layer=&w->layer;assert(!can_activate(w));return w;
}
#define OpenWindowTags(dummy,...) host_open((IPTR[]){__VA_ARGS__})
static void CloseWindow(struct Window *w){assert(w);struct Layer **p=&w->screen->LayerInfo.top_layer;while(*p!=&w->layer){assert(*p);p=&(*p)->back;}*p=w->layer.back;++nativecloses;free(w);}
static void ChangeWindowBox(struct Window *w,LONG x,LONG y,LONG width,LONG height){assert(w&&width==w->Width&&height==w->Height);w->LeftEdge=x;w->TopEdge=y;w->shape=(struct Region){x,y,x+width,y+height};++movecalls;}
static BOOL ShowWindow(struct Window *w,struct Window *other){assert(w&&!other);if(++showcalls==failshow||w->expected!=w->screen->context)return FALSE;w->layer.visible=TRUE;return TRUE;}
static BOOL HideWindow(struct Window *w){assert(w);++hidecalls;w->layer.visible=FALSE;return TRUE;}
static void FinishDragNDrop(struct DragNDrop *dnd);
'''
# Production activation guard returns void; turn the wrapper's return into FALSE.
service=service.replace('LAYERINPUTTRANSPARENT)) return;','LAYERINPUTTRANSPARENT)) return FALSE;')
# Match public linkage in the complete production implementation.
service=service.replace('static void FinishDragNDrop(struct DragNDrop *dnd);','struct DragNDrop;\nvoid FinishDragNDrop(struct DragNDrop *dnd);')
main=r'''
static struct BitMap source={2,2,{0,255,0,0,128,0,200,255,255,255,30,80,64,200,100,0}};
static struct BitMapNode *make(void){struct TagItem tags[]={{GUI_BitMap,(IPTR)&source},{GUI_Width,2},{GUI_Height,2},{GUI_LeftOffset,1},{GUI_TopOffset,1},{GUI_IntegerScale,2},{GUI_SourceAlpha,1},{0,0}};return CreateBitMapNodeA(tags);}
static void reset(void){assert(!live&&!rpdepth);allocations=failat=nativeopens=nativecloses=showcalls=hidecalls=movecalls=failopen=failshow=contextqueries=0;failread=failwrite=FALSE;client_context_override=NULL;writes=0;}
int main(void){
 struct Screen screen={.ViewPort.ColorMap=(void*)1,.scene=123};UWORD pens[]={0,1,2};struct DrawInfo info={&screen,pens};struct ScreenRenderContext context={&info},changed_context={&info};screen.context=&context;
 struct Region underneath_shape={0,0,100,100};struct Layer underneath={.visibleshape=&underneath_shape,.visible=TRUE};screen.LayerInfo.top_layer=&underneath;
 reset();struct DragNDrop *d=CreateDragNDropA(NULL);struct BitMapNode *n=make();assert(d&&n);AttachBitMapNode(d,n);UBYTE original[64];memcpy(original,n->bmn_BitMapBuffer,64);assert(PrepareDragNDrop(d,&screen));assert(contextqueries==1&&nativeopens==1&&!showcalls&&n->bmn_Window&&!n->bmn_Window->WLayer->visible);
 for(ULONG y=0;y<4;++y)for(ULONG x=0;x<4;++x){UBYTE *src=original+(y*4+x)*4,*got=(UBYTE*)n->bmn_CardPixels+(y*4+x)*4;assert(got[0]==255);for(int c=0;c<3;++c){unsigned bg=(c+1)*32;assert(got[c+1]==(src[c+1]*src[0]+bg*(255-src[0])+127)/255);}}
 assert(!memcmp(original,n->bmn_BitMapBuffer,64));assert(DrawDragNDrop(d,15,20));assert(n->bmn_Window->LeftEdge==10&&n->bmn_Window->TopEdge==15&&showcalls==1&&movecalls==1);assert(hit(&screen.LayerInfo,15,20)==&underneath);assert(n->bmn_Window->WLayer->Flags==(LAYERPROTECTED|LAYERINPUTTRANSPARENT));
 assert(DrawDragNDrop(d,15,20)&&showcalls==2&&movecalls==2&&writes==1);
 screen.scene=231;screen.context=&changed_context;client_context_override=&context;assert(!DrawDragNDrop(d,15,20)&&showcalls==3&&movecalls==2&&writes==1);client_context_override=NULL;assert(!DrawDragNDrop(d,15,20)&&showcalls==3&&movecalls==2);assert(!memcmp(original,n->bmn_BitMapBuffer,64));UndrawDragNDrop(d);assert(hidecalls==1&&screen.scene==231);DeleteDragNDrop(d);assert(nativecloses==1&&screen.scene==231&&screen.LayerInfo.top_layer==&underneath&&!live);
 for(int fault=0;fault<4;++fault){reset();d=CreateDragNDropA(NULL);n=make();struct BitMapNode *n2=make();assert(d&&n&&n2);AttachBitMapNode(d,n);AttachBitMapNode(d,n2);unsigned baseline=live;if(fault==0)failat=allocations+1;if(fault==1)failopen=1;if(fault==2)failopen=2;if(fault==3)failwrite=TRUE;assert(!PrepareDragNDrop(d,&screen));assert(!d->dnd_Screen&&!n->bmn_Window&&!n2->bmn_Window&&!n->bmn_CardPixels&&!n2->bmn_CardPixels&&live==baseline&&!showcalls&&screen.scene==231&&screen.LayerInfo.top_layer==&underneath);DeleteDragNDrop(d);assert(!live);}
 reset();d=CreateDragNDropA(NULL);n=make();AttachBitMapNode(d,n);assert(PrepareDragNDrop(d,&screen));failshow=1;assert(!DrawDragNDrop(d,15,20));DeleteDragNDrop(d);assert(!live&&nativecloses==1&&screen.scene==231);
 reset();d=CreateDragNDropA(NULL);n=make();AttachBitMapNode(d,n);info.dri_Screen=NULL;assert(!PrepareDragNDrop(d,&screen)&&!nativeopens);info.dri_Screen=&screen;DeleteDragNDrop(d);assert(!live);
 struct NewWindow request={WFLG_BORDERLESS|WFLG_TOOLBOX,0,NULL};assert(ValidVisualOverlay(&request,TRUE,TRUE,NULL,NULL,NULL,FALSE));
 ULONG bad[]={WFLG_ACTIVATE,WFLG_GIMMEZEROZERO,WFLG_BACKDROP,WFLG_SIMPLE_REFRESH,WFLG_SUPER_BITMAP,WFLG_SIZEGADGET,WFLG_DRAGBAR,WFLG_DEPTHGADGET,WFLG_CLOSEGADGET,WFLG_REPORTMOUSE,WFLG_WINDOWACTIVE};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){request.Flags|=bad[i];assert(!ValidVisualOverlay(&request,TRUE,TRUE,NULL,NULL,NULL,FALSE));request.Flags&=~bad[i];}
 assert(!ValidVisualOverlay(&request,FALSE,TRUE,NULL,NULL,NULL,FALSE));assert(!ValidVisualOverlay(&request,TRUE,FALSE,NULL,NULL,NULL,FALSE));assert(!ValidVisualOverlay(&request,TRUE,TRUE,&underneath,NULL,NULL,FALSE));assert(!ValidVisualOverlay(&request,TRUE,TRUE,NULL,&underneath_shape,NULL,FALSE));struct Hook hook={0};assert(!ValidVisualOverlay(&request,TRUE,TRUE,NULL,NULL,&hook,FALSE));assert(!ValidVisualOverlay(&request,TRUE,TRUE,NULL,NULL,NULL,TRUE));request.IDCMPFlags=1;assert(!ValidVisualOverlay(&request,TRUE,TRUE,NULL,NULL,NULL,FALSE));request.IDCMPFlags=0;request.FirstGadget=&request;assert(!ValidVisualOverlay(&request,TRUE,TRUE,NULL,NULL,NULL,FALSE));
 puts("PASS actual managed opaque drag: pixel oracle, hidden preparation, scene-only drawing, input passthrough, explicit activation veto, protected ownership, move/hide/close, allocation/open/write/show faults; no old-screen restoration after simulated scene change");
}
'''
text=head+flags+'\n'+policy+'\n'+listfunctions+'\n'+service+'\n'+drag+'\n'+main
mutants=[('shown-card-context','if (!ShowWindow(node->bmn_Window, NULL))','if (!node->bmn_Shown && !ShowWindow(node->bmn_Window, NULL))'),('input-hit','!(l->Flags & LAYERINPUTTRANSPARENT)','1'),('activation','(WLAYER(window)->Flags & LAYERINPUTTRANSPARENT)','0'),('unprotected','LAYERPROTECTED | LAYERINPUTTRANSPARENT','LAYERINPUTTRANSPARENT'),('live-before-prepare','WA_Hidden, TRUE','WA_Hidden, FALSE'),('close-leak','if (window) CloseWindow(window);','if (0 && window) CloseWindow(window);'),('wrong-hotspot','(QUAD)x - node->bmn_Left - DRAG_CARD_BORDER','(QUAD)x - node->bmn_Left'),('flatten-alpha','background[channel] * (255 - alpha)','background[channel] * 255')]
with tempfile.TemporaryDirectory(prefix='scaleoverlay-') as d:
    d=Path(d)
    for name,program in [('production',text)]+[(n,text.replace(a,b)) for n,a,b in mutants]:
        c=d/(name+'.c');exe=d/name;c.write_text(program)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
        p=subprocess.run([str(exe)],capture_output=True,text=True)
        if name=='production':assert p.returncode==0,p.stdout+p.stderr;print(p.stdout,end='')
        else:assert p.returncode!=0,name+' survived';print('REJECT mutant '+name)
