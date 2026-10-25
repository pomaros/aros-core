#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Run actual native widget canvas, Energy resources/buttons/graph and alive text.
OS allocation, font, window and graphics calls are host adapters; no hardware proof.
"""
import argparse, pathlib, re, subprocess, tempfile, sys
sys.dont_write_bytecode = True
from nativewidget_geometry_fixture import header as geometry_header, bodies as geometry_bodies
p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[4]);p.add_argument('--mutations',action='store_true');p.add_argument('--out',type=pathlib.Path);args=p.parse_args();root=args.source
energy=(root/'arch/aarch64-apple/c/Energy.c').read_text();place=(root/'arch/aarch64-apple/include/widgetplace.h').read_text();alive=(root/'arch/aarch64-apple/kernel/coldstart_proof.c').read_text();canvas=(root/'arch/aarch64-apple/include/widgetscale.h').read_text()
def function(text,name):
 m=re.search(r'(?m)^static[^;{}]*\b'+name+r'\([^;]*?\)\s*\{',text);assert m,name
 start=m.start();i=text.index('{',m.start());depth=1;j=i+1
 while depth:
  if text[j]=='{':depth+=1
  if text[j]=='}':depth-=1
  j+=1
 return text[start:j]
shim=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int64_t QUAD;typedef uint64_t UQUAD;typedef int32_t LONG;typedef uint32_t ULONG;typedef int16_t WORD;typedef uint16_t UWORD;typedef uint8_t UBYTE;typedef uintptr_t IPTR;typedef void *APTR;typedef void Object;typedef char *STRPTR;typedef const char *CONST_STRPTR;typedef int BOOL;typedef void VOID;
#define TRUE 1
#define FALSE 0
#define RETURN_ERROR 10
#define WIN_WIDTH 772
#define DEFAULT_FONT "Vera Sans.font"
#define DEFAULT_SIZE 15
#define BMA_DEPTH 4
#define BMF_CLEAR 1
#define MEMF_CLEAR 1
#define MEMF_ANY 2
#define SA_RenderContext 164
#define TAG_DONE 0
#define WFLG_DRAGBAR 1
#define WFLG_DEPTHGADGET 2
#define WFLG_CLOSEGADGET 4
#define WFLG_SMART_REFRESH 8
#define WFLG_WINDOWACTIVE 64
#define WFLG_RMBTRAP 16
#define WFLG_NOCAREREFRESH 32
#define PSNF_PRIVATE 1
#define IDCMP_CLOSEWINDOW 1
#define IDCMP_MOUSEBUTTONS 2
#define IDCMP_REFRESHWINDOW 4
#define IDCMP_RAWKEY 8
#define IDCMP_CHANGESCALE 16
#define IDCMP_NEWSIZE 32
#define IDCMP_CHANGEWINDOW 64
#define HIGHLIGHTTEXTPEN 4
#define AROS_APPLE_BATTERY_CURRENT 1
#define AROS_APPLE_BATTERY_PRESENT 2
#define AROS_APPLE_BATTERY_ADAPTER 4
#define AROS_APPLE_BATTERY_CHARGING 8
#define POWERSTF_CHARGING 1
#define AROS_APPLE_CPU_STATS_MAX 64
#define E_LEVELS 5
#define P_LEVELS 12
struct AROSAppleBatteryInfo{ULONG flags,remaining_mah,millivolts,full_charge_mah,charge_percent,cycles;LONG current_ma,temperature_tenths;};
struct PowerState{ULONG pst_Flags,pst_MinutesLeft;};struct AROSAppleDCPInfo{ULONG power,millinits;};struct AROSCPUSample{int unused;};
static char panel_result[120],test_result[200];static BOOL test_ticks_held;static void*idle=(void*)1;static ULONG domain_count;
#define BACKGROUNDPEN 0
#define SHINEPEN 1
#define SHADOWPEN 2
#define TEXTPEN 3
#define JAM1 1
#define JAM2 2
#define HISTORY 300
#define GRAPH_HEIGHT 64
#define C_GREEN 0xff2e9e44
#define C_AMBER 0xffe0a000
#define C_GREY 0xffa0a0a0
#define C_RED 0xffd03030
#define C_GRAPH 0xff3a78c8
#define C_GRAPH_ALT 0xff6fa8e8
#define C_GRAPH_BG 0xff202830
#define STRIPE 5
struct Library{};struct TextAttr{STRPTR ta_Name;UWORD ta_YSize;UBYTE ta_Style,ta_Flags;};
struct Rectangle{LONG MinX,MinY,MaxX,MaxY;};struct TextExtent{UWORD te_Width,te_Height;struct Rectangle te_Extent;};
struct TextFont{UWORD tf_YSize,tf_XSize,baseline;};struct DiskFontHeader{struct TextFont dfh_TF;};
struct BitMap{LONG width,height;};struct Window;struct RastPort{struct BitMap *BitMap;struct TextFont *Font;LONG TxHeight,TxBaseline;struct Window *owner;};
struct Screen;struct DrawInfo{struct TextFont *dri_Font;UWORD *dri_Pens;struct Screen *dri_Screen;};
struct ScreenRenderContext{UWORD src_Scale;struct DrawInfo *src_PhysicalDrawInfo;ULONG src_PhysicalBarHeight;};
struct Screen{struct RastPort RastPort;LONG Width,Height;ULONG Scale;};
struct Window{LONG Width,Height,BorderLeft,BorderTop,BorderRight,BorderBottom;struct RastPort *RPort;LONG LeftEdge,TopEdge;const struct ScreenRenderContext *expected;ULONG Flags;struct Screen *WScreen;BOOL ScaleAware,ScaleGeometryEnabled;struct AwareGeometry ScaleGeometry;LONG MinWidth,MinHeight,MaxWidth,MaxHeight;struct RastPort port;};
struct ScaleWindowGeometry{LONG Left,Top,Width,Height,MinWidth,MinHeight,MaxWidth,MaxHeight;};
struct NewWindow{LONG LeftEdge,TopEdge,Width,Height,MinWidth,MinHeight,MaxWidth,MaxHeight;};
struct TagItem{ULONG ti_Tag;IPTR ti_Data;};
enum{WA_Title=1,WA_PubScreen,WA_ScaleAware,WA_ScaleGeometry,WA_RenderContext,WA_Hidden,WA_Activate,WA_Flags,WA_IDCMP,WA_SizeGadget,WA_SizeBBottom};
static ULONG opened_idcmp;static BOOL opened_sizegadget;static LONG fill_area;
static struct ScreenRenderContext *current;static int failat,ordinal,live_fonts,live_bitmaps,live_windows,live_views;
struct Node{struct Node*ln_Succ;char*ln_Name;};struct List{struct Node*lh_Head;};struct PubScreenNode{struct Node psn_Node;ULONG psn_Flags;};
static struct PubScreenNode pubnode;static struct Node pubtail;static struct List publist;static struct Screen *pubscreen;static int pins;
static struct List *LockPubScreenList(void){return &publist;}static void UnlockPubScreenList(void){}
static struct Screen *LockPubScreen(char*n){assert(!strcmp(n,"Workbench"));++pins;return pubscreen;}static void UnlockPubScreen(void*n,struct Screen*s){(void)n;assert(s==pubscreen&&pins>0);--pins;}
static int exact_missing,stale_open,stale_show;static LONG movex,movey,blitx,blity,blitw,blith;static ULONG colours[HISTORY+2];static int colour_count;
static void *acquire(size_t n){if(++ordinal==failat)return NULL;void *p=calloc(1,n);assert(p);return p;}
static void*AllocVec(ULONG n,ULONG flags){(void)flags;void*p=acquire(n);if(p)++live_views;return p;}static void FreeVec(void*p){if(p){--live_views;free(p);}}
static BOOL GetAttr(ULONG a,Object*s,IPTR*out){(void)a;(void)s;*out=(IPTR)current;return current!=NULL;}
static LONG GetBitMapAttr(struct BitMap*b,LONG a){(void)b;(void)a;return 32;}
static struct BitMap *AllocBitMap(LONG w,LONG h,LONG d,LONG f,struct BitMap*friend){(void)d;(void)f;(void)friend;struct BitMap*b=acquire(sizeof(*b));if(b){b->width=w;b->height=h;++live_bitmaps;}return b;}
static void FreeBitMap(struct BitMap*b){assert(b);--live_bitmaps;free(b);}
static void InitRastPort(struct RastPort*r){memset(r,0,sizeof(*r));}
static void SetFont(struct RastPort*r,struct TextFont*f){assert(f);r->Font=f;r->TxHeight=f->tf_YSize;r->TxBaseline=f->baseline;}
static LONG TextLength(struct RastPort*r,CONST_STRPTR s,ULONG n){(void)s;return r->Font->tf_XSize*n;}
static ULONG TextFit(struct RastPort*r,CONST_STRPTR s,ULONG n,struct TextExtent*t,struct TextExtent*c,LONG direction,LONG w,LONG h){(void)s;(void)c;(void)direction;(void)h;assert(t);return n<(ULONG)(w/r->Font->tf_XSize)?n:(ULONG)(w/r->Font->tf_XSize);}
static void TextExtent(struct RastPort*r,CONST_STRPTR s,ULONG n,struct TextExtent*t){(void)s;(void)n;t->te_Width=r->Font->tf_XSize;t->te_Height=r->TxHeight;t->te_Extent=(struct Rectangle){-3,-r->TxBaseline,r->Font->tf_XSize+1,r->TxHeight-r->TxBaseline-1};}
static void BltBitMapRastPort(struct BitMap*b,LONG x,LONG y,struct RastPort*r,LONG dx,LONG dy,LONG w,LONG h,LONG mode){(void)r;(void)dx;(void)dy;(void)mode;assert(x>=0&&y>=0&&x+w<=b->width&&y+h<=b->height);blitx=x;blity=y;blitw=w;blith=h;}
static struct TextFont *OpenDiskFont(struct TextAttr*t){if(exact_missing&&t->ta_YSize!=15)return NULL;struct TextFont*f=acquire(sizeof(*f));if(f){f->tf_YSize=t->ta_YSize;f->tf_XSize=(t->ta_YSize+1)/2;f->baseline=t->ta_YSize-2;++live_fonts;}return f;}
static struct DiskFontHeader *NewScaledDiskFont(struct TextFont*base,struct TextAttr*t){(void)base;struct DiskFontHeader*d=acquire(sizeof(*d));if(d){d->dfh_TF.tf_YSize=t->ta_YSize;d->dfh_TF.tf_XSize=(t->ta_YSize+1)/2;d->dfh_TF.baseline=t->ta_YSize-2;++live_fonts;}return d;}
static void CloseFont(struct TextFont*f){assert(f);--live_fonts;free(f);}
static struct Window *OpenWindowTagList(void*unused,struct TagItem*t){(void)unused;struct ScaleWindowGeometry*g=NULL;const struct ScreenRenderContext*c=NULL;struct Screen*screen=NULL;BOOL hidden=0,aware=0,sizegadget=0,sizebottom=0;ULONG idcmp=0;for(;t->ti_Tag;t++){if(t->ti_Tag==WA_ScaleGeometry)g=(void*)t->ti_Data;if(t->ti_Tag==WA_RenderContext)c=(void*)t->ti_Data;if(t->ti_Tag==WA_PubScreen)screen=(void*)t->ti_Data;if(t->ti_Tag==WA_Hidden)hidden=t->ti_Data;if(t->ti_Tag==WA_ScaleAware)aware=t->ti_Data;if(t->ti_Tag==WA_IDCMP)idcmp=t->ti_Data;if(t->ti_Tag==WA_SizeGadget)sizegadget=t->ti_Data;if(t->ti_Tag==WA_SizeBBottom)sizebottom=t->ti_Data;}opened_idcmp=idcmp;opened_sizegadget=sizegadget&&sizebottom;assert(hidden&&aware&&g&&c&&screen);if(stale_open||c!=current)return NULL;struct Window*w=acquire(sizeof(*w));if(w){screen->Width=screen->RastPort.BitMap->width;screen->Height=screen->RastPort.BitMap->height;screen->Scale=c->src_Scale;w->WScreen=screen;w->ScaleAware=TRUE;w->BorderLeft=w->BorderRight=w->BorderBottom=(4*c->src_Scale+50)/100;w->BorderTop=c->src_PhysicalDrawInfo->dri_Font->tf_YSize+(5*c->src_Scale+50)/100;if(sizegadget&&sizebottom)w->BorderBottom=(16*c->src_Scale+50)/100;w->RPort=&w->port;w->port.owner=w;struct NewWindow initial={0};if(!WindowFitInitializeGeometry(w,g,screen->Width,screen->Height,TRUE,&initial)){free(w);return NULL;}w->Width=initial.Width;w->Height=initial.Height;w->LeftEdge=initial.LeftEdge;w->TopEdge=initial.TopEdge;w->MinWidth=initial.MinWidth;w->MinHeight=initial.MinHeight;w->MaxWidth=initial.MaxWidth;w->MaxHeight=initial.MaxHeight;w->expected=c;VerifyWidgetGeometry(w);++live_windows;}return w;}

static void ActivateWindow(struct Window*w){w->Flags|=WFLG_WINDOWACTIVE;}
static BOOL ShowWindow(struct Window*w,void*unused){assert(!unused);return !stale_show&&w->expected==current;}
static void CopyMem(const void*s,void*d,ULONG n){memcpy(d,s,n);}
static void CloseWindow(struct Window*w){assert(w);--live_windows;free(w);}
static void SetAPen(struct RastPort*r,UWORD pen){(void)r;(void)pen;}
static void SetBPen(struct RastPort*r,UWORD pen){(void)r;(void)pen;}
static void SetDrMd(struct RastPort*r,UWORD mode){(void)r;(void)mode;}
static void RectFill(struct RastPort*r,LONG x,LONG y,LONG x1,LONG y1){if(r->owner){struct Window*w=r->owner;assert(x<=x1&&y<=y1&&x>=w->BorderLeft&&y>=w->BorderTop&&x1<w->Width-w->BorderRight&&y1<w->Height-w->BorderBottom);fill_area+=(x1-x+1)*(y1-y+1);return;}assert(r->BitMap);assert(x>=0&&y>=0&&x1<r->BitMap->width&&y1<r->BitMap->height);}
static void Move(struct RastPort*r,LONG x,LONG y){(void)r;movex=x;movey=y;}
static void Draw(struct RastPort*r,LONG x,LONG y){(void)r;(void)x;(void)y;}
static int textcalls;static void Text(struct RastPort*r,CONST_STRPTR s,ULONG n){(void)s;(void)n;assert(r->Font);++textcalls;}
static void FillPixelArray(struct RastPort*r,LONG x,LONG y,LONG w,LONG h,ULONG c){RectFill(r,x,y,x+w-1,y+h-1);if(colour_count<HISTORY+2)colours[colour_count++]=c;}
static struct Library *DiskfontBase=(void*)1;static ULONG every=2;

'''
loop=energy[energy.index('    while (running) {'):energy.index('\nout:')]
assert re.search(r'class == IDCMP_NEWSIZE \|\| class == IDCMP_CHANGEWINDOW\) \{\n\s+if \(WidgetContext\(v\.screen\) == v\.context\) \{\n(?:\s+/\*[^*]*\*/\n)?\s+WidgetPlaceChanged\(&v\.place, v\.window, v\.context->src_Scale\);\n\s+if \(class == IDCMP_NEWSIZE\)\n\s+energy_view_present\(&v\);',loop),'Energy NEWSIZE/CHANGEWINDOW dispatch'
assert re.search(r'class == IDCMP_NEWSIZE \|\| class == IDCMP_CHANGEWINDOW\) \{(?:\n\s+/\*[^*]*\*+(?:[^/*][^*]*\*+)*/)?\n\s+WidgetPlaceChanged\(&alive_view->place, alive, alive_view->context->src_Scale\);\n\s+if \(class == IDCMP_NEWSIZE\)\n\s+draw_alive\(alive,',alive),'alive NEWSIZE/CHANGEWINDOW dispatch'
assert alive.count('alive_present(view, target);')==1,'alive draw presents through the resize-aware path'
shim=shim.replace('struct Library{};',geometry_header(root)+'\nstruct Library{};')
shim=shim.replace('static struct Window *OpenWindowTagList',geometry_bodies(root)+'\nstatic struct Window *OpenWindowTagList')
groups=energy[energy.index('static const struct { const char *name, *match[3]; } groups[]'):energy.index('struct state {')]
state=energy[energy.index('struct state {'):energy.index('struct view {')]
tables=energy[energy.index('static const UWORD e_mhz'):energy.index('enum square')]
labels=energy[energy.index('static const char *const light_labels'):energy.index('/* Everything;')]
parts=[shim,canvas,place,groups,state,tables,labels,'enum square { SQ_DONE, SQ_PART, SQ_NONE, SQ_OFF };',energy[energy.index('enum button_id {'):energy.index('/* The PMGR domains')],energy[energy.index('struct view {'):energy.index('static const struct AROSAppleIdleControl')], '#define PX(n) WidgetPixels(v->context->src_Scale,(n))']
parts += [function(energy,n) for n in ['erase','text_at','line_text','button','heading','square_colour','squares','format_duration','watts','mean_pstr','draw_graph','draw_all','sample_power','energy_view_present','energy_view_minimum','energy_view_close','energy_view_open','energy_view_show','energy_view_replace']]
# Compile the actual redraw gate rather than a second copy of its condition.
gate=re.search(r'            if \(tick\)\n                sample_power\(s\);',energy).group(0)
parts+=['static void redraw_sample(struct state*s,BOOL tick){'+gate+'}\n']
retry_start=energy.index('        if (running && (rescale || WidgetContext(v.screen) != v.context))')
retry_end=energy.index('        if (redraw && running)',retry_start)
retry=energy[retry_start:retry_end].replace('v.screen','v->screen').replace('v.context','v->context').replace('(&v,','(v,')
parts += ['static BOOL retry_control(struct view*v,const struct state*s,const IPTR*args,BOOL running,BOOL rescale){int rc=0;'+retry+'return running;}']
# Actual alive native text/cell mapping; the remaining hold/sensor service is not mocked as proof.
a=alive.index('#define ALIVE_SCALE');b=alive.index('/*\n * The backlight',a)
parts += [alive[a:b]]
parts += [function(alive,n) for n in ["alive_present","alive_minimum","close_alive_view","prepare_alive_view"]]
parts += ["static void draw_alive_view(struct AliveView*v,uint64_t uptime){(void)uptime;assert(v->canvas.port.Font==v->context->src_PhysicalDrawInfo->dri_Font);}"]
parts += [function(alive,n) for n in ["open_alive_window","replace_alive_window"]]
parts += ['\n'.join(re.findall(r'^#define ALIVE_ROW_.*$',alive,re.M))]
parts += ['static LONG alive_kbd_pct; static int action;static LONG kbd_light_saved_pct(void){return 50;}static void kbd_light_set_pct(LONG n){action=n<alive_kbd_pct?1:2;}static void lcd_step(int d){action=d<0?3:4;}static void audio_volume_step(int d){action=d<0?5:6;}']
parts += [function(alive,'alive_click')]
main=r'''
/* Intuition's size gadget: the request within the window's limits. */
static void resize_to(struct Window*w,LONG width,LONG height){
 if(width<w->MinWidth)width=w->MinWidth;if(height<w->MinHeight)height=w->MinHeight;
 if(width>w->MaxWidth)width=w->MaxWidth;if(height>w->MaxHeight)height=w->MaxHeight;
 w->Width=width;w->Height=height;
 w->ScaleGeometry.axis[AG_WIDTH]=(struct AwareGeometryValue){width,w->WScreen->Scale};
 w->ScaleGeometry.axis[AG_HEIGHT]=(struct AwareGeometryValue){height,w->WScreen->Scale};}
/* Independent viewport oracle after a present: the offset is reachable for
 * the inner size, and the blit plus the erased remainder tile it exactly. */
static void check_viewport(const struct WidgetCanvas*c,const struct Window*w){
 LONG iw=w->Width-w->BorderLeft-w->BorderRight,ih=w->Height-w->BorderTop-w->BorderBottom;
 LONG sw=iw<c->width?iw:c->width,sh=ih<c->height?ih:c->height;
 LONG maxx=c->width>iw?c->width-iw:0,maxy=c->height>ih?c->height-ih:0;
 assert(iw>0&&ih>0&&blitw==sw&&blith==sh);
 assert(c->left>=0&&c->left<=maxx&&c->top>=0&&c->top<=maxy);
 assert(blitw*blith+fill_area==iw*ih);}
static void begin_present(void){fill_area=0;blitw=blith=-1;}
/* Shrink to the minimum, scroll to the far corner, reach every button,
 * then grow to the screen (beyond the canvas where it fits). */
static void energy_resize_cases(struct view*v){
 LONG mw,mh;energy_view_minimum(v,&mw,&mh);struct Window*w=v->window;
 assert(opened_sizegadget&&(opened_idcmp&IDCMP_NEWSIZE));
 assert(w->MinWidth==mw&&w->MinHeight==mh&&mh-w->BorderTop-w->BorderBottom>=2*v->lh);
 assert(w->ScaleGeometry.axis[AG_MINWIDTH].value==mw&&w->ScaleGeometry.axis[AG_MINHEIGHT].value==mh);
 assert(w->ScaleGeometry.axis[AG_MINWIDTH].scale==v->context->src_Scale&&w->ScaleGeometry.axis[AG_MINHEIGHT].scale==v->context->src_Scale);
 assert(w->ScaleGeometry.axis[AG_MAXWIDTH].value==-1&&w->ScaleGeometry.axis[AG_MAXHEIGHT].value==-1);
 v->canvas.left=v->canvas.width;v->canvas.top=v->canvas.height;begin_present();energy_view_present(v);check_viewport(&v->canvas,w);
 resize_to(w,1,1);assert(w->Width==mw&&w->Height==mh);begin_present();energy_view_present(v);check_viewport(&v->canvas,w);
 for(int i=0;i<1000;i++){WidgetCanvasScroll(&v->canvas,0x4d,v->lh*3);WidgetCanvasScroll(&v->canvas,0x4e,v->lh*3);}
 begin_present();energy_view_present(v);check_viewport(&v->canvas,w);
 LONG iw=mw-w->BorderLeft-w->BorderRight,ih=mh-w->BorderTop-w->BorderBottom;
 assert(v->canvas.top+ih==v->canvas.height&&v->canvas.left+iw==v->canvas.width);
 for(int id=1;id<B_COUNT;id++){struct button*b=&v->buttons[id];LONG cx,cy;if(!b->w)continue;
  v->canvas.left=b->x;v->canvas.top=b->y;begin_present();energy_view_present(v);check_viewport(&v->canvas,w);
  assert(WidgetCanvasPoint(&v->canvas,w,w->BorderLeft+b->x-v->canvas.left,w->BorderTop+b->y-v->canvas.top,&cx,&cy));assert(cx==b->x&&cy==b->y);}
 resize_to(w,v->canvas.width+w->BorderLeft+w->BorderRight+60,v->canvas.height+w->BorderTop+w->BorderBottom+60);
 begin_present();energy_view_present(v);check_viewport(&v->canvas,w);
 if(w->Width-w->BorderLeft-w->BorderRight>v->canvas.width)assert(fill_area>0&&v->canvas.left==0);}
static void alive_resize_cases(struct AliveView*v){
 LONG mw,mh;alive_minimum(v,&mw,&mh);struct Window*w=v->window;
 assert(opened_sizegadget&&(opened_idcmp&IDCMP_NEWSIZE));
 assert(w->MinWidth==mw&&w->MinHeight==mh&&mh-w->BorderTop-w->BorderBottom>=v->font_height);
 assert(w->ScaleGeometry.axis[AG_MINWIDTH].value==mw&&w->ScaleGeometry.axis[AG_MINWIDTH].scale==v->context->src_Scale);
 assert(w->ScaleGeometry.axis[AG_MAXWIDTH].value==-1&&w->ScaleGeometry.axis[AG_MAXHEIGHT].value==-1);
 resize_to(w,1,1);assert(w->Width==mw&&w->Height==mh);
 for(int i=0;i<1000;i++){WidgetCanvasScroll(&v->canvas,0x7b,v->font_height*3);WidgetCanvasScroll(&v->canvas,0x4e,v->font_height*3);}
 begin_present();alive_present(v,w);check_viewport(&v->canvas,w);
 assert(v->canvas.top+mh-w->BorderTop-w->BorderBottom==v->canvas.height&&v->canvas.left+mw-w->BorderLeft-w->BorderRight==v->canvas.width);
 resize_to(w,v->canvas.width+w->BorderLeft+w->BorderRight+60,v->canvas.height+w->BorderTop+w->BorderBottom+60);
 begin_present();alive_present(v,w);check_viewport(&v->canvas,w);
 if(w->Width-w->BorderLeft-w->BorderRight>v->canvas.width)assert(fill_area>0&&v->canvas.left==0);}
static LONG maxl(LONG a,LONG b){return a>b?a:b;}
static BOOL open_energy_at(struct view*v,LONG left,LONG top,const IPTR*args){WidgetPlaceUser(&v->place,left,top,0,0);return energy_view_open(v,args);}
static struct AliveView*prepare_auto(void){struct WidgetPlace p;WidgetPlaceAutomatic(&p);return prepare_alive_view(&p);}
static BOOL apart(const struct Window*a,const struct Window*b){return a->LeftEdge+a->Width<=b->LeftEdge||b->LeftEdge+b->Width<=a->LeftEdge||a->TopEdge+a->Height<=b->TopEdge||b->TopEdge+b->Height<=a->TopEdge;}
/* An automatic window inside its slot at this context; Energy against the
 * slot's right edge and as wide as its page wherever the slot holds it. */
static void check_slot(struct Window*w,struct Screen*screen,const struct ScreenRenderContext*c,ULONG slot,const struct WidgetCanvas*page){
 struct WidgetRect r;assert(WidgetScreenSlot(screen,c,slot,&r));LONG sc=c->src_Scale;
 LONG l=WidgetPixels(sc,r.Left),t=WidgetPixels(sc,r.Top),rr=WidgetPixels(sc,r.Left+r.Width),b=WidgetPixels(sc,r.Top+r.Height);
 if(w->LeftEdge<l-1||w->TopEdge<t-1||w->LeftEdge+w->Width>rr+1||w->TopEdge+w->Height>b+1){fprintf(stderr,"window %ld,%ld %ldx%ld outside slot %ld,%ld..%ld,%ld at %ld%%\n",(long)w->LeftEdge,(long)w->TopEdge,(long)w->Width,(long)w->Height,(long)l,(long)t,(long)rr,(long)b,(long)sc);abort();}
 if(page){LONG inner=w->Width-w->BorderLeft-w->BorderRight;assert(labs(w->LeftEdge+w->Width-rr)<=1);
  if(page->width+w->BorderLeft+w->BorderRight<=rr-l&&inner<page->width){fprintf(stderr,"Energy truncated: inner %ld < page %ld in a %ld slot at %ld%%\n",(long)inner,(long)page->width,(long)(rr-l),(long)sc);abort();}
  if(page->width+w->BorderLeft+w->BorderRight<=rr-l&&inner>page->width+sc/100+1){fprintf(stderr,"Energy wider than its page: inner %ld page %ld at %ld%%\n",(long)inner,(long)page->width,(long)sc);abort();}}}

int main(void){
 UWORD pens[]={0,1,2,3,4};struct TextFont borrowed={13,7,10};struct DrawInfo di={&borrowed,pens,NULL};struct Screen screen={0};struct BitMap screenbm={2560,1600};screen.RastPort.BitMap=&screenbm;screen.Width=2560;screen.Height=1600;di.dri_Screen=&screen;
 pubscreen=&screen;pubnode.psn_Node.ln_Name="Workbench";pubnode.psn_Node.ln_Succ=&pubtail;publist.lh_Head=&pubnode.psn_Node;
 struct ScreenRenderContext context={100,&di};current=&context;IPTR args[5]={0};struct state s={0};s.pstr_mw=1000;
 for(int scale=100;scale<=400;scale+=25){context.src_Scale=scale;context.src_PhysicalBarHeight=WidgetPixels(scale,18);borrowed.tf_YSize=WidgetPixels(scale,13);borrowed.tf_XSize=WidgetPixels(scale,7);borrowed.baseline=borrowed.tf_YSize-2;
  for(int missing=0;missing<2;missing++){exact_missing=missing;struct view v={0};v.screen=&screen;ordinal=0;failat=0;assert(open_energy_at(&v,20,20,args));int allocations=ordinal;assert(v.rp->TxHeight==WidgetPixels(scale,15));
   WORD right=WidgetPixels(scale,740), y=WidgetPixels(scale,400);button(&v,B_POWEROFF,right,y,"Power off",TRUE);struct button b=v.buttons[B_POWEROFF];assert(b.h>=v.rp->TxHeight);assert(movey==y+1+(b.h-v.rp->TxHeight)/2+v.rp->TxBaseline);
   v.canvas.left=100000;v.canvas.top=100000;WidgetCanvasPresent(&v.canvas,v.window);assert(blitw>0&&blith>0&&blitx>=0&&blity>=0);LONG old=v.canvas.top;assert(WidgetCanvasScroll(&v.canvas,0x4c,v.lh));assert(v.canvas.top==old-v.lh);WidgetCanvasPresent(&v.canvas,v.window);
   colour_count=0;for(int i=0;i<20;i++)sample_power(&s);draw_graph(&v,WidgetPixels(scale,30),&s);assert(colours[0]==C_GRAPH_BG);for(int i=0;i<20;i++)assert(colours[i+1]==((((s.samples_total-1-i)/5)&1)?C_GRAPH_ALT:C_GRAPH));
   ULONG count=s.samples_total;redraw_sample(&s,FALSE);assert(s.samples_total==count);redraw_sample(&s,TRUE);assert(s.samples_total==count+1);
   s.have_battery=TRUE;s.battery.flags=AROS_APPLE_BATTERY_PRESENT|AROS_APPLE_BATTERY_CURRENT|AROS_APPLE_BATTERY_ADAPTER;s.battery.current_ma=-500;s.battery.millivolts=12000;s.have_dcp=TRUE;s.have_idle=TRUE;
   LONG used=draw_all(&v,&s);assert(used>0&&used<v.canvas.bitmap->height);v.canvas.height=used;WidgetCanvasPresent(&v.canvas,v.window);
   for(int id=1;id<B_COUNT;id++){struct button*b=&v.buttons[id];assert(b->x>=0&&b->y>=0&&b->x+b->w<=v.canvas.width&&b->y+b->h<=used);}
   assert(energy_view_show(&v,&s));energy_resize_cases(&v);
   struct ScreenRenderContext nextctx=context;current=&nextctx;struct Window*oldwindow=v.window;
   stale_show=1;assert(retry_control(&v,&s,args,TRUE,TRUE));assert(v.window==oldwindow);stale_show=0;
   ordinal=0;assert(energy_view_replace(&v,&s,args));assert(v.window!=oldwindow);int replace_allocations=ordinal;
   assert(v.rp==&v.canvas.port);draw_all(&v,&s);
   energy_view_close(&v);assert(!live_fonts&&!live_bitmaps&&!live_windows);
   for(int fail=1;fail<=replace_allocations;fail++){current=&context;memset(&v,0,sizeof(v));v.screen=&screen;ordinal=0;failat=0;assert(open_energy_at(&v,0,0,args));oldwindow=v.window;current=&nextctx;ordinal=0;failat=fail;BOOL changed=energy_view_replace(&v,&s,args);if(!changed)assert(v.window==oldwindow);energy_view_close(&v);assert(!live_fonts&&!live_bitmaps&&!live_windows);}
   failat=0;current=&context;
   energy_view_close(&v);assert(!live_fonts&&!live_bitmaps&&!live_windows);
   for(int fail=1;fail<=allocations;fail++){memset(&v,0,sizeof(v));v.screen=&screen;ordinal=0;failat=fail;open_energy_at(&v,0,0,args);energy_view_close(&v);assert(!live_fonts&&!live_bitmaps&&!live_windows);}failat=0;
  }

  { /* A chosen size through a scale change, then resized at the new scale;
     * a refit below the new frame's minimum reopens at that minimum. */
   int target=scale>=300?scale-100:scale+100;struct ScreenRenderContext tctx=context;tctx.src_Scale=target;
   struct view v={0};v.screen=&screen;current=&context;exact_missing=0;ordinal=failat=0;
   assert(open_energy_at(&v,40,30,args));assert(energy_view_show(&v,&s));
   LONG mw,mh;energy_view_minimum(&v,&mw,&mh);LONG uw=mw+WidgetPixels(scale,41),uh=mh+WidgetPixels(scale,29);
   resize_to(v.window,uw,uh);WidgetPlaceChanged(&v.place,v.window,scale);v.canvas.top=v.lh*5;LONG oldtop=v.canvas.top;
   borrowed.tf_YSize=WidgetPixels(target,13);borrowed.tf_XSize=WidgetPixels(target,7);borrowed.baseline=borrowed.tf_YSize-2;
   LONG rw=(uw*target+scale/2)/scale,rh=(uh*target+scale/2)/scale;v.window->Width=rw;v.window->Height=rh;
   v.window->LeftEdge=40*target/100;v.window->TopEdge=30*target/100;current=&tctx;
   assert(energy_view_replace(&v,&s,args));assert(v.context==&tctx);
   LONG nw,nh;energy_view_minimum(&v,&nw,&nh);
   assert(labs(v.window->Width-maxl(rw,nw))<=target/100+1&&labs(v.window->Height-maxl(rh,nh))<=target/100+1);
   assert(v.window->ScaleGeometry.axis[AG_MINWIDTH].scale==(ULONG)target&&v.window->MinWidth==nw&&v.window->MinHeight==nh);
   {LONG ih=v.window->Height-v.window->BorderTop-v.window->BorderBottom,maxy=v.canvas.height>ih?v.canvas.height-ih:0,want=oldtop*target/scale;assert(v.canvas.top==(want<maxy?want:maxy));}
   energy_resize_cases(&v);
   v.window->Width=v.window->Height=target/100;WidgetPlaceChanged(&v.place,v.window,target);current=&context;
   borrowed.tf_YSize=WidgetPixels(scale,13);borrowed.tf_XSize=WidgetPixels(scale,7);borrowed.baseline=borrowed.tf_YSize-2;
   assert(energy_view_replace(&v,&s,args));energy_view_minimum(&v,&nw,&nh);
   assert(v.window->Width>=nw&&v.window->Width<=nw+scale/100+1&&v.window->Height>=nh&&v.window->Height<=nh+scale/100+1);
   assert(v.window->MinWidth==nw&&v.window->MinHeight==nh);
   energy_view_close(&v);assert(!live_fonts&&!live_bitmaps&&!live_windows);
  }
  struct view v={0};v.screen=&screen;stale_open=1;assert(!open_energy_at(&v,0,0,args));assert(!live_fonts&&!live_bitmaps&&!live_windows);stale_open=0;
  for(int failure=0;failure<=3;failure++){failat=failure;ordinal=0;struct AliveView*av=prepare_auto();close_alive_view(av);assert(!pins&&!live_bitmaps&&!live_windows&&!live_fonts&&!live_views);}failat=0;
  stale_open=1;assert(!prepare_auto());assert(!pins&&!live_bitmaps&&!live_views);stale_open=0;
  pubnode.psn_Flags=PSNF_PRIVATE;assert(!prepare_auto());assert(!pins&&!live_views);pubnode.psn_Flags=0;
  stale_show=1;assert(!open_alive_window());assert(!alive_view&&!pins&&!live_views&&!live_windows&&!live_bitmaps);stale_show=0;
  struct Window*aw=open_alive_window();assert(aw&&alive_view);struct AliveView*oldview=alive_view;
  stale_show=1;assert(!replace_alive_window(&aw,1));assert(alive_view==oldview&&aw==oldview->window&&pins==1&&live_bitmaps==1&&live_windows==1&&live_views==1);stale_show=0;
  for(int failure=1;failure<=3;failure++){ordinal=0;failat=failure;assert(!replace_alive_window(&aw,1));assert(alive_view==oldview&&pins==1&&live_bitmaps==1&&live_windows==1&&live_views==1);}failat=0;
  aw->Flags|=WFLG_WINDOWACTIVE;assert(replace_alive_window(&aw,1));assert(alive_view!=oldview&&aw==alive_view->window&&(aw->Flags&WFLG_WINDOWACTIVE));alive_resize_cases(alive_view);
  { int target=scale>=300?scale-100:scale+100;struct ScreenRenderContext tctx=context;tctx.src_Scale=target;
   LONG mw,mh;alive_minimum(alive_view,&mw,&mh);LONG uw=mw+WidgetPixels(scale,37),uh=mh+WidgetPixels(scale,23);
   resize_to(aw,uw,uh);WidgetPlaceChanged(&alive_view->place,aw,scale);
   borrowed.tf_YSize=WidgetPixels(target,13);borrowed.tf_XSize=WidgetPixels(target,7);borrowed.baseline=borrowed.tf_YSize-2;
   LONG rw=(uw*target+scale/2)/scale,rh=(uh*target+scale/2)/scale;aw->Width=rw;aw->Height=rh;current=&tctx;
   assert(replace_alive_window(&aw,1));assert(alive_view->context==&tctx&&aw==alive_view->window);
   LONG nw,nh;alive_minimum(alive_view,&nw,&nh);
   /* The user's size in 100 %% units, rounded once each way. */
   assert(labs(aw->Width-maxl(rw,nw))<=target/100+1&&labs(aw->Height-maxl(rh,nh))<=target/100+1);
   assert(aw->ScaleGeometry.axis[AG_MINWIDTH].scale==(ULONG)target&&aw->MinWidth==nw&&aw->MinHeight==nh);
   alive_resize_cases(alive_view);
   aw->Width=aw->Height=target/100;WidgetPlaceChanged(&alive_view->place,aw,target);current=&context;
   borrowed.tf_YSize=WidgetPixels(scale,13);borrowed.tf_XSize=WidgetPixels(scale,7);borrowed.baseline=borrowed.tf_YSize-2;
   assert(replace_alive_window(&aw,1));alive_minimum(alive_view,&nw,&nh);
   assert(aw->Width>=nw&&aw->Width<=nw+scale/100+1&&aw->Height>=nh&&aw->Height<=nh+scale/100+1&&aw->MinWidth==nw&&aw->MinHeight==nh);}
  close_alive_view(alive_view);alive_view=NULL;assert(!pins&&!live_views&&!live_bitmaps&&!live_windows);
  { /* Born automatic at this scale, both widgets sit in their slots apart;
     * through 100 %% and 200 %% replacements they move to the new slots, a
     * moved Energy keeps the user's geometry and a moved status widget too. */
   struct view e={0};e.screen=&screen;WidgetPlaceAutomatic(&e.place);current=&context;exact_missing=0;ordinal=failat=0;
   assert(energy_view_open(&e,args)&&energy_view_show(&e,&s));check_slot(e.window,&screen,&context,WIDGET_SLOT_ENERGY,&e.canvas);
   struct Window*a=open_alive_window();assert(a);check_slot(a,&screen,&context,WIDGET_SLOT_ALIVE,NULL);assert(apart(a,e.window));
   /* A viewport narrower than its page also scrolls with the horizontal wheel. */
   {LONG l0=e.canvas.left;assert(WidgetCanvasScroll(&e.canvas,0x7d,5)&&e.canvas.left==l0+5);assert(WidgetCanvasScroll(&e.canvas,0x7c,5)&&e.canvas.left==l0);}
   int targets[2]={scale==200?100:200,scale};
   for(int k=0;k<2;k++){struct ScreenRenderContext t=context;t.src_Scale=targets[k];t.src_PhysicalBarHeight=WidgetPixels(targets[k],18);
    borrowed.tf_YSize=WidgetPixels(targets[k],13);borrowed.tf_XSize=WidgetPixels(targets[k],7);borrowed.baseline=borrowed.tf_YSize-2;current=k?&context:&t;
    /* Intuition's refit of the old windows, which the slots replace. */
    e.window->LeftEdge=0;e.window->TopEdge=0;a->LeftEdge=0;a->TopEdge=0;
    assert(energy_view_replace(&e,&s,args)&&replace_alive_window(&a,1));
    check_slot(e.window,&screen,current,WIDGET_SLOT_ENERGY,&e.canvas);check_slot(a,&screen,current,WIDGET_SLOT_ALIVE,NULL);assert(apart(a,e.window));
    /* An event for the unchanged window (its own open) is not the user's. */
    WidgetPlaceChanged(&e.place,e.window,current->src_Scale);WidgetPlaceChanged(&alive_view->place,a,current->src_Scale);
    assert(e.place.Automatic&&alive_view->place.Automatic);}
   borrowed.tf_YSize=WidgetPixels(scale,13);borrowed.tf_XSize=WidgetPixels(scale,7);borrowed.baseline=borrowed.tf_YSize-2;
   /* The user moves Energy: its geometry survives a round trip through 200 %% (or 100 %%). */
   e.window->LeftEdge=WidgetPixels(scale,100);e.window->TopEdge=WidgetPixels(scale,60);e.window->Height=WidgetPixels(scale,300);WidgetPlaceChanged(&e.place,e.window,scale);assert(!e.place.Automatic);
   struct WidgetRect user=e.place.Logical;LONG ul=e.window->LeftEdge,ut=e.window->TopEdge,uw2=e.window->Width,uh2=e.window->Height;
   for(int k=0;k<2;k++){struct ScreenRenderContext t=context;t.src_Scale=targets[k];t.src_PhysicalBarHeight=WidgetPixels(targets[k],18);current=k?&context:&t;
    borrowed.tf_YSize=WidgetPixels(targets[k],13);borrowed.tf_XSize=WidgetPixels(targets[k],7);borrowed.baseline=borrowed.tf_YSize-2;
    e.window->LeftEdge=0;assert(energy_view_replace(&e,&s,args));assert(!e.place.Automatic&&!memcmp(&user,&e.place.Logical,sizeof user));}
   if(!(labs(e.window->LeftEdge-ul)<=1&&labs(e.window->TopEdge-ut)<=1&&labs(e.window->Width-uw2)<=1&&labs(e.window->Height-uh2)<=1))fprintf(stderr,"scale %d user %ld,%ld %ldx%ld now %ld,%ld %ldx%ld logical %ld,%ld %ldx%ld\n",scale,(long)ul,(long)ut,(long)uw2,(long)uh2,(long)e.window->LeftEdge,(long)e.window->TopEdge,(long)e.window->Width,(long)e.window->Height,(long)user.Left,(long)user.Top,(long)user.Width,(long)user.Height);
   assert(labs(e.window->LeftEdge-ul)<=1&&labs(e.window->TopEdge-ut)<=1&&labs(e.window->Width-uw2)<=1&&labs(e.window->Height-uh2)<=1);
   energy_view_close(&e);close_alive_view(alive_view);alive_view=NULL;current=&context;
   assert(!pins&&!live_views&&!live_bitmaps&&!live_windows&&!live_fonts);}
  struct AliveView sampleview={0};struct AliveView*view=&sampleview;
  view->font_height=borrowed.tf_YSize;view->cell=borrowed.tf_XSize+1;assert(WidgetCanvasCreate(&view->canvas,&screen,alive_x(view,ALIVE_WIDTH),alive_y(view,ALIVE_HEIGHT)));SetFont(&view->canvas.port,&borrowed);
  textcalls=0;alive_draw_text(view,&view->canvas.port,12,80,"BATTERY 100",11,ALIVE_DATE_SCALE);assert(textcalls==11);int before=textcalls;alive_draw_text(view,&view->canvas.port,12,80,"BATTERY 100",11,ALIVE_DATE_SCALE);assert(textcalls==before);alive_draw_text(view,&view->canvas.port,12,80,"BATTERY 099",11,ALIVE_DATE_SCALE);assert(textcalls==before+3);
  char longline[65];memset(longline,'W',64);longline[64]=0;alive_draw_text(view,&view->canvas.port,12,120,longline,64,ALIVE_DATE_SCALE);
  /* Every cell hit is inverted with the same scale used by its painted box. */
  for(LONG cell=0;cell<40;cell++){LONG x=alive_x(view,12+cell*12)+view->cell/2;assert((x*12/view->cell-12)/12==cell);}
  struct Window viewport={300,200,4,24,4,4,0};LONG cx,cy;view->canvas.left=80;view->canvas.top=80;
  assert(WidgetCanvasPoint(&view->canvas,&viewport,14,34,&cx,&cy));assert(cx==90&&cy==90);assert(!WidgetCanvasPoint(&view->canvas,&viewport,0,0,&cx,&cy));
  LONG rowtop=alive_y(view,ALIVE_PAD+ALIVE_GLYPH_H*ALIVE_SCALE+5*ALIVE_LINE_GAP+4*ALIVE_GLYPH_H*ALIVE_DATE_SCALE);
  ULONG cells[]={ALIVE_ROW_KBD_MINUS,ALIVE_ROW_KBD_PLUS,ALIVE_ROW_LCD_MINUS,ALIVE_ROW_LCD_PLUS,ALIVE_ROW_VOL_MINUS,ALIVE_ROW_VOL_PLUS};
  for(int i=0;i<6;i++){action=0;alive_click(view,alive_x(view,ALIVE_PAD)+cells[i]*view->cell+view->cell/2,rowtop+view->font_height/2);assert(action==i+1);}
  WidgetCanvasDelete(&view->canvas);assert(!live_fonts&&!live_bitmaps&&!live_windows);
 }
 puts("PASS: actual WindowFitInitializeGeometry/defaults Energy+alive canonical13births x13targets; actual widget canvas/Energy ownership+faults/13 scales/Energy+alive resize limits, scroll clamp, scale-change size/button baseline/graph history+stripes/alive retained-font cells");return 0;
}
'''
parts += [main]
with tempfile.TemporaryDirectory() as td:
 out=args.out or pathlib.Path(td);out.mkdir(parents=True,exist_ok=True)
 c=out/'nativewidgets.c';c.write_text('\n'.join(parts));exe=c.with_suffix('')
 subprocess.run(['clang','-std=gnu11','-fsanitize=address,undefined','-g','-O1','-Wno-incompatible-pointer-types','-Wno-pointer-sign',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
 if args.mutations:
  mutations=[
   ('energy-minimum-default','geometry.MinWidth = geometry.MinHeight = 1;','geometry.MinWidth = geometry.MinHeight = 0;'),
   ('alive-minimum-default','geometry.Height = rect.Height > 0 ? rect.Height : ALIVE_HEIGHT;\n    /* The canvas scrolls independently of this viewport. Zero would make\n     * OpenWindow freeze the initial (possibly full-panel) extent as minimum. */\n    geometry.MinWidth = geometry.MinHeight = 1;', 'geometry.Height = rect.Height > 0 ? rect.Height : ALIVE_HEIGHT; geometry.MinWidth = geometry.MinHeight = 0;'),
   ('font-size-unscaled','ta.ta_YSize = WidgetPixels(scale, ta.ta_YSize);','ta.ta_YSize = ta.ta_YSize;'),
   ('button-baseline','(h - rp->TxHeight) / 2 + rp->TxBaseline','(h - rp->TxHeight) / 2'),
   ('history-on-redraw','if (tick)\n                sample_power(s);','if (TRUE)\n                sample_power(s);'),
   ('graph-bands','/ STRIPE) & 1','/ (STRIPE+1)) & 1'),
   ('no-horizontal-wheel','case 0x4e: case 0x7d:','case 0x4e:'),
   ('scroll-unbounded','if (canvas->top > maxy) canvas->top = maxy;','if (FALSE) canvas->top = maxy;'),
   ('font-reference-leak','if (v->font) CloseFont(v->font);','if (FALSE) CloseFont(v->font);'),
   ('alive-stale-bitmap-leak','if (!view->window) goto fail;','if (!view->window) return NULL;'),
   ('energy-dangling-rport','v->rp = &v->canvas.port;','v->rp = v->rp;'),
   ('energy-stop-after-failure','energy_view_replace(v, s, args);','if(!energy_view_replace(v, s, args)) running=FALSE;'),
   ('alive-no-show-gate','if (!ShowWindow(next->window, NULL)) {','if (FALSE) {'),
   ('energy-resize-no-gadget','{ WA_SizeGadget, TRUE }, { WA_SizeBBottom, TRUE },\n            { WA_Flags','{ WA_Flags'),
   ('energy-resize-no-newsize','IDCMP_NEWSIZE | IDCMP_CHANGEWINDOW | IDCMP_RAWKEY','IDCMP_CHANGEWINDOW | IDCMP_RAWKEY'),
   ('energy-resize-no-erase','    if (inner_w > shown_w && inner_h > 0)\n        RectFill(w->RPort','    if (FALSE)\n        RectFill(w->RPort'),
   ('energy-resize-no-limits','if (!WindowLimits(v->window, min_width, min_height, (UWORD)-1, (UWORD)-1)) goto fail;','(void)min_height;'),
   ('energy-user-geometry-lost','WidgetPlaceCopy(&next.place, &v->place);','WidgetPlaceAutomatic(&next.place);'),
   ('energy-refit-place-kept','WidgetPlaceCopy(&next.place, &v->place);','WidgetPlaceUser(&next.place, v->window->LeftEdge * 100 / next.screen->Scale, v->window->TopEdge * 100 / next.screen->Scale, 0, 0);'),
   ('energy-not-narrowed-to-page','            if (!again && v->place.Automatic) {','            if (FALSE) {'),
   ('energy-move-not-recorded','    WidgetPlaceOpened(&v->place, v->window);\n    return TRUE;','    return TRUE;'),
   ('energy-resize-no-reopen','            if (!again) break;','            if (TRUE) break;'),
   ('alive-resize-no-gadget','{ WA_SizeGadget, TRUE }, { WA_SizeBBottom, TRUE },\n            { TAG_DONE','{ TAG_DONE'),
   ('alive-resize-no-newsize','IDCMP_RAWKEY | IDCMP_NEWSIZE |\n                IDCMP_CHANGEWINDOW','IDCMP_RAWKEY |\n                IDCMP_CHANGEWINDOW'),
   ('alive-resize-no-erase','    if (inner_h > shown_h && shown_w > 0)\n        RectFill(window->RPort','    if (FALSE)\n        RectFill(window->RPort'),
   ('alive-resize-no-limits','if (!WindowLimits(view->window, min_width, min_height, (UWORD)-1, (UWORD)-1)) goto fail;','(void)min_height;'),
   ('alive-resize-size-lost','prepare_alive_view(&old->place);','({struct WidgetPlace automatic;WidgetPlaceAutomatic(&automatic);prepare_alive_view(&automatic);});'),
   ('alive-move-not-recorded','    WidgetPlaceOpened(&view->place, view->window);\n    return view;','    return view;'),
   ('alive-refit-place-kept','prepare_alive_view(&old->place);','({struct WidgetPlace refit;WidgetPlaceUser(&refit,old->window->LeftEdge*100/old->context->src_Scale,old->window->TopEdge*100/old->context->src_Scale,0,0);prepare_alive_view(&refit);});'),
   ('alive-resize-no-reopen','            if (attempt) goto fail;\n            if (view->window->Width < min_width)','            if (TRUE) break;\n            if (view->window->Width < min_width)'),
  ]
  original='\n'.join(parts)
  for name,old,new in mutations:
   assert old in original,name
   mutant=out/(name+'.c');mutant.write_text(original.replace(old,new,1));mutant_exe=out/name
   subprocess.run(['clang','-std=gnu11','-fsanitize=address,undefined','-g','-O1','-Wno-incompatible-pointer-types','-Wno-pointer-sign',str(mutant),'-o',str(mutant_exe)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
   run=subprocess.run([str(mutant_exe)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
   (out/(name+'.log')).write_bytes(run.stdout+run.stderr)
   assert run.returncode!=0,'mutation escaped: '+name
   print('REJECTED:',name)

