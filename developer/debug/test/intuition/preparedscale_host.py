#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual Intuition coordinator, faulted dependency boundaries and install cuts.
The complete production windowscale.c executes. Dependency preparations are
ownership models, not a substitute for the separate actual Layers/decoration/
notification/composition fixtures. No pixel or Region algorithm is mocked here
and then presented as qualification of that algorithm.
"""
from pathlib import Path
import re, subprocess, tempfile
from scaleresult_fixture import result_definitions, result_helpers, detail_header
ROOT=Path(__file__).resolve().parents[4]
def read(p):return (ROOT/p).read_text()
def strip(s):return '\n'.join(l for l in s.splitlines() if not l.startswith('#include'))+'\n'
prefix=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t UBYTE;typedef int BOOL;typedef int32_t LONG;typedef uint64_t UQUAD; typedef uint32_t ULONG;typedef int16_t WORD;typedef uint16_t UWORD;typedef uintptr_t IPTR;typedef void*Msg;
#define TRUE 1
#define FALSE 0
#define bug(...) (++diagnostics)
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define SA_SCALE_MIN 100
#define WFLG_BORDERLESS 1
#define WFLG_GIMMEZEROZERO 4
#define BMA_DEPTH 1
#define BMF_CLEAR 1
#define LAYERREFRESH 2
#define LGT_REPLACE_SHAPE 1
#define LGT_RESOLVED_SHAPE 2
#define WDM_PREPARESCALESHAPE 5
struct Region{unsigned id;};struct Hook{unsigned id;};
struct TextFont{WORD tf_YSize,tf_XSize,tf_Baseline;int refs;};
struct TTextAttr{const char*name;UWORD size;ULONG style;};
struct RastPort{void *BitMap;struct TextFont*Font;WORD TxWidth,TxHeight,TxBaseline;};
struct Rectangle{WORD MinX,MinY,MaxX,MaxY;};
struct Layer_Info;struct Layer{struct Rectangle bounds;WORD Width,Height;ULONG Flags;struct Region*shaperegion;struct RastPort*rp;struct Layer_Info*LayerInfo;};
struct Layer_Info{int dummy;};
struct Requester{struct Requester*OlderRequest;struct Layer*ReqLayer;LONG LeftEdge,TopEdge,Width,Height;};
struct WindowScaleShapes;struct WindowCompositionScale;
struct DrawInfo{struct TextFont*dri_Font;};struct IntDrawInfo{struct TextFont*dri_Font;};
struct Screen;
struct Window{char titlebuffer[256];struct Window*NextWindow;struct Screen*WScreen;LONG LeftEdge,TopEdge,Width,Height,GZZWidth,GZZHeight;unsigned framevalue,requestervalue,activevalue;LONG BorderLeft,BorderRight,BorderTop,BorderBottom;WORD MouseX,MouseY,GZZMouseX,GZZMouseY;ULONG Flags;struct Requester*FirstRequest;struct Layer*wl,*bl;BOOL gzz,care,ScaleAware,CustomShape,ScaleGeometryEnabled;struct Hook DefaultWindowShapeHook;struct DrawInfo DecorDrawInfo;IPTR DecorUserBuffer;struct WindowScaleShapes*ScaleShapes;void*Composition;};
struct Screen{struct Screen*NextScreen;struct Window*FirstWindow;struct RastPort RastPort;struct Layer_Info LayerInfo;struct Layer*BarLayer;WORD BarHeight,MouseX,MouseY;};
struct LogicalScreenInfo{struct LogicalScreenInfo*previous;struct TextFont*base;};
struct ScreenRenderContext;
struct IntScreen{struct Screen Screen;struct ScreenRenderContext*RenderContext,*RenderGenerations;struct DrawInfo DInfo;struct TextFont*ScaleBaseFont;UWORD Scale;BOOL SysFont,ScaleSysFont;struct TTextAttr textattr;struct LogicalScreenInfo*LogicalInfo;void*WinDecorObj;};
struct GfxBase{int dummy;};struct LayersBase{int dummy;};
struct IntuitionBase{struct Screen*FirstScreen;struct GfxBase*GfxBase;struct LayersBase*LayersBase;};
#define IS(s) ((struct IntScreen*)(s))
#define IW(w) (w)
#define WLAYER(w) ((w)->wl)
#define BLAYER(w) ((w)->bl)
#define IS_GZZWINDOW(w) ((w)->gzz)
#define IS_DOCAREREFRESH(w) ((w)->care)
#define GetPrivIBase(b) (b)
#define LOCK_REFRESH(s) ((void)(s),++refreshlocks)
#define UNLOCK_REFRESH(s) ((void)(s),--refreshlocks)
struct LayerGeometryRequest{struct Layer*layer;struct Rectangle bounds;struct Region*shape;ULONG flags;struct Hook*resolved_hook;};
struct wdpPrepareScaleShape{ULONG MethodID;struct Window*wdp_Window;LONG wdp_Width,wdp_Height,wdp_BorderTop;struct TextFont*wdp_Font;IPTR wdp_UserBuffer;struct Region**wdp_Result;};
struct ScreenScaleMetrics{LONG BarHeight;};
static unsigned ordinal,failat,denied,live,hardtrap,cut_at,cut_count,cutting,aborted;
static unsigned events,draws,refreshlocks,ibaselocks,pixels,pixellease;
static unsigned pointerselected,release_refusal,diagnostics,awaregzz;
static void*allocate(size_t n){assert(!hardtrap);if(++ordinal==failat){denied++;return NULL;}void*p=calloc(1,n);assert(p);live++;return p;}
static void release(void*p){assert(!hardtrap);if(p){assert(live);--live;free(p);}}
#define AllocVec(n,f) allocate(n)
#define FreeVec(p) release(p)
static ULONG LockIBase(ULONG n){(void)n;ibaselocks++;return 1;}
static void UnlockIBase(ULONG n){assert(n&&ibaselocks);ibaselocks--;}
static void CloseFont(struct TextFont*f){assert(!hardtrap&&f&&f->refs>0);f->refs--;}
static void SetFont(struct RastPort*r,struct TextFont*f){r->Font=f;r->TxWidth=f->tf_XSize;r->TxHeight=f->tf_YSize;r->TxBaseline=f->tf_Baseline;}
static void DisposeRegion(struct Region*r){release(r);}
static IPTR DoMethodA(void*o,Msg m){assert(!hardtrap);(void)o;struct wdpPrepareScaleShape*p=m;assert(p->MethodID==WDM_PREPARESCALESHAPE);assert(p->wdp_Font->tf_YSize==(p->wdp_Window->ScaleAware?26:13));assert(p->wdp_BorderTop==(p->wdp_Window->ScaleAware?26:13));assert(p->wdp_UserBuffer==(p->wdp_Window->ScaleAware?200:100));*p->wdp_Result=allocate(sizeof(struct Region));return *p->wdp_Result!=NULL;}
static void ScreenFontTextAttr(struct TextFont*f,struct TTextAttr*a){*a=(struct TTextAttr){"face",f->tf_YSize,0};}
'''
# Every owned-caption snapshot must happen under IBase plus the screen
# LayerInfo lock, the lock that SetWindowTitles holds while it writes.
title_reader='    result->ssr_Window = (IPTR)window;'
helpers=result_helpers(ROOT);assert helpers.count(title_reader)==1
helpers=helpers.replace(title_reader,'    assert(ibaselocks && refreshlocks); ++titlereads;\n'+title_reader)
headers='static unsigned titlereads;\n__attribute__((destructor)) static void report_titlereads(void){printf("owned caption snapshots under IBase+LayerInfo: %u\\n",titlereads);}\n'+result_definitions(ROOT)+helpers+detail_header(ROOT)+strip(read('rom/intuition/windowfit.h'))+strip(read('rom/intuition/preparednotifications.h'))
lt=read('compiler/include/graphics/layertransaction.h')
refusal_struct=lt[lt.index('struct LayerGeometryRefusal\n{'):lt.index('};',lt.index('struct LayerGeometryRefusal\n{'))+2]+'\n'
boundaries=refusal_struct+r'''
/* A forced Layers refusal: the check, layer and flags the batch records. */
static ULONG refuse_check,refuse_flags,refuse_at;static struct Layer*refuse_layer;
static BOOL forced(struct LayerGeometryRefusal*r,ULONG where){if(refuse_at!=where||!r)return FALSE;memset(r,0,sizeof*r);r->lgr_Check=refuse_check;r->lgr_Flags=refuse_flags;r->lgr_Layer=refuse_layer;r->lgr_Pixels=LGR_PIXELS_STACK;r->lgr_Lease=LGR_LEASE_DRIVER;r->lgr_Driver=HIDD_PIXELS_REFUSE_SCANOUT;r->lgr_Value[0]=HIDD_SCANOUT_NOT_OWNED;memcpy(r->lgr_Class,"hidd.bitmap.efifb",18);return TRUE;}
struct WindowFitTransaction{struct Window*w[3];struct WindowFitGeometry old[3],next[3];ULONG count;};
static BOOL gate(void){assert(!hardtrap);if(++ordinal==failat){denied++;return FALSE;}return TRUE;}
BOOL WindowFitPreflight(struct Screen*s,ULONG scale,const struct WindowFitFrameMetrics*frames,ULONG framecount,const struct WindowLayoutFit*layout,struct WindowFitTransaction**out,struct ScreenScaleResult*diagnostic,struct IntuitionBase*b){(void)diagnostic;(void)b;assert(!layout&&framecount<=3);
 struct WindowFitTransaction*p=allocate(sizeof*p);*out=p;if(!p)return FALSE;
 for(struct Window*w=s->FirstWindow;w;w=w->NextWindow){unsigned i=p->count++;assert(i<3&&i<framecount&&frames[i].window==w);assert(frames[i].left==(w->ScaleAware?4:2)&&frames[i].top==(w->ScaleAware?26:13)&&frames[i].right==(w->ScaleAware?4:2)&&frames[i].bottom==(w->ScaleAware?4:2));p->w[i]=w;p->old[i]=(struct WindowFitGeometry){w->LeftEdge,w->TopEdge,w->Width,w->Height,0,0,0,0,0,0};p->next[i]=p->old[i];if(!w->ScaleAware&&scale!=100){p->next[i].width/=2;p->next[i].height/=2;}}
 return TRUE;}
ULONG WindowFitCount(const struct WindowFitTransaction*p){return p->count;}
BOOL WindowFitDesired(const struct WindowFitTransaction*p,ULONG i,struct Window**w,struct WindowFitGeometry*g){if(!gate())return FALSE;*w=p->w[i];*g=p->next[i];return TRUE;}
void WindowFitInstall(struct WindowFitTransaction*p,BOOL undo){for(ULONG i=0;i<p->count;i++){struct Window*w=p->w[i];struct WindowFitGeometry*g=undo?&p->old[i]:&p->next[i];w->LeftEdge=g->left;w->TopEdge=g->top;w->Width=g->width;w->Height=g->height;assert(w->BorderTop==(undo?13:(w->ScaleAware?26:13)));w->GZZWidth=w->Width-w->BorderLeft-w->BorderRight;w->GZZHeight=w->Height-w->BorderTop-w->BorderBottom;}}
void WindowFitFinish(struct WindowFitTransaction*p,BOOL undo,struct IntuitionBase*b){(void)undo;(void)b;release(p);}
static struct LogicalScreenInfo*PrepareLogicalScreenInfo(struct IntScreen*s,struct TextFont*f,struct IntuitionBase*b){(void)b;if(s->LogicalInfo->base==f)return s->LogicalInfo;struct LogicalScreenInfo*p=allocate(sizeof*p);if(p){p->base=f;f->refs++;}return p;}
static void CommitLogicalScreenInfo(struct IntScreen*s,struct LogicalScreenInfo*p){if(s->LogicalInfo!=p){p->previous=s->LogicalInfo;s->LogicalInfo=p;}}
static void RestoreLogicalScreenInfo(struct IntScreen*s,struct LogicalScreenInfo*p){s->LogicalInfo=p;}
static void DisposeLogicalScreenInfo(struct LogicalScreenInfo*p,struct IntuitionBase*b){(void)b;CloseFont(p->base);release(p);}
struct PreparedScreenScaleDecoration{struct Screen*s;struct ScreenScaleMetrics metrics;WORD old;BOOL installed;void*image;};
static struct PreparedScreenScaleDecoration*PrepareScreenScaleDecoration(struct Screen*s,struct TextFont*basefont,struct TextFont*f,ULONG scale,struct ScreenScaleResult*diagnostic,struct IntuitionBase*b){(void)diagnostic;(void)b;(void)basefont;(void)scale;struct PreparedScreenScaleDecoration*p=allocate(sizeof*p);if(!p)return NULL;p->s=s;p->old=s->BarHeight;p->metrics.BarHeight=f->tf_YSize;p->image=allocate(1);if(!p->image){release(p);return NULL;}return p;}
static const struct ScreenScaleMetrics*PreparedScreenScaleDecorationMetrics(struct PreparedScreenScaleDecoration*p){return &p->metrics;}
static void InstallScreenScaleDecoration(struct PreparedScreenScaleDecoration*p,BOOL undo){p->s->BarHeight=undo?p->old:p->metrics.BarHeight;p->installed=!undo;}
static void ReleaseScreenScaleDecoration(struct PreparedScreenScaleDecoration*p,struct IntuitionBase*b){(void)b;release(p->image);release(p);}

/* Immutable screen-context dependency ownership model. Actual font/image/cache
 * algorithms execute separately in screenrendercontext_host.py. */
struct ScreenRenderContext{struct ScreenRenderContext*next;struct Screen*owner;struct TextFont*base,*physical;UWORD scale;};
struct PreparedScreenRenderContext{struct IntScreen*screen;struct ScreenRenderContext*old,*next;BOOL linked;};
static struct ScreenRenderContext initialrender;
static const struct ScreenRenderContext*CurrentScreenRenderContext(struct Screen*s){return IS(s)->RenderContext;}
static BOOL BoundScreenRenderContext(const struct ScreenRenderContext*c,struct Screen*s){return c&&c->owner==s&&CurrentScreenRenderContext(s)==c;}
static struct PreparedScreenRenderContext*PrepareScreenRenderContext(struct IntScreen*s,struct TextFont*base,struct TextFont*physical,ULONG scale,const struct ScreenScaleMetrics*m,struct IntuitionBase*b){(void)m;(void)b;struct PreparedScreenRenderContext*p=allocate(sizeof*p);if(!p)return NULL;p->next=allocate(sizeof(*p->next));if(!p->next){release(p);return NULL;}p->screen=s;p->old=s->RenderContext;*p->next=(struct ScreenRenderContext){NULL,&s->Screen,base,physical,scale};base->refs++;physical->refs++;return p;}
static const struct ScreenRenderContext*PreparedScreenRenderContextView(struct PreparedScreenRenderContext*p){return p->next;}
static BOOL BindScreenScaleDecoration(struct PreparedScreenScaleDecoration*p,const struct ScreenRenderContext*c){assert(p&&c&&p->s==c->owner);return gate();}
static void InstallScreenRenderContext(struct PreparedScreenRenderContext*p,BOOL undo){if(undo){p->screen->RenderContext=p->old;if(p->linked){p->screen->RenderGenerations=p->next->next;p->linked=FALSE;}}else{if(!p->linked){p->next->next=p->screen->RenderGenerations;p->screen->RenderGenerations=p->next;p->linked=TRUE;}p->screen->RenderContext=p->next;}}
static void ReleaseScreenRenderContext(struct PreparedScreenRenderContext*p,BOOL committed,struct IntuitionBase*b){(void)b;assert(!pixellease&&p->linked==committed);if(!committed){CloseFont(p->next->base);CloseFont(p->next->physical);release(p->next);}release(p);}
/* Frame ownership model only. Actual typed decorator/image/font algorithms
 * are covered by windowframe_host.py and the theme fixtures. */
struct PreparedWindowFrame{struct Window*w;const struct ScreenRenderContext*context;LONG old[4],next[4];unsigned oldvalue;struct IntDrawInfo dri;IPTR buffer;BOOL layout;};
static struct PreparedWindowFrame*PrepareWindowFrame(struct Window*w,const struct ScreenRenderContext*c,struct ScreenScaleResult*r,struct IntuitionBase*b){(void)b;(void)r;struct PreparedWindowFrame*p=allocate(sizeof*p);if(p){p->w=w;p->context=c;p->old[0]=w->BorderLeft;p->old[1]=w->BorderTop;p->old[2]=w->BorderRight;p->old[3]=w->BorderBottom;memcpy(p->next,p->old,sizeof p->old);if(w->ScaleAware)for(unsigned i=0;i<4;i++)p->next[i]*=2;p->oldvalue=w->framevalue;p->dri.dri_Font=w->ScaleAware?c->physical:w->DecorDrawInfo.dri_Font;p->buffer=w->ScaleAware?200:100;}return p;}
static BOOL WindowFrameBorders(const struct PreparedWindowFrame*p,LONG*l,LONG*t,LONG*r,LONG*b){if(!gate())return FALSE;*l=p->next[0];*t=p->next[1];*r=p->next[2];*b=p->next[3];return TRUE;}
static BOOL PrepareWindowFrameLayout(struct PreparedWindowFrame*p,LONG w,LONG h,struct ScreenScaleResult*r,struct IntuitionBase*b){(void)b;(void)r;assert(w>0&&h>0&&!pixellease);if(!gate())return FALSE;p->layout=TRUE;return TRUE;}
static struct IntDrawInfo*PreparedWindowFrameDrawInfo(const struct PreparedWindowFrame*p){assert(p->layout);return (struct IntDrawInfo*)&p->dri;}
static IPTR PreparedWindowFrameUserBuffer(const struct PreparedWindowFrame*p){assert(p->layout);return p->buffer;}
static void InstallWindowFrame(struct PreparedWindowFrame*p,BOOL undo){assert(p&&p->layout&&pixellease);LONG*v=undo?p->old:p->next;p->w->BorderLeft=v[0];p->w->BorderTop=v[1];p->w->BorderRight=v[2];p->w->BorderBottom=v[3];p->w->framevalue=undo?p->oldvalue:200;}
static void ReleaseWindowFrame(struct PreparedWindowFrame*p,BOOL committed,struct IntuitionBase*b){(void)b;assert(!pixellease);if(committed)assert(p->layout&&p->w->framevalue==200);release(p);}
struct LayerGeometryBatch{ULONG count;struct LayerGeometryRequest request[16];struct Layer old[16];unsigned oldpixels;BOOL installed;struct LayerGeometryRefusal*refusal;};
static struct LayerGeometryBatch*PrepareLayerGeometryBatch(struct Layer_Info*i,struct LayerGeometryRequest*r,ULONG n,struct LayerGeometryRefusal*refusal){(void)i;assert(refusal);if(forced(refusal,1))return NULL;struct LayerGeometryBatch*p=allocate(sizeof*p);if(p){assert(n<16);p->count=n;p->refusal=refusal;p->oldpixels=pixels;pixellease++;for(ULONG j=0;j<n;j++){p->request[j]=r[j];p->old[j]=*r[j].layer;}}return p;}
static BOOL SealLayerGeometryBatch(struct LayerGeometryBatch*p){if(forced(p->refusal,3))return FALSE;return gate();}
static BOOL LayerGeometryNeedsRefresh(struct LayerGeometryBatch*p,struct Layer*l){(void)p;(void)l;return TRUE;}
static void CommitLayerGeometryBatch(struct LayerGeometryBatch*p){for(ULONG i=0;i<p->count;i++){struct Layer*l=p->request[i].layer;l->bounds=p->request[i].bounds;l->Width=l->bounds.MaxX-l->bounds.MinX+1;l->Height=l->bounds.MaxY-l->bounds.MinY+1;if(p->request[i].flags&LGT_REPLACE_SHAPE)l->shaperegion=p->request[i].shape;l->Flags|=LAYERREFRESH;}pixels=200;p->installed=TRUE;}
static void RollbackLayerGeometryBatch(struct LayerGeometryBatch*p){for(ULONG i=0;i<p->count;i++)*p->request[i].layer=p->old[i];pixels=p->oldpixels;p->installed=FALSE;}
static void FinishLayerGeometryBatch(struct LayerGeometryBatch*p){assert(!hardtrap&&!events&&p->installed);}
static void ReleaseLayerGeometryBatch(struct LayerGeometryBatch*p){assert(pixellease);pixellease--;release(p);}
/* Pointer dependencies model identity and lease order only; the real pointer
 * and pixel algorithms have separate actual-body fixtures. */
struct HIDD_CursorState {unsigned value;};struct HIDD_PixelMap {int dummy;};
static unsigned pointervalue=100;
struct PreparedScreenPointerScale {struct HIDD_CursorState proposed;unsigned before;BOOL bound,installed,noop;};
static struct PreparedScreenPointerScale *PrepareScreenPointerScale(struct Screen*s,ULONG scale,struct IntuitionBase*b){(void)s;(void)b;struct PreparedScreenPointerScale*p=allocate(sizeof*p);if(p){p->before=pointervalue;p->proposed.value=scale;p->noop=!pointerselected;}return p;}
static struct HIDD_CursorState *PreparedScreenPointerCursor(struct PreparedScreenPointerScale*p){return p->noop?NULL:&p->proposed;}
static struct HIDD_PixelMap *PrepareLayerGeometryCursor(struct LayerGeometryBatch*l,void*bitmap,struct HIDD_CursorState*p){(void)bitmap;assert(l&&p&&pixellease);static struct HIDD_PixelMap map;if(forced(l->refusal,2))return NULL;return gate()?&map:NULL;}
static BOOL BindScreenPointerScale(struct PreparedScreenPointerScale*p,struct HIDD_PixelMap*m){assert(pixellease);if(p->noop)return TRUE;if(!m||!gate())return FALSE;p->bound=TRUE;return TRUE;}
static void InstallScreenPointerScale(struct PreparedScreenPointerScale*p,BOOL undo){assert(p&&pixellease);if(p->noop)return;assert(p->bound);pointervalue=undo?p->before:p->proposed.value;p->installed=!undo;}
static struct PreparedScreenPointerScale *refused_pending;
static BOOL ReleaseScreenPointerScale(struct PreparedScreenPointerScale*p,BOOL committed,struct IntuitionBase*b){(void)b;assert(!pixellease&&(p->noop||p->installed==committed));if(release_refusal){refused_pending=p;return FALSE;}release(p);return TRUE;}
struct WindowCompositionScale{struct Window*w;WORD x,y,gx,gy;unsigned oldvalue;BOOL installed;};
static unsigned compositionvalue;
static struct WindowCompositionScale*PrepareWindowCompositionScale(struct Window*w,ULONG scale,const struct WindowFitGeometry*g,struct Region*shape,struct LayerGeometryRequest*r,struct IntuitionBase*b){(void)scale;(void)g;(void)shape;(void)b;struct WindowCompositionScale*p=allocate(sizeof*p);if(p){p->w=w;p->x=w->MouseX;p->y=w->MouseY;p->gx=w->GZZMouseX;p->gy=w->GZZMouseY;p->oldvalue=compositionvalue;*r=(struct LayerGeometryRequest){w->Composition,{2,2,40,40},NULL,0,NULL};}return p;}
static BOOL PrepareWindowCompositionPixels(struct WindowCompositionScale*p,struct LayerGeometryBatch*l,struct IntuitionBase*b){(void)p;(void)l;(void)b;return gate();}
static void SealWindowCompositionScale(struct WindowCompositionScale*p){(void)p;}
static void InstallWindowCompositionScale(struct WindowCompositionScale*p,BOOL undo){p->installed=!undo;compositionvalue=undo?p->oldvalue:200;p->w->MouseX=undo?p->x:3;p->w->MouseY=undo?p->y:4;p->w->GZZMouseX=undo?p->gx:5;p->w->GZZMouseY=undo?p->gy:6;}
static void ReleaseWindowCompositionScale(struct WindowCompositionScale*p,struct IntuitionBase*b){(void)b;release(p);}
struct PreparedNotifications{ULONG count;struct PreparedNotificationSpec specs[3];BOOL filled,sent;};
struct PreparedNotifications*PreparedNotificationsPrepare(const struct PreparedNotificationSpec*s,ULONG n,struct IntuitionBase*b){(void)b;struct PreparedNotifications*p=allocate(sizeof*p);if(p){assert(n<=3);p->count=n;if(n)memcpy(p->specs,s,n*sizeof*s);}return p;}
void PreparedNotificationsFill(struct PreparedNotifications*p,struct IntuitionBase*b){assert(!events);assert(IS(b->FirstScreen)->Scale==200&&IS(b->FirstScreen)->RenderContext->scale==200);p->filled=TRUE;}
void PreparedNotificationsEnqueue(struct PreparedNotifications*p,struct IntuitionBase*b){(void)b;assert(p->filled&&!p->sent&&draws);for(ULONG i=0;i<p->count;i++)assert(p->specs[i].flags&PNF_SCALE);events=p->count+1;p->sent=TRUE;}
void PreparedNotificationsRelease(struct PreparedNotifications*p,struct IntuitionBase*b){(void)b;release(p);}
static void WindowSizeHasChanged(struct Window*w,LONG x,LONG y,BOOL f,struct IntuitionBase*b){(void)w;(void)x;(void)y;(void)f;(void)b;assert(!hardtrap&&!events);draws++;}
static void DrawWindowRefresh(struct Window*w,struct IntuitionBase*b){(void)w;(void)b;assert(!hardtrap&&!events);draws++;}
static void int_refreshwindowframe(struct Window*w,LONG x,LONG y,struct IntuitionBase*b){(void)x;(void)y;(void)b;assert(!hardtrap&&!events&&w->framevalue==200);draws++;}
static void UpdateMouseCoords(struct Window*w){(void)w;assert(!hardtrap);}
static void int_CalcSkinInfo(struct Screen*s,struct IntuitionBase*b){(void)s;(void)b;assert(!hardtrap);}
static void DrawAuxiliaryLayerRefresh(struct Screen*s,struct IntuitionBase*b){(void)s;(void)b;assert(!hardtrap);}
static void RenderScreenBar(struct Screen*s,BOOL f,struct IntuitionBase*b){(void)s;(void)f;(void)b;assert(!hardtrap&&!events);draws++;}
static void WindowCompositionPresentScreen(struct Screen*s,struct IntuitionBase*b){(void)s;(void)b;assert(!hardtrap);}
'''
boundaries += r'''
/* Requester dependencies model owned resources and publication order only.
 * Actual painting/8+10-bit transport and gadget input have separate fixtures. */
struct PreparedSystemRequesterScale{struct Window*w;const struct ScreenRenderContext*context;struct Rectangle client;unsigned old;};
struct BitMap{LONG width,height;struct Window*owner;BOOL painted;};
static ULONG GetBitMapAttr(void*b,ULONG a){(void)b;assert(a==BMA_DEPTH);return 32;}
static struct BitMap*AllocBitMap(LONG w,LONG h,ULONG d,ULONG f,void*b){(void)b;assert(d==32&&f==BMF_CLEAR);struct BitMap*p=allocate(sizeof(*p));if(p){p->width=w;p->height=h;}return p;}
static void FreeBitMap(struct BitMap*p){release(p);}
static void InitRastPort(struct RastPort*p){memset(p,0,sizeof(*p));}
static BOOL PrepareSystemRequesterScale(struct Window*w,const struct ScreenRenderContext*c,const struct WindowFitGeometry*g,LONG l,LONG t,LONG r,LONG b,struct PreparedSystemRequesterScale**out,struct IntuitionBase*base){
 (void)base;*out=NULL;if(!w->ScaleAware||w->gzz)return TRUE;
 struct PreparedSystemRequesterScale*p=allocate(sizeof(*p));if(!p)return FALSE;
 *p=(struct PreparedSystemRequesterScale){w,c,{l,t,g->width-r-1,g->height-b-1},w->requestervalue};*out=p;return TRUE;
}
static BOOL SystemRequesterScaleBounds(const struct PreparedSystemRequesterScale*p,struct Rectangle*r){assert(p);*r=p->client;return TRUE;}
static BOOL PaintSystemRequesterScale(struct PreparedSystemRequesterScale*p,struct RastPort*rp,struct IntuitionBase*base){(void)base;assert(!hardtrap&&p&&rp->BitMap&&pixellease);struct BitMap*b=rp->BitMap;b->owner=p->w;b->painted=TRUE;return gate();}
static BOOL WriteLayerGeometryBitmap(struct LayerGeometryBatch*batch,struct Layer*layer,struct BitMap*b,const struct Rectangle*r){assert(!hardtrap&&batch&&pixellease&&b->painted&&layer==b->owner->wl&&r->MinX==4&&r->MinY==26&&r->MaxX<b->width&&r->MaxY<b->height);return gate();}
static void InstallSystemRequesterScale(struct PreparedSystemRequesterScale*p,BOOL rollback){if(!p)return;assert(p->w->framevalue==(rollback?100:200));p->w->requestervalue=rollback?p->old:200;}
static void FinishSystemRequesterScale(struct PreparedSystemRequesterScale*p,BOOL committed,struct IntuitionBase*base){(void)base;assert(!hardtrap&&!pixellease&&p->w->requestervalue==(committed?200:p->old));release(p);}
'''
boundaries += r'''
/* Active input cache internals have a separate actual production fixture. */
struct PreparedActiveGadgetScale{struct Window*w;unsigned old;};
static BOOL PrepareActiveGadgetScale(struct Window*w,struct DrawInfo*d,const struct WindowFitGeometry*g,LONG l,LONG t,LONG r,LONG b,struct PreparedActiveGadgetScale*out,struct IntuitionBase*base){(void)d;(void)g;(void)l;(void)t;(void)r;(void)b;(void)base;if(!w->ScaleAware){out->w=NULL;return TRUE;}out->w=w;out->old=w->activevalue;return gate();}
static void InstallActiveGadgetScale(struct PreparedActiveGadgetScale*p,BOOL rollback){if(!p->w)return;assert(p->w->framevalue==(rollback?100:200));assert(p->w->GZZWidth==(rollback?76:72));p->w->activevalue=rollback?p->old:200;}
'''
main=r'''
static struct IntScreen screen;static struct Window windows[3];static struct Layer layers[8];static struct RastPort barrp;
static struct TextFont fonts[3];static struct LogicalScreenInfo initiallogical;static struct Requester requester;static struct IntuitionBase base;
struct Snapshot{struct IntScreen screen;struct Window windows[3];struct Layer layers[8];struct RastPort bar;int refs[3];unsigned allocs;};
static void setup(unsigned n,BOOL changebase){memset(&screen,0,sizeof screen);memset(windows,0,sizeof windows);memset(layers,0,sizeof layers);memset(&barrp,0,sizeof barrp);memset(&initiallogical,0,sizeof initiallogical);memset(&base,0,sizeof base);memset(&requester,0,sizeof requester);
 fonts[0]=(struct TextFont){13,7,10,4};fonts[1]=(struct TextFont){26,14,20,1};fonts[2]=(struct TextFont){15,8,11,changebase?1:0};if(!changebase)fonts[0].refs++;screen.DInfo.dri_Font=screen.ScaleBaseFont=&fonts[0];screen.Scale=100;initialrender=(struct ScreenRenderContext){NULL,&screen.Screen,&fonts[0],&fonts[0],100};screen.RenderContext=&initialrender;screen.SysFont=screen.ScaleSysFont=TRUE;screen.textattr=(struct TTextAttr){"face",13,0};screen.Screen.BarHeight=13;screen.Screen.MouseX=20;screen.Screen.MouseY=22;SetFont(&screen.Screen.RastPort,&fonts[0]);barrp=screen.Screen.RastPort;initiallogical.base=&fonts[0];screen.LogicalInfo=&initiallogical;screen.Screen.BarLayer=&layers[6];layers[6]=(struct Layer){.bounds={0,0,199,13},.Width=200,.Height=14,.rp=&barrp};
 for(unsigned i=0;i<n;i++){struct Window*w=&windows[i];w->NextWindow=i+1<n?&windows[i+1]:NULL;w->WScreen=&screen.Screen;w->LeftEdge=i*20;w->TopEdge=20+i*10;w->Width=80;w->Height=70;w->BorderLeft=w->BorderRight=w->BorderBottom=2;w->BorderTop=13;w->GZZWidth=76;w->GZZHeight=55;w->framevalue=w->requestervalue=w->activevalue=100;w->MouseX=10;w->MouseY=11;w->GZZMouseX=8;w->GZZMouseY=-2;w->wl=&layers[i];w->DecorDrawInfo.dri_Font=&fonts[0];w->care=TRUE;w->ScaleAware=i==1;w->gzz=i>1;layers[i]=(struct Layer){.bounds={i*20,20+i*10,i*20+79,20+i*10+69},.Width=80,.Height=70};if(i==1&&awaregzz){w->gzz=TRUE;w->bl=&layers[7];layers[7]=layers[i];}if(i==2){w->bl=&layers[3];layers[3]=layers[i];w->Composition=&layers[4];layers[4]=layers[i];requester=(struct Requester){NULL,&layers[5],3,4,10,12};w->FirstRequest=&requester;layers[5]=(struct Layer){.bounds={43,44,52,55},.Width=10,.Height=12};}}
 if(n){for(unsigned g=0;g<3;g++){struct WindowScaleShapes*p=allocate(sizeof(*p));p->previous=windows[0].ScaleShapes;p->outer=allocate(sizeof(struct Region));windows[0].ScaleShapes=p;}layers[0].shaperegion=windows[0].ScaleShapes->outer;}
 screen.Screen.FirstWindow=n?windows:NULL;base.FirstScreen=&screen.Screen;assert(!pixellease);release_refusal=diagnostics=0;refused_pending=NULL;pointervalue=pixels=compositionvalue=100;events=draws=refreshlocks=ibaselocks=ordinal=denied=0;hardtrap=cutting=aborted=cut_count=0;
}
static void snapshot(struct Snapshot*s){s->screen=screen;memcpy(s->windows,windows,sizeof windows);memcpy(s->layers,layers,sizeof layers);s->bar=barrp;for(unsigned i=0;i<3;i++)s->refs[i]=fonts[i].refs;s->allocs=live;}
static void unchanged(struct Snapshot*s){assert(!memcmp(&s->screen,&screen,sizeof screen));assert(!memcmp(s->windows,windows,sizeof windows));assert(!memcmp(s->layers,layers,sizeof layers));assert(!memcmp(&s->bar,&barrp,sizeof barrp));for(unsigned i=0;i<3;i++)assert(s->refs[i]==fonts[i].refs);assert(live==s->allocs&&!events&&!draws&&pixels==100&&compositionvalue==100&&pointervalue==100&&!pixellease&&!refreshlocks&&!ibaselocks);}
static void cleanup(void){while(screen.RenderGenerations){struct ScreenRenderContext*c=screen.RenderGenerations;screen.RenderGenerations=c->next;CloseFont(c->base);CloseFont(c->physical);release(c);}for(unsigned i=0;i<3;i++)FreeWindowScaleShapes(&windows[i],&base);while(screen.LogicalInfo!=&initiallogical){struct LogicalScreenInfo*p=screen.LogicalInfo;screen.LogicalInfo=p->previous;DisposeLogicalScreenInfo(p,&base);}assert(!live);}
int main(void){unsigned faults=0,cuts=0;struct ScreenScaleResult result;
 for(pointerselected=0;pointerselected<2;pointerselected++)for(unsigned n=0;n<=3;n+=3)for(unsigned cb=0;cb<2;cb++)for(awaregzz=0;awaregzz<2;awaregzz++){
 unsigned attempts=0,totalcuts=0;for(unsigned f=0;;f++){setup(n,cb);struct Snapshot before;snapshot(&before);failat=f;
 BOOL ok=ApplyPreparedScreenScale(&screen,&fonts[1],cb?&fonts[2]:&fonts[0],200,NULL,&result,&base);unsigned used=ordinal;failat=0;
 if(!f){assert(ok&&result.ssr_Status==SSR_SUCCESS&&result.ssr_RequestedScale==200&&result.ssr_AppliedScale==200&&!result.ssr_Window&&!result.ssr_WindowTitle[0]&&!result.ssr_MinHeight);attempts=used;totalcuts=cut_count;assert(pointervalue==(pointerselected?200:100)&&!pixellease);assert(events==n+1&&screen.Scale==200&&screen.DInfo.dri_Font==&fonts[1]);assert(screen.Screen.RastPort.TxHeight==26&&barrp.TxHeight==26);assert(screen.Screen.BarHeight==26);if(n){assert(windows[0].Width==40&&windows[1].Width==80&&windows[2].Width==40);assert(windows[0].ScaleShapes&&!windows[0].ScaleShapes->previous&&compositionvalue==200);assert(windows[1].activevalue==200);assert(windows[1].requestervalue==(awaregzz?100:200));assert(windows[1].BorderTop==26&&windows[1].GZZWidth==72&&windows[1].GZZHeight==40);if(awaregzz){assert(layers[1].bounds.MinX==24&&layers[1].bounds.MinY==56&&layers[1].Width==72&&layers[1].Height==40);}}assert(!ibaselocks&&!refreshlocks);cleanup();}
 else{assert(!ok&&denied==1&&result.ssr_Status==SSR_FAILED&&result.ssr_RequestedScale==200&&result.ssr_AppliedScale==100&&result.ssr_Stage!=SSR_STAGE_NONE&&result.ssr_Reason!=SSR_REASON_NONE);
  /* An unrecorded fault (an Intuition allocation, a model gate) never
     borrows a Layers cause: the receipt has no layer detail. */
  assert(result.ssr_Detail!=SSR_DETAIL_LAYER&&!result.ssr_LayerCheck);unchanged(&before);faults++;cleanup();}
 if(f==attempts)break;}
 for(unsigned c=1;c<=totalcuts;c++){setup(n,cb);struct Snapshot before;snapshot(&before);cutting=1;cut_at=c;
 assert(!ApplyPreparedScreenScale(&screen,&fonts[1],cb?&fonts[2]:&fonts[0],200,NULL,&result,&base));assert(result.ssr_Status==SSR_FAILED&&result.ssr_Stage==SSR_STAGE_COMMIT&&result.ssr_Reason==SSR_REASON_COMMIT_FAILED&&result.ssr_AppliedScale==100);assert(aborted&&cut_count==c);hardtrap=0;unchanged(&before);cleanup();cuts++;}
 }
 /* Each forced Layers refusal reaches the receipt with Intuition's role for
    the layer, the owning window's caption and the shared wording. */
 {struct Layer_Info canvas;struct Layer canvasroot={.LayerInfo=&canvas},stranger={0};
  struct{struct Layer*layer;ULONG flags,role;int window;ULONG at,stage;}cases[]={
   {&layers[0],0,SSR_LAYER_WINDOW,0,1,SSR_STAGE_LAYER_GEOMETRY},{&layers[3],0,SSR_LAYER_WINDOW_BORDER,2,1,SSR_STAGE_LAYER_GEOMETRY},
   {&layers[2],0,SSR_LAYER_WINDOW_CLIENT,2,1,SSR_STAGE_LAYER_GEOMETRY},{&layers[4],0,SSR_LAYER_PRESENTATION,2,1,SSR_STAGE_LAYER_GEOMETRY},
   {&layers[5],0,SSR_LAYER_REQUESTER,2,1,SSR_STAGE_LAYER_GEOMETRY},{&layers[6],0,SSR_LAYER_BAR,-1,3,SSR_STAGE_LAYER_GEOMETRY},
   {&stranger,LGRF_ROOT,SSR_LAYER_ROOT,-1,1,SSR_STAGE_LAYER_GEOMETRY},{&stranger,0,SSR_LAYER_OTHER,-1,1,SSR_STAGE_LAYER_GEOMETRY},
   {&layers[2],LGRF_PRIVATE,SSR_LAYER_CANVAS,2,1,SSR_STAGE_LAYER_GEOMETRY},{&canvasroot,LGRF_ROOT|LGRF_PRIVATE,SSR_LAYER_CANVAS_ROOT,0,1,SSR_STAGE_LAYER_GEOMETRY},
   {&stranger,LGRF_PRIVATE,SSR_LAYER_PRIVATE,-1,1,SSR_STAGE_LAYER_GEOMETRY},{&layers[6],0,SSR_LAYER_BAR,-1,2,SSR_STAGE_CURSOR},
   {NULL,0,SSR_LAYER_NONE,-1,1,SSR_STAGE_LAYER_GEOMETRY}};
  for(unsigned c=0;c<sizeof cases/sizeof*cases;c++){setup(3,FALSE);pointerselected=1;strcpy(windows[0].titlebuffer,"Wanderer");strcpy(windows[2].titlebuffer,"AROS Shell");
   layers[0].LayerInfo=&canvas;struct Snapshot before;snapshot(&before);
   refuse_check=LGR_PIXELS;refuse_flags=cases[c].flags;refuse_layer=cases[c].layer;refuse_at=cases[c].at;diagnostics=0;
   assert(!ApplyPreparedScreenScale(&screen,&fonts[1],&fonts[0],200,NULL,&result,&base));refuse_at=0;
   assert(result.ssr_Status==SSR_FAILED&&result.ssr_Stage==cases[c].stage&&result.ssr_Detail==SSR_DETAIL_LAYER);
   assert(result.ssr_LayerRole==cases[c].role&&result.ssr_Layer==(IPTR)cases[c].layer&&result.ssr_LayerCheck==LGR_PIXELS);
   assert(result.ssr_LayerDriver==HIDD_PIXELS_REFUSE_SCANOUT&&result.ssr_LayerValue[0]==HIDD_SCANOUT_NOT_OWNED&&!strcmp((char*)result.ssr_DetailName,"hidd.bitmap.efifb"));
   assert(cases[c].window<0?!result.ssr_Window:result.ssr_Window==(IPTR)&windows[cases[c].window]);
   assert(cases[c].window<0?!result.ssr_WindowTitle[0]:!strcmp((char*)result.ssr_WindowTitle,windows[cases[c].window].titlebuffer));
   char text[320];assert(ScaleDetailFormat(&result,text,sizeof text));
   if(c==0)assert(!strcmp(text,"the window layer: the pixel lease of the screen bitmap was refused: 'hidd.bitmap.efifb' refused PreparePixels: the shown bitmap differs from the scanout: the boot console still owns the framebuffer"));
   if(cases[c].role==SSR_LAYER_NONE)assert(!strncmp(text,"the pixel lease",15));
   if(cases[c].role==SSR_LAYER_OTHER)assert(!strncmp(text,"layer 0x",8));
   assert(diagnostics==1);unchanged(&before);cleanup();}
  puts("PASS Layers refusal roles: window/border/client/presentation/requester/bar/root/other/canvas/canvas-root/private/none, prepare/cursor/seal stages, owned captions");}
 /* An impossible release refusal diagnoses and retains the pending owner.
    The model captures it explicitly so the fixture can safely tear it down. */
 setup(3,FALSE);pointerselected=1;release_refusal=1;
 assert(ApplyPreparedScreenScale(&screen,&fonts[1],&fonts[0],200,NULL,NULL,&base));
 assert(diagnostics==1&&refused_pending&&refused_pending->installed&&!pixellease);
 unsigned kept=live;release_refusal=0;
 assert(ReleaseScreenPointerScale(refused_pending,TRUE,&base)&&live==kept-1);refused_pending=NULL;cleanup();
 puts("PASS coordinator selected/nonselected pointer branches and release-refusal diagnostic/retention model");
 printf("PASS actual Intuition coordinator: zero/three windows, same/new baseline; %u preparation faults, %u install statement cuts; no premature notifications; exact metadata/geometry/raster-pointer/font-ref/generation rollback\n",faults,cuts);return 0;}
'''
body=strip(read('rom/intuition/windowscale.c'))
# Instrument actual reversible install after each assignment and each pure
# dependency publication. Rollback itself executes unmodified production code.
start=body.index('static void install_scale(');end=body.index('\nstatic void free_shape_chain',start)
install=body[start:end]
# Insert at statement ends, including two assignments on the same source line.
install=re.sub(r'(?m)([^\n;]*;)',lambda m:m.group(0)+(' INSTALL_CUT();' if ('=' in m.group(0) and not any(x in m.group(0) for x in ('for (','struct ','ULONG '))) else ''),install)
for call in ('InstallScreenScaleDecoration(prepared->decor, rollback);','InstallScreenRenderContext(prepared->render, rollback);','WindowFitInstall(prepared->fit, rollback);','InstallWindowFrame(prepared->windows[i].frame, rollback);','InstallSystemRequesterScale(prepared->windows[i].requester, rollback);','InstallActiveGadgetScale(&prepared->windows[i].active, rollback);','InstallScreenPointerScale(prepared->pointer, rollback);','CommitLogicalScreenInfo(is, prepared->logical);','CommitLayerGeometryBatch(prepared->layers);','InstallWindowCompositionScale(record->composition, rollback);'):
 # if (composition) statement needs braces when adding an extra statement.
 if call.startswith('InstallWindowComposition'):
  install=install.replace('if (record->composition) '+call,'if (record->composition) { '+call+' INSTALL_CUT(); }')
 else:install=install.replace(call,call+' INSTALL_CUT();')
# Body definition is recursive-visible in C. Cut abort unwinds before release;
# actual rollback knows partial logical/generation publication.
install=install.replace('static void install_scale(','''#define INSTALL_CUT() do { if (!rollback) { ++cut_count; if (cutting && cut_count == cut_at) { install_scale(prepared, TRUE, IntuitionBase); aborted=1; return; } } } while(0)
static void install_scale(''')
body=body[:start]+install+body[end:]
body=body.replace('/* BEGIN allocation-free install. Every possible failure is above. */','/* BEGIN allocation-free install. Every possible failure is above. */\n    hardtrap=1;')
body=body.replace('install_scale(&install, FALSE, IntuitionBase);','install_scale(&install, FALSE, IntuitionBase);\n    if (aborted) { hardtrap=0; goto done; }')
body=body.replace('committed = TRUE;','committed = TRUE; hardtrap=0;')
source=prefix+headers+boundaries+body+main
with tempfile.TemporaryDirectory(prefix='preparedscale-') as td:
 p=Path(td);c=p/'test.c';exe=p/'test';c.write_text(source)
 def buildrun(s):
  c.write_text(s)
  subprocess.run(['clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-unused-variable',str(c),'-o',str(exe)],check=True)
  return subprocess.run([str(exe)],capture_output=True,text=True)
 r=buildrun(source)
 if r.returncode:print(r.stdout+r.stderr);raise SystemExit(r.returncode)
 print(r.stdout,end='')
 assert re.search(r'owned caption snapshots under IBase\+LayerInfo: [1-9]',r.stdout),'no locked caption snapshot exercised'
 mutations={
  'active-cache-install-omitted':('InstallActiveGadgetScale(&prepared->windows[i].active, rollback);','(void)0;'),
  'active-cache-before-fit':('    WindowFitInstall(prepared->fit, rollback);','    for (i=0;i<prepared->count;++i) InstallActiveGadgetScale(&prepared->windows[i].active, rollback); WindowFitInstall(prepared->fit, rollback);'),
  'requester-install-omitted':('InstallSystemRequesterScale(prepared->windows[i].requester, rollback);','(void)0;'),
  'requester-lease-order':('if (layers) ReleaseLayerGeometryBatch(layers);','if (windows) for (i=0;i<count;++i) if (windows[i].requester) FinishSystemRequesterScale(windows[i].requester,committed,IntuitionBase); if (layers) ReleaseLayerGeometryBatch(layers);'),
  'old-gzz-border-offset':('g->top + (BLAYER(window) ? record->bordertop : 0)','g->top + (BLAYER(window) ? window->BorderTop : 0)'),
  'old-gzz-client-height':('g->height - record->bordertop - record->borderbottom','g->height - window->BorderTop - window->BorderBottom'),
  'lost-frame-install':('InstallWindowFrame(prepared->windows[i].frame, rollback);','(void)prepared->windows[i].frame;'),
  'lost-frame-rollback':('InstallWindowFrame(prepared->windows[i].frame, rollback);','InstallWindowFrame(prepared->windows[i].frame, FALSE);'),
  'wrong-proposed-shape-font':('message.wdp_Font = dri->dri_Font;','message.wdp_Font = IW(window)->DecorDrawInfo.dri_Font;'),
  'wrong-proposed-shape-border':('message.wdp_BorderTop = record->bordertop;','message.wdp_BorderTop = window->BorderTop;'),
  'lost-render-install':('InstallScreenRenderContext(prepared->render, rollback);','(void)prepared->render;'),
  'lost-render-rollback':('InstallScreenRenderContext(prepared->render, rollback);','InstallScreenRenderContext(prepared->render, FALSE);'),
  'noop-requires-map':('        if (proposed && !map)\n', '        if (!map)\n'),
  'release-before-lease-end':('if (layers) ReleaseLayerGeometryBatch(layers);', 'if (pointer) ReleaseScreenPointerScale(pointer, committed, IntuitionBase); if (layers) ReleaseLayerGeometryBatch(layers);'),
  'lost-pointer-install':('InstallScreenPointerScale(prepared->pointer, rollback);','(void)prepared->pointer;'),
  'lost-pointer-rollback':('InstallScreenPointerScale(prepared->pointer, rollback);','InstallScreenPointerScale(prepared->pointer, FALSE);'),
  'shape-history-leak':('free_shape_chain(windows[i].shapes->previous, IntuitionBase);','(void)0;'),
  'notification-before-frame':('if (committed) FinishLayerGeometryBatch(layers);','if (committed) { PreparedNotificationsFill(notifications, IntuitionBase); PreparedNotificationsEnqueue(notifications, IntuitionBase); FinishLayerGeometryBatch(layers); }'),
  'rollback-font':('rollback ? prepared->oldfont : prepared->font','prepared->font'),
  'rollback-pixels':('RollbackLayerGeometryBatch(prepared->layers);','(void)prepared->layers;'),
  'notification-before-install':('install_scale(&install, FALSE, IntuitionBase);','PreparedNotificationsFill(notifications, IntuitionBase); install_scale(&install, FALSE, IntuitionBase);'),
  'legacy-event-filter':('specs[i].flags |= PNF_SCALE;','if (IW(window)->ScaleAware) specs[i].flags |= PNF_SCALE;'),
  'lost-shape-unlink':('IW(window)->ScaleShapes = record->shapes->previous;','IW(window)->ScaleShapes = record->shapes;'),
  'layer-refusal-dropped':('        layer_refused(result, SSR_STAGE_LAYER_GEOMETRY, NULL, &refusal, screen, windows, count);\n        goto done;\n    }\n    if (pointer)','        goto done;\n    }\n    if (pointer)'),
  'presentation-unrecorded':('record->proxy = requests[*used - 1].layer;','(void)used;'),
  'canvas-as-window':('if (composed) return SSR_LAYER_CANVAS;','(void)composed;'),
  'gzz-roles-swapped':('return layer == BLAYER(window) ? SSR_LAYER_WINDOW_BORDER : SSR_LAYER_WINDOW_CLIENT;','return layer == BLAYER(window) ? SSR_LAYER_WINDOW_CLIENT : SSR_LAYER_WINDOW_BORDER;'),
  'root-without-flag':('return (refusal->lgr_Flags & LGRF_ROOT) ? SSR_LAYER_ROOT : SSR_LAYER_OTHER;','return SSR_LAYER_ROOT;'),
  'owner-caption-lost':('ScaleResultSetStage(result, stage, owner ? owner : current);','ScaleResultSetStage(result, stage, current);'),
  'seal-refusal-dropped':('    if (!SealLayerGeometryBatch(layers))\n    {\n        layer_refused(result, SSR_STAGE_LAYER_GEOMETRY, NULL, &refusal, screen, windows, count);','    if (!SealLayerGeometryBatch(layers))\n    {'),
 }
 for name,(old,new) in mutations.items():
  assert old in source,name
  r=buildrun(source.replace(old,new))
  if r.returncode==0:raise AssertionError('mutation accepted: '+name)
  print('REJECT '+name)
print('actual Intuition coordinator ASan/UBSan result=PASS')
