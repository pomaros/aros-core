#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# This file is part of AROS, distributed under the AROS Public License 1.1.
"""Actual pointer renderer/generation + actual HIDD cursor prepare/install bodies.
Exec/bitmap/BOOPSI are host adapters. Ordinary draw/erase are counters here;
the separate preparedpixels fixture executes the actual 32/30-bit renderer.
This test does not qualify native drawing, clicks, cursor/frame publication.
"""
from pathlib import Path
import re, subprocess, tempfile, sys
ROOT=Path(__file__).resolve().parents[4]
def read(p):return (ROOT/p).read_text(encoding='latin1')
def function(s,name):
    m=re.search(r'(?m)^.*\b'+re.escape(name)+r'\([^;]*?\)\s*\{',s);assert m,name
    a=m.start();i=s.index('{',m.start())+1;depth=1
    while depth:
        if s[i]=='{':depth+=1
        elif s[i]=='}':depth-=1
        i+=1
    return s[a:i]+'\n'
state=read('rom/hidds/gfx/include/preparedcursor.h');state=re.sub(r'^#include[^\n]*\n','',state,flags=re.M)
engine=read('rom/intuition/pointerscale.c');engine=engine[engine.index('struct PointerRenderGeneration'):]
gfx=read('rom/hidds/gfx/gfx_displayclass.c')
gfx=''.join(function(gfx,n) for n in ['cursor_PrepareShape','Display__Hidd_Display__SupportsPreparedCursor','cursor_PreparedDeclared','Display__Hidd_Display__PrepareCursorShape','Display__Hidd_Display__ReleaseCursorShape','cursor_InstallShape','Display__Hidd_Display__InstallCursorShape','Display__Hidd_Display__SetCursorShape'])
prefix=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint8_t UBYTE;typedef uint16_t UWORD;typedef uint32_t ULONG;typedef int32_t LONG;
typedef uint64_t UQUAD;typedef int64_t QUAD;typedef intptr_t IPTR;typedef int BOOL;typedef void*APTR;typedef int OOP_MethodID;
#define TRUE 1
#define FALSE 0
typedef const char *CONST_STRPTR;
static unsigned diagnostics;
#define bug(...) (++diagnostics)
#define MEMF_CLEAR 1
#define MEMF_PUBLIC 2
#define MEMF_ANY 0
#define BMA_WIDTH 1
#define BMA_HEIGHT 2
#define BMA_DEPTH 3
#define BMF_DISPLAYABLE 1
#define BMF_SPECIALFMT 128
#define PIXFMT_ARGB32 11
#define POINTER_BUILTIN_NONE 0
#define POINTER_BUILTIN_ARROW 1
#define POINTER_BUILTIN_BUSY 2
#define POINTERA_SharedPointer 7
#define vHidd_StdPixFmt_ARGB32 8
#define aHidd_BitMap_Width 1
#define aHidd_BitMap_Height 2
#define aHidd_BitMap_PixFmt 3
#define aHidd_PixFmt_BytesPerPixel 4
struct Library{int unused;};struct GfxBase{int unused;};struct SignalSemaphore{int depth;};
typedef struct OOP_Class{void*csd;struct OOP_Class*super;BOOL declaresPreparedCursor;}OOP_Class;
typedef struct OOP_Object{OOP_Class*cl;int kind;void*data;}OOP_Object;
typedef OOP_Object Object;
#define OOP_OCLASS(o) ((o)->cl)
#define OOP_INST_DATA(cl,o) ((void)(cl),(o)->data)
#define INST_DATA(cl,o) ((void)(cl),(o)->data)
struct class_static_data{struct Library*cs_OOPBase;};
#define CSD(cl) ((struct class_static_data*)(cl)->csd)
static BOOL HIDD_Display_SupportsPreparedCursor(OOP_Object*d);
'''
prefix+=state
prefix+=r'''
struct HIDDDisplayData{struct HIDD_CursorState cursor;struct SignalSemaphore fbsem;};
struct BitMap{ULONG width,height,depth;BOOL hidd,alpha;UBYTE*pixels;OOP_Object object;};
struct ExtSprite{struct BitMap*es_BitMap;};
struct SharedPointer{struct ExtSprite*sprite;LONG xoffset,yoffset;int ref_count;BOOL physicalPixels;ULONG builtinShape;};
struct MonitorHandle{OOP_Object*display;};
struct PointerRenderGeneration;
struct Screen;
struct IMonitorNode{struct MonitorHandle*handle;struct SharedPointer*pointer;struct Screen*pointerScreen;struct PointerRenderGeneration*pointerRender;struct BitMap*tmpPtr;struct Screen*front;};
struct Screen{ULONG Scale;Object*IMonitorNode;struct SharedPointer*Pointer;};
struct Color32{ULONG red,green,blue;};
struct IntuitionBase{struct GfxBase*GfxBase;struct Library*OOPBase;OOP_MethodID ib_HiddBitMapBase,ib_HiddDisplayBase;void*monitorclass;Object*DefaultPointer;struct Color32 Colors[32];UWORD PointerAlpha;};
#define IntIntuitionBase IntuitionBase
#define GetPrivIBase(b) (b)
#define GetPrivScreen(s) (s)
#define IS_HIDD_BM(b) ((b)->hidd)
#define HIDD_BM_OBJ(b) (&(b)->object)
struct HIDD_PixelMap{struct HIDD_CursorState*cursorState;void*cursorCookie,*cursorSemaphore;};
static unsigned ordinal,failat,live,forbidden,erases,draws,base_depth;
static void*AllocVec(size_t n,ULONG flags){assert(!forbidden);(void)flags;if(++ordinal==failat)return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void FreeVec(void*p){assert(!forbidden);if(p){assert(live);--live;free(p);}}
static ULONG LockIBase(ULONG n){(void)n;return base_depth++;}
static void UnlockIBase(ULONG n){assert(base_depth==n+1);--base_depth;}
static void ObtainSemaphore(struct SignalSemaphore*s){assert(!forbidden);s->depth++;}
static void ReleaseSemaphore(struct SignalSemaphore*s){assert(!forbidden&&s->depth);s->depth--;}
static void ObtainSharedPointer(struct SharedPointer*s,struct IntuitionBase*b){(void)b;assert(s&&s->ref_count>0);s->ref_count++;}
static void ReleaseSharedPointer(struct SharedPointer*s,struct IntuitionBase*b){(void)b;assert(s&&s->ref_count>0);s->ref_count--;}
static ULONG GetBitMapAttr(struct BitMap*b,ULONG a){assert(!forbidden);return a==1?b->width:a==2?b->height:b->depth;}
static OOP_Object pixfmt={NULL,4,NULL};
static void OOP_GetAttr(OOP_Object*o,ULONG a,IPTR*out){assert(!forbidden);if(o->kind==1){struct BitMap*b=o->data;*out=a==1?b->width:a==2?b->height:(IPTR)&pixfmt;}else if(a==3)*out=(IPTR)o;else{assert(a==4);*out=4;}}
static void HIDD_BM_GetImage(OOP_Object*o,UBYTE*out,ULONG stride,ULONG x,ULONG y,ULONG w,ULONG h,int fmt){assert(!forbidden&&fmt==8&&o->kind==1);struct BitMap*b=o->data;assert(x+w<=b->width&&y+h<=b->height);for(ULONG row=0;row<h;row++){memcpy(out+row*stride,b->pixels+((row+y)*b->width+x)*4,w*4);if(!b->alpha)for(ULONG col=0;col<w;col++)out[row*stride+col*4]=255;}}
static void HIDD_BM_PutImage(OOP_Object*o,void*gc,const UBYTE*in,ULONG stride,ULONG x,ULONG y,ULONG w,ULONG h,int fmt){(void)gc;assert(!forbidden&&fmt==8&&o->kind==1);struct BitMap*b=o->data;assert(x+w<=b->width&&y+h<=b->height);for(ULONG row=0;row<h;row++)memcpy(b->pixels+((row+y)*b->width+x)*4,in+row*stride,w*4);}
static struct BitMap*AllocBitMap(ULONG w,ULONG h,ULONG depth,ULONG flags,struct BitMap*friend){(void)friend;struct BitMap*b=AllocVec(sizeof*b,0);if(!b)return NULL;b->pixels=AllocVec(w*h*4,0);if(!b->pixels){FreeVec(b);return NULL;}b->width=w;b->height=h;b->depth=depth;b->hidd=TRUE;b->alpha=(flags&BMF_SPECIALFMT)&&((flags>>24)==PIXFMT_ARGB32);b->object=(OOP_Object){NULL,1,b};return b;}
static void FreeBitMap(struct BitMap*b){FreeVec(b->pixels);FreeVec(b);}
static void BltBitMap(struct BitMap*a,int ax,int ay,struct BitMap*b,int bx,int by,ULONG w,ULONG h,int minterm,int mask,void*tmp){(void)ax;(void)ay;(void)bx;(void)by;(void)minterm;(void)mask;(void)tmp;memcpy(b->pixels,a->pixels,w*h*4);}
static struct Screen*FindFirstScreen(Object*node,struct IntuitionBase*b){(void)b;return ((struct IMonitorNode*)node->data)->front;}
static BOOL GetAttr(ULONG a,Object*o,IPTR*out){assert(!forbidden&&a==7);*out=(IPTR)o->data;return TRUE;}
static void cursor_Erase(OOP_Class*c,OOP_Object*o){(void)c;assert(!forbidden);struct HIDDDisplayData*d=o->data;erases++;d->cursor.cursor_drawn=FALSE;}
static void cursor_Draw(OOP_Class*c,OOP_Object*o){(void)c;assert(!forbidden);struct HIDDDisplayData*d=o->data;draws++;d->cursor.cursor_drawn=d->cursor.cursor_argb&&d->cursor.cursor_bm;}
struct pHidd_Display_SupportsPreparedCursor{ULONG id;};
struct pHidd_Display_PrepareCursorShape{ULONG id;OOP_Object*shape;LONG xoffset,yoffset;struct HIDD_CursorState*state;};
struct pHidd_Display_ReleaseCursorShape{ULONG id;struct HIDD_CursorState*state;};
struct pHidd_Display_InstallCursorShape{ULONG id;struct HIDD_CursorState*prepared,*previous;};
struct pHidd_Display_SetCursorShape{ULONG id;OOP_Object*shape;LONG xoffset,yoffset;};
'''
wrappers=r'''
static OOP_Class*BaseClass(OOP_Object*o){OOP_Class*c=o->cl;while(c->super)c=c->super;return c;}

/* Method dispatch: the first class in the chain implementing the method answers. */
static BOOL HIDD_Display_SupportsPreparedCursor(OOP_Object*d){struct pHidd_Display_SupportsPreparedCursor msg={0};OOP_Class*c;for(c=d->cl;c->super;c=c->super)if(c->declaresPreparedCursor)return TRUE;return Display__Hidd_Display__SupportsPreparedCursor(c,d,&msg);}
static BOOL HIDD_Display_PrepareCursorShape(OOP_Object*d,OOP_Object*b,LONG x,LONG y,struct HIDD_CursorState*s){struct pHidd_Display_PrepareCursorShape msg={0,b,x,y,s};return Display__Hidd_Display__PrepareCursorShape(BaseClass(d),d,&msg);}
static BOOL HIDD_Display_ReleaseCursorShape(OOP_Object*d,struct HIDD_CursorState*s){struct pHidd_Display_ReleaseCursorShape msg={0,s};return Display__Hidd_Display__ReleaseCursorShape(BaseClass(d),d,&msg);}
static BOOL HIDD_Display_InstallCursorShape(OOP_Object*d,struct HIDD_CursorState*a,struct HIDD_CursorState*b){struct pHidd_Display_InstallCursorShape msg={0,a,b};return Display__Hidd_Display__InstallCursorShape(BaseClass(d),d,&msg);}
static BOOL SetDisplayPointerShape(OOP_Object*d,struct SharedPointer*p,struct BitMap*b,struct IntuitionBase*base){(void)base;struct pHidd_Display_SetCursorShape msg={0,HIDD_BM_OBJ(b),p->xoffset,p->yoffset};return Display__Hidd_Display__SetCursorShape(BaseClass(d),d,&msg);}
'''
tests=r'''
static void pixels(void){
 UBYTE colors[3][4]={{0},{255,20,30,40},{255,230,240,250}};ULONG scales[]={100,125,150,175,200,225,250,275,300,325,350,375,400,100};
 for(unsigned phase=0;phase<14;phase++){
  ULONG scale=scales[phase],w,h;LONG hx,hy;assert(PointerScaleExtent(16,scale,&w)&&PointerScaleExtent(16,scale,&h));
  assert(PointerScaleOffset(-3,scale,&hx)&&PointerScaleOffset(-5,scale,&hy));
  assert(hx==-(LONG)((3*scale+50)/100)&&hy==-(LONG)((5*scale+50)/100));
  UBYTE*p=calloc(w*h,4);PointerRenderBuiltin(p,w,h,scale,POINTER_BUILTIN_ARROW,colors);unsigned long alpha=0;for(ULONG i=0;i<w*h;i++)alpha+=p[4*i];
  double area=(double)alpha/255.0*10000.0/(scale*scale);assert(area>71.0&&area<73.0);assert(p[0]==255&&p[1]==20);assert(!p[(w*h-1)*4]);
  printf("pointer scale=%u image=%ux%u hotspot=%d,%d alpha-area=%.4f\n",scale,w,h,hx,hy,area);
  PointerRenderBuiltin(p,w,h,scale,POINTER_BUILTIN_BUSY,colors);assert(p[((h/2)*w+w/2)*4]==255);assert(!p[0]);free(p);
 }
 UBYTE source[]={255,10,20,30,0,250,1,2,128,40,60,80,255,90,100,110};
 for(ULONG scale=100;scale<=400;scale+=25){ULONG w,h;assert(PointerScaleExtent(2,scale,&w)&&PointerScaleExtent(2,scale,&h));UBYTE*out=calloc(w*h,4);PointerRenderBitmap(out,w,h,source,2,2,scale);if(scale%100==0){ULONG f=scale/100;for(ULONG y=0;y<h;y++)for(ULONG x=0;x<w;x++)assert(!memcmp(out+(y*w+x)*4,source+((y/f)*2+x/f)*4,4));}else{assert(out[0]==255&&out[1]==10);assert(out[(w-1)*4]==0);assert(out[(w*h-1)*4]==255);}free(out);}
 LONG out;assert(!PointerScaleOffset(INT32_MIN,400,&out));assert(PointerScaleOffset(INT32_MIN,100,&out)&&out==INT32_MIN);assert(!PointerScaleValid(99)&&!PointerScaleValid(101)&&!PointerScaleValid(425));
 UBYTE transparent[]={0,255,0,0,255,0,100,200,0,255,0,0,255,0,100,200},smooth[4*3*3];PointerRenderBitmap(smooth,3,3,transparent,2,2,150);assert(smooth[4]==128&&smooth[5]==0&&smooth[6]==100&&smooth[7]==200);
}
static void generation(void){
 struct class_static_data csd={0};OOP_Class displayclass={&csd};struct HIDDDisplayData data={0};OOP_Object display={&displayclass,2,&data};
 struct MonitorHandle handle={&display};struct IMonitorNode mon={.handle=&handle};OOP_Object node={NULL,3,&mon};
 UBYTE apixels[4*4*3],bpixels[sizeof apixels];for(unsigned i=0;i<sizeof apixels;i++)apixels[i]=(i*13+17)&255;memcpy(bpixels,apixels,sizeof apixels);bpixels[0]=0;
 struct BitMap abm={4,3,32,TRUE,TRUE,apixels,{0}},bbm={4,3,32,TRUE,TRUE,bpixels,{0}};abm.object=(OOP_Object){NULL,1,&abm};bbm.object=(OOP_Object){NULL,1,&bbm};
 struct ExtSprite asprite={&abm},bsprite={&bbm};struct SharedPointer a={&asprite,-3,-5,1,FALSE,0},b={&bsprite,-1,-2,1,FALSE,0};
 struct Screen screen={200,&node,&a},other={100,&node,&b};mon.front=&screen;
 struct GfxBase gfx={0};struct IntuitionBase base={.GfxBase=&gfx,.PointerAlpha=65535};
 data.cursor.cursor_visible=TRUE;data.cursor.cursor_bm=&abm.object;data.cursor.cursor_mouseX=7;data.cursor.cursor_mouseY=9;
 assert(SetMonitorPointerScale(&mon,&screen,&a,&base));assert(a.ref_count==2&&mon.pointerRender->bitmap->alpha);
 assert(data.cursor.cursor_w==8&&data.cursor.cursor_h==6&&data.cursor.cursor_xoffset==-6&&data.cursor.cursor_yoffset==-10);
 assert(data.cursor.cursor_argb[0]==apixels[0]);assert(!memcmp(data.cursor.cursor_argb,apixels,4));
 unsigned retained=live;struct HIDD_CursorState before=data.cursor;struct PointerRenderGeneration*gen=mon.pointerRender;unsigned oldErases=erases;
 /* Unknown Display subclasses cannot inherit detached-state publication. */
 OOP_Class subclass={&csd,&displayclass};display.cl=&subclass;
 assert(!HIDD_Display_SupportsPreparedCursor(&display));
 assert(!SetMonitorPointerScale(&mon,&screen,&b,&base));
 assert(!memcmp(&before,&data.cursor,sizeof before)&&live==retained);
 {struct HIDD_CursorState s={0},p={0};
  assert(!HIDD_Display_PrepareCursorShape(&display,&abm.object,-3,-5,&s)&&!s.cursor_argb&&live==retained);
  assert(!HIDD_Display_ReleaseCursorShape(&display,&s));assert(!HIDD_Display_InstallCursorShape(&display,&s,&p));}
 /* A driver subclass keeping the base software cursor (efifbgfx on the M1)
  * declares the capability and reaches the inherited bodies by inheritance. */
 OOP_Class driver={&csd,&displayclass,TRUE};display.cl=&driver;
 assert(HIDD_Display_SupportsPreparedCursor(&display));
 {struct HIDD_CursorState s={0};assert(HIDD_Display_PrepareCursorShape(&display,&abm.object,-3,-5,&s)&&s.cursor_argb&&s.cursor_backup);
  assert(HIDD_Display_ReleaseCursorShape(&display,&s)&&!s.cursor_argb&&live==retained);}
 assert(!memcmp(&before,&data.cursor,sizeof before));
 display.cl=&displayclass;
 struct HIDD_CursorState alias=before;assert(!HIDD_Display_ReleaseCursorShape(&display,&alias));
 assert(!memcmp(&before,&data.cursor,sizeof before)&&live==retained);
 for(unsigned failure=1;failure<=7;failure++){
  ordinal=0;failat=failure;assert(!SetMonitorPointerScale(&mon,&screen,&b,&base));failat=0;
  assert(!memcmp(&before,&data.cursor,sizeof before)&&mon.pointerRender==gen&&mon.pointer==&a&&mon.pointerScreen==&screen&&live==retained&&a.ref_count==2&&b.ref_count==1&&erases==oldErases);
 }
 for(unsigned failure=1;failure<=4;failure++){
  ordinal=0;failat=failure;assert(!PrepareScreenPointerScale(&screen,100,&base));failat=0;
  assert(!memcmp(&before,&data.cursor,sizeof before)&&mon.pointerRender==gen&&live==retained&&a.ref_count==2);
 }
 struct PreparedScreenPointerScale*pending=PrepareScreenPointerScale(&other,400,&base);assert(pending&&!PreparedScreenPointerCursor(pending));assert(BindScreenPointerScale(pending,NULL));forbidden=1;InstallScreenPointerScale(pending,FALSE);forbidden=0;assert(ReleaseScreenPointerScale(pending,TRUE,&base));assert(live==retained&&!memcmp(&before,&data.cursor,sizeof before));
 struct Screen detached={100,NULL,NULL};
 pending=PrepareScreenPointerScale(&detached,200,&base);assert(pending&&!PreparedScreenPointerCursor(pending)&&BindScreenPointerScale(pending,NULL));
 assert(ReleaseScreenPointerScale(pending,FALSE,&base)&&live==retained);
 struct IMonitorNode saved_mon=mon;struct SharedPointer *saved_source=screen.Pointer;
 mon.pointerScreen=NULL;mon.pointer=NULL;screen.Pointer=NULL;unsigned noted=diagnostics;
 pending=PrepareScreenPointerScale(&screen,200,&base);assert(pending&&!PreparedScreenPointerCursor(pending)&&BindScreenPointerScale(pending,NULL));
 assert(diagnostics==noted+1&&ReleaseScreenPointerScale(pending,FALSE,&base)&&live==retained);
 mon=saved_mon;screen.Pointer=saved_source;
 struct HIDD_PixelMap map={&data.cursor,&display,&data.fbsem};
 pending=PrepareScreenPointerScale(&screen,100,&base);assert(pending);
 UBYTE alien[4]={0};UBYTE *saved_argb=data.cursor.cursor_argb;data.cursor.cursor_argb=alien;noted=diagnostics;
 assert(!BindScreenPointerScale(pending,&map)&&diagnostics==noted+1);data.cursor.cursor_argb=saved_argb;
 assert(ReleaseScreenPointerScale(pending,FALSE,&base)&&live==retained);
 pending=PrepareScreenPointerScale(&screen,100,&base);assert(pending&&BindScreenPointerScale(pending,&map));struct HIDD_CursorState*next=PreparedScreenPointerCursor(pending);assert(next->cursor_w==4&&next->cursor_xoffset==-3);
 /* Frame-render adapter here; actual proposed clipping/renderer runs separately. */
 next->cursor_bm=before.cursor_bm;next->cursor_visible=before.cursor_visible;next->cursor_mouseX=before.cursor_mouseX;next->cursor_mouseY=before.cursor_mouseY;
 forbidden=1;InstallScreenPointerScale(pending,FALSE);assert(data.cursor.cursor_argb==next->cursor_argb&&mon.pointerRender!=gen);InstallScreenPointerScale(pending,TRUE);forbidden=0;
 assert(!memcmp(&before,&data.cursor,sizeof before)&&mon.pointerRender==gen);assert(ReleaseScreenPointerScale(pending,FALSE,&base)&&live==retained&&a.ref_count==2);
 pending=PrepareScreenPointerScale(&screen,100,&base);assert(pending&&BindScreenPointerScale(pending,&map));next=PreparedScreenPointerCursor(pending);next->cursor_bm=before.cursor_bm;next->cursor_visible=TRUE;UBYTE*winner=next->cursor_argb;
 forbidden=1;InstallScreenPointerScale(pending,FALSE);forbidden=0;assert(data.cursor.cursor_argb==winner);assert(ReleaseScreenPointerScale(pending,TRUE,&base)&&a.ref_count==2);
 assert(data.cursor.cursor_w==4&&data.cursor.cursor_xoffset==-3);
 /* An explicit physical image/hotspot opts out even on a400% screen. */
 b.physicalPixels=TRUE;screen.Scale=400;assert(SetMonitorPointerScale(&mon,&screen,&b,&base));assert(data.cursor.cursor_w==4&&data.cursor.cursor_xoffset==-1&&b.ref_count==2&&a.ref_count==1);
 ForgetScreenPointerScale(&screen,&base);assert(!mon.pointerScreen);mon.front=&other;
 pending=PrepareScreenPointerScale(&other,200,&base);assert(pending&&BindScreenPointerScale(pending,&map));next=PreparedScreenPointerCursor(pending);next->cursor_bm=data.cursor.cursor_bm;next->cursor_visible=TRUE;
 forbidden=1;InstallScreenPointerScale(pending,FALSE);forbidden=0;assert(ReleaseScreenPointerScale(pending,TRUE,&base)&&mon.pointerScreen==&other);
 display.cl=&driver;assert(SetMonitorPointerScale(&mon,&other,&a,&base)&&mon.pointer==&a&&data.cursor.cursor_xoffset==-3&&data.cursor.cursor_drawn);display.cl=&displayclass;
 DisposeMonitorPointerScale(&mon,&base);FreeVec(data.cursor.cursor_argb);FreeVec(data.cursor.cursor_backup);assert(!live&&a.ref_count==1&&b.ref_count==1&&!data.fbsem.depth&&!base_depth);
 puts("PASS actual generation/HIDD setter:7normal+4scale allocation failures, source pins, independent screen, physical optout, pure publication/rollback, loser release");
}
int main(void){pixels();generation();return 0;}
'''
mutations={
    'missing-foreign-shape-diagnostic':('bug("[SCALE] pointer refused: live shape changed outside Intuition\\n");', '(void)0;'),
    'foreign-shape-accepted':('if (map->cursorState->cursor_argb != (pending->before ? pending->before->presentationARGB : NULL))', 'if (0)'),
    'unscaled-hotspot':('QUAD product = (QUAD)point * scale;','QUAD product = (QUAD)point * 100;'),
    'double-physical-pointer':('if (source->physicalPixels) scale = 100;','/* physical units ignored */'),
    'bitmap-alpha-lost':('BMF_SPECIALFMT | ((ULONG)PIXFMT_ARGB32 << 24)','0'),
    'early-live-erase':('generation = PointerPrepareGeneration(source, scale, capable, IntuitionBase);','cursor_Erase(BaseClass(display), display); generation = PointerPrepareGeneration(source, scale, capable, IntuitionBase);'),
    'premature-source-free':('ObtainSharedPointer(source, IntuitionBase);','/* missing source pin */'),
    'fractional-transparent-fringe':('UQUAD alpha = weight * c[0];','UQUAD alpha = weight * 255;'),
    'exact-class-prepared-body':('return OOP_OCLASS(o) == cl || HIDD_Display_SupportsPreparedCursor(o);','return OOP_OCLASS(o) == cl;'),
    'undeclared-subclass-inherits':('return OOP_OCLASS(o) == cl || HIDD_Display_SupportsPreparedCursor(o);','return TRUE;'),
    'rollback-new-hotspot':('*pending->live = rollback ? pending->previous : pending->proposed;','*pending->live = pending->proposed;'),
}
production=prefix+gfx+wrappers+engine+tests
def driver_declaration(conf):
    """The M1 display driver declares the prepared cursor and keeps the base
    software cursor: overriding a cursor method would void the declaration."""
    m=re.search(r'classid\s+CLID_Hidd_Display_EFIFB.*?##begin methodlist(.*?)##end methodlist',conf,re.S);assert m
    methods=m.group(1).split()
    assert 'SupportsPreparedCursor' in methods,'efifbgfx display does not declare the prepared cursor'
    for name in ('SetCursorShape','SetCursorPos','SetCursorVisible','PrepareCursorShape','ReleaseCursorShape','InstallCursorShape'):
        assert name not in methods,'efifbgfx display overrides '+name
conf=read('rom/hidds/efifbgfx/efifbgfx.conf');driver_declaration(conf)
print('PASS efifbgfx display declares SupportsPreparedCursor and keeps the base cursor methods')
if '--negative-controls' in sys.argv:
    for name,mutant in (('driver-undeclared',conf.replace('Show\nSupportsPreparedCursor\n','Show\n')),
                        ('driver-hardware-cursor',conf.replace('Show\nSupportsPreparedCursor\n','Show\nSupportsPreparedCursor\nSetCursorShape\n'))):
        assert mutant!=conf,name
        try:driver_declaration(mutant)
        except AssertionError:print('REJECTED',name)
        else:raise SystemExit(name+' escaped')
with tempfile.TemporaryDirectory(prefix='pointerscale-') as d:
    unit=Path(d)/'fixture.c';exe=Path(d)/'fixture';cases=[('production',production)]
    if '--negative-controls' in sys.argv:
        for name,(old,new) in mutations.items():
            assert old in production,name;cases.append((name,production.replace(old,new)))
    for name,source in cases:
        unit.write_text(source);subprocess.run(['clang','-std=c11','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe)],capture_output=True,text=True)
        if name=='production':print(run.stdout,end='');assert run.returncode==0,run.stderr
        else:assert run.returncode!=0,name+' escaped';print('REJECTED',name)
