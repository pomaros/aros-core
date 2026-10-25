#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Run actual native Magnifier draw/open/cleanup bodies with bounded OS adapters."""
import argparse
import hashlib
import pathlib
import re
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
from nativewidget_geometry_fixture import header as geometry_header, bodies as geometry_bodies

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[4])
p.add_argument('--negative-controls', action='store_true')
p.add_argument('--out', type=pathlib.Path)
a = p.parse_args()
source = a.root / 'arch/aarch64-apple/c/Magnifier.c'
s = source.read_text()
bodies = s[s.index('struct view {'):s.index('\nint main(void)')]
constants = s[s.index('#define WIDGET_WIDTH'):s.index('struct Library *CyberGfxBase;')]
place = (a.root / 'arch/aarch64-apple/include/widgetplace.h').read_text()
tags = sorted(set(re.findall(r'\b(?:WA_|GTVI_|GTTX_|IDCMP_|WFLG_|BMA_)[A-Za-z0-9_]+', bodies)))
# IDCMP classes and window flags are masks: give each its own bit.
defines = '\n'.join(f'#define {name} (1UL << {i})' if name.startswith(('IDCMP_', 'WFLG_'))
                    else f'#define {name} {i + 10}' for i, name in enumerate(tags))
c = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef int64_t QUAD;typedef int BOOL; typedef int16_t WORD; typedef uint16_t UWORD;
typedef int32_t LONG; typedef uint32_t ULONG; typedef uintptr_t IPTR;
typedef uint8_t UBYTE; typedef void *APTR; typedef void Object;
#define TRUE 1
#define FALSE 0
#define TAG_DONE 0
#define MEMF_ANY 0
#define RECTFMT_ARGB 0
#define BUTTONIDCMP 0
#define GTJ_CENTER 1
#define BUTTON_KIND 1
#define TEXT_KIND 2
#define AROS_LONG2BE(x) (x)
#define FSF_TAGGED 128
#define BACKGROUNDPEN 0
struct TextFont { UWORD tf_YSize; struct {struct {char *ln_Name;} mn_Node;} tf_Message; UBYTE tf_Style,tf_Flags; APTR tf_Extension; };
struct TextAttr {char *ta_Name;UWORD ta_YSize;UBYTE ta_Style,ta_Flags;};
struct TTextAttr {char *tta_Name;UWORD tta_YSize;UBYTE tta_Style,tta_Flags;struct TagItem *tta_Tags;};
struct TextFontExtension {struct TagItem *tfe_Tags;};
struct DrawInfo { struct TextFont *dri_Font; UWORD *dri_Pens; };
struct ScreenRenderContext { UWORD src_Scale; struct DrawInfo *src_PhysicalDrawInfo; ULONG src_PhysicalBarHeight; };
struct BitMap { LONG width,height; };
struct RastPort { struct BitMap *BitMap; };
struct Screen { struct RastPort RastPort; WORD MouseX,MouseY;LONG Width,Height;ULONG Scale; };
struct Gadget { struct Gadget *next; UWORD GadgetID; LONG x,y,w,h; const char *text; };
struct Window { WORD LeftEdge,TopEdge,Width,Height,BorderLeft,BorderRight,BorderTop,BorderBottom; ULONG Flags; struct RastPort *RPort;struct Screen *WScreen;BOOL ScaleAware,ScaleGeometryEnabled;struct AwareGeometry ScaleGeometry;LONG MinWidth,MinHeight,MaxWidth,MaxHeight; };
struct NewGadget { struct TextAttr *ng_TextAttr; APTR ng_VisualInfo; LONG ng_TopEdge,ng_LeftEdge,ng_Height,ng_Width; UBYTE *ng_GadgetText; UWORD ng_GadgetID; };
struct ScaleWindowGeometry { LONG Left,Top,Width,Height,MinWidth,MinHeight,MaxWidth,MaxHeight; };
struct NewWindow{LONG LeftEdge,TopEdge,Width,Height,MinWidth,MinHeight,MaxWidth,MaxHeight;};
struct TagItem { IPTR ti_Tag, ti_Data; };
static const struct ScreenRenderContext *active;
static int fail_at, calls, live, reads, writes, stale_open, opened_sizegadget, stale_show;
static IPTR opened_idcmp;static LONG fill_area;static struct Window *drawn;
static LONG sample_x,sample_y,sample_w,sample_h;
static struct Gadget *controls[3];
static int control_count;
static void *owned(size_t n) { calls++; if(calls==fail_at)return NULL; void *v=calloc(1,n); assert(v);live++; return v; }
static void released(void *v){if(v){live--;free(v);}}
static IPTR tag(const struct TagItem *t, IPTR name){for(;t->ti_Tag;t++)if(t->ti_Tag==name)return t->ti_Data;return 0;}
'''+defines+r'''
#define SA_RenderContext 9999
static BOOL GetAttr(IPTR name,Object *object,IPTR *value){assert(name==SA_RenderContext&&object);*value=(IPTR)active;return TRUE;}
static LONG GetBitMapAttr(struct BitMap *b,IPTR name){return name==BMA_WIDTH?b->width:b->height;}
static APTR GetVisualInfoA(struct Screen *screen,struct TagItem *tags){assert(screen);assert(tag(tags,GTVI_RenderContext)==(IPTR)active);return owned(1);}
static void FreeVisualInfo(APTR v){released(v);}
static struct Window *OpenWindowTagList(void *unused,struct TagItem *tags){
 assert(!unused&&tag(tags,WA_ScaleAware)&&tag(tags,WA_Hidden)&&!tag(tags,WA_Activate));
 if(stale_open || tag(tags,WA_RenderContext)!=(IPTR)active)return NULL;
 struct Window *w=owned(sizeof(*w));if(!w)return NULL;
 const struct ScaleWindowGeometry *g=(const void *)tag(tags,WA_ScaleGeometry);
 struct Screen *screen=(void*)tag(tags,WA_PubScreen);assert(screen);
 screen->Width=screen->RastPort.BitMap->width;screen->Height=screen->RastPort.BitMap->height;screen->Scale=active->src_Scale;
 w->WScreen=screen;w->ScaleAware=TRUE;
 w->BorderLeft=w->BorderRight=w->BorderBottom=(4*active->src_Scale+50)/100;
 w->BorderTop=active->src_PhysicalDrawInfo->dri_Font->tf_YSize+(5*active->src_Scale+50)/100;
 opened_idcmp=tag(tags,WA_IDCMP);opened_sizegadget=tag(tags,WA_SizeGadget)&&tag(tags,WA_SizeBBottom);
 if(opened_sizegadget)w->BorderBottom=(16*active->src_Scale+50)/100;
 struct NewWindow initial={0};if(!WindowFitInitializeGeometry(w,g,screen->Width,screen->Height,TRUE,&initial)){released(w);return NULL;}
 w->LeftEdge=initial.LeftEdge;w->TopEdge=initial.TopEdge;w->Width=initial.Width;w->Height=initial.Height;
 w->MinWidth=initial.MinWidth;w->MinHeight=initial.MinHeight;w->MaxWidth=initial.MaxWidth;w->MaxHeight=initial.MaxHeight;
 return w;
}
static void CloseWindow(struct Window *w){released(w);}
static BOOL ShowWindow(struct Window *w,void *unused){assert(w&&!unused);return !stale_show;}
static void ActivateWindow(struct Window *w){w->Flags|=WFLG_WINDOWACTIVE;}
static void SetAPen(struct RastPort *rp,UWORD pen){assert(!rp&&pen==7);}
/* The erased part of the picture area: inside the window frame, under the row. */
static void RectFill(struct RastPort *rp,LONG x,LONG y,LONG x1,LONG y1){
 assert(!rp&&drawn&&x<=x1&&y<=y1&&x>=drawn->BorderLeft&&x1<drawn->Width-drawn->BorderRight&&y1<drawn->Height-drawn->BorderBottom);
 fill_area+=(x1-x+1)*(y1-y+1);}
static struct Gadget *CreateContext(struct Gadget **g){*g=owned(sizeof(**g));return *g;}
static struct Gadget *CreateGadgetA(int kind,struct Gadget *prev,struct NewGadget *ng,struct TagItem *tags){
 const struct TextFont *f=active->src_PhysicalDrawInfo->dri_Font;
 const struct TTextAttr *a=(const void*)ng->ng_TextAttr;
 assert(a && a->tta_Name==f->tf_Message.mn_Node.ln_Name && a->tta_YSize==f->tf_YSize);
 assert(a->tta_Style==f->tf_Style && a->tta_Flags==f->tf_Flags);
 assert(a->tta_Tags==((struct TextFontExtension*)f->tf_Extension)->tfe_Tags);
 assert(prev);struct Gadget *g=owned(sizeof(*g));if(!g)return NULL;prev->next=g;
 assert(kind==BUTTON_KIND||kind==TEXT_KIND);g->GadgetID=ng->ng_GadgetID;
 g->x=ng->ng_LeftEdge;g->y=ng->ng_TopEdge;g->w=ng->ng_Width;g->h=ng->ng_Height;
 if(kind==TEXT_KIND){g->text=(const char *)tag(tags,GTTX_Text);assert(g->text && strchr(g->text,'X'));}
 assert(control_count<3);controls[control_count++]=g;return g;
}
static void FreeGadgets(struct Gadget *g){while(g){struct Gadget *next=g->next;released(g);g=next;}}
static APTR AllocVec(ULONG size,ULONG flags){assert(!flags);return owned(size);}
static void FreeVec(APTR v){released(v);}
static void AddGList(struct Window *w,struct Gadget *g,int a,int b,void *r){assert(w&&g&&a==-1&&b==-1&&!r);}
static void GT_RefreshWindow(struct Window *w,void *r){assert(w&&!r);}
static void GT_SetGadgetAttrs(struct Gadget *g,struct Window *w,void *r,...){assert(g&&g->text&&w&&!r);}
static ULONG pixel(LONG x,LONG y){return 0xff000000u|((ULONG)y<<12)|(ULONG)x;}
static void ReadPixelArray(ULONG *dst,int a,int b,LONG stride,struct RastPort *rp,LONG x,LONG y,LONG width,LONG height,int format){
 assert(!a&&!b&&stride==width*4&&format==RECTFMT_ARGB);
 assert(x>=0&&y>=0&&x+width<=rp->BitMap->width&&y+height<=rp->BitMap->height);
 sample_x=x;sample_y=y;sample_w=width;sample_h=height;reads++;
 for(LONG j=0;j<height;j++)for(LONG i=0;i<width;i++)dst[j*width+i]=pixel(x+i,y+j);
}
static void WritePixelArray(ULONG *dst,int a,int b,LONG stride,struct RastPort *rp,LONG x,LONG y,LONG width,LONG height,int format){
 assert(dst&&!a&&!b&&stride==width*4&&!rp&&x>=0&&y>=0&&format==RECTFMT_ARGB&&height>0);writes++;
}
'''+place+constants+bodies+r'''
static void verify_picture(struct view *v,LONG mx,LONG my){
 draw(v,mx,my);LONG zoom=(factors[v->index]*v->scale+50)/100;
 LONG cx=(mx-sample_x)*zoom+zoom/2,cy=(my-sample_y)*zoom+zoom/2;
 LONG reach=(12*v->scale+50)/100,gap=(4*v->scale+50)/100,thick=(v->scale+50)/100;
 for(LONG y=0;y<v->height;y++)for(LONG x=0;x<v->width;x++){
   ULONG expect=pixel(sample_x+x/zoom,sample_y+y/zoom);
   if(cx>=0&&cx<v->width&&cy>=0&&cy<v->height){
     /* Independent point-membership oracle for the horizontal/vertical strokes. */
     LONG dx=x-cx,dy=y-cy;
     if((y>=cy+thick && y<cy+2*thick && dx>=-reach&&dx<=reach&&!(dx>-gap&&dx<gap)) ||
        (x>=cx+thick && x<cx+2*thick && dy>=-reach&&dy<=reach&&!(dy>-gap&&dy<gap)))expect=WHITE;
     if((y>=cy && y<cy+thick && dx>=-reach&&dx<=reach&&!(dx>-gap&&dx<gap)) ||
        (x>=cx && x<cx+thick && dy>=-reach&&dy<=reach&&!(dy>-gap&&dy<gap)))expect=RED;
   }
   assert(v->picture[y*v->width+x]==expect);
 }
}

static void resize_to(struct Window*w,LONG width,LONG height){
 if(width<w->MinWidth)width=w->MinWidth;if(height<w->MinHeight)height=w->MinHeight;
 if(width>w->MaxWidth)width=w->MaxWidth;if(height>w->MaxHeight)height=w->MaxHeight;
 w->Width=width;w->Height=height;
 w->ScaleGeometry.axis[AG_WIDTH]=(struct AwareGeometryValue){width,w->WScreen->Scale};
 w->ScaleGeometry.axis[AG_HEIGHT]=(struct AwareGeometryValue){height,w->WScreen->Scale};}
static LONG maxl(LONG a,LONG b){return a>b?a:b;}
static BOOL open_at(struct view*v,LONG left,LONG top){WidgetPlaceUser(&v->place,left,top,0,0);return open_view(v);}
static LONG inner_w(const struct view*v){return v->window->Width-v->x-v->window->BorderRight;}
static LONG inner_h(const struct view*v){return v->window->Height-v->y-v->window->BorderBottom;}
/* Limits, the button row at the minimum, a larger area at the same factor,
 * and a refused growth that keeps the old buffers and erases the rest. */
static void resize_cases(struct view*v){
 LONG mw,mh;view_minimum(v,&mw,&mh);struct Window*w=v->window;
 assert(opened_sizegadget&&(opened_idcmp&IDCMP_NEWSIZE));
 assert(w->MinWidth==mw&&w->MinHeight==mh&&w->ScaleGeometry.axis[AG_MINWIDTH].scale==v->scale&&w->ScaleGeometry.axis[AG_MINHEIGHT].scale==v->scale);
 assert(w->ScaleGeometry.axis[AG_MAXWIDTH].value==-1&&w->ScaleGeometry.axis[AG_MAXHEIGHT].value==-1);
 int index=v->index;resize_to(w,1,1);assert(w->Width==mw&&w->Height==mh);drawn=w;resize_view(v);
 assert(v->width==inner_w(v)&&v->height==inner_h(v)&&v->height>=1&&(ULONG)v->width*v->height<=v->capacity);
 for(int i=0;i<3;i++){struct Gadget*g=controls[i];assert(g->x>=w->BorderLeft&&g->x+g->w<=w->Width-w->BorderRight&&g->y>=w->BorderTop&&g->y+g->h<=v->y);}
 verify_picture(v,500,450);
 LONG before=sample_w*sample_h;resize_to(w,w->MaxWidth,w->MaxHeight);resize_view(v);
 assert(v->width==inner_w(v)&&v->height==inner_h(v)&&(ULONG)v->width*v->height<=v->capacity&&v->index==index);
 LONG zoom=(factors[v->index]*v->scale+50)/100;
 assert(sample_w==(v->width+zoom-1)/zoom&&sample_h==(v->height+zoom-1)/zoom&&sample_w*sample_h>before);
 verify_picture(v,0,0);verify_picture(v,999,899);
 resize_to(w,mw+(40*v->scale+50)/100,mh+(30*v->scale+50)/100);resize_view(v);verify_picture(v,10,10);
 LONG oldw=v->width,oldh=v->height;
 assert((ULONG)(w->MaxWidth-v->x-w->BorderRight)*(ULONG)(w->MaxHeight-v->y-w->BorderBottom)>(ULONG)(oldw*oldh));{
  /* Buffers of exactly the current area: the next growth must allocate. */
  FreeVec(v->source);FreeVec(v->picture);v->source=AllocVec(oldw*oldh*4,0);v->picture=AllocVec(oldw*oldh*4,0);v->capacity=oldw*oldh;
  resize_to(w,w->MaxWidth,w->MaxHeight);fill_area=0;fail_at=calls+1;assert(!layout_view(v));fail_at=0;
  assert(v->width==oldw&&v->height==oldh&&(LONG)v->width*v->height+fill_area==inner_w(v)*inner_h(v));
  verify_picture(v,500,450);resize_view(v);assert(v->width==inner_w(v)&&v->height==inner_h(v));verify_picture(v,500,450);}
 drawn=NULL;}
int main(void){
 struct BitMap bitmap={1000,900};struct Screen screen={{&bitmap},500,450};screen.Width=1000;screen.Height=900;
 struct TagItem tags[]={{1234,4321},{0,0}};struct TextFontExtension extension={tags};
 struct TextFont font={13,{{"tagged.font"}},FSF_TAGGED|2,4,&extension};UWORD pens[]={7};struct DrawInfo di={&font,pens};struct ScreenRenderContext ctx={100,&di};active=&ctx;
 int scales=0,faults=0;
 for(int scale=100;scale<=400;scale+=25){
  ctx.src_Scale=scale;font.tf_YSize=(13*scale+50)/100;
  for(int index=0;index<7;index++){
   struct view v={0};v.screen=&screen;v.index=index;calls=control_count=0;
   assert(open_at(&v,100,30));assert(live==9);VerifyWidgetGeometry(v.window);assert(v.window->ScaleGeometry.axis[AG_MINWIDTH].value==v.window->MinWidth&&v.window->ScaleGeometry.axis[AG_MINHEIGHT].value==v.window->MinHeight);assert(v.window->ScaleGeometry.axis[AG_MINWIDTH].scale==(ULONG)scale&&v.window->ScaleGeometry.axis[AG_MINHEIGHT].scale==(ULONG)scale);
   assert(v.context==active&&v.scale==(ULONG)scale&&v.raster_width==1000&&v.raster_height==900);
   for(int i=0;i<3;i++){struct Gadget *g=controls[i];assert(g->h==font.tf_YSize+(4*scale+50)/100);assert(g->w==((i==1?48:40)*scale+50)/100);assert(g->x>=v.window->BorderLeft&&g->x+g->w<=v.window->Width-v.window->BorderRight);assert(g->y+g->h<=v.y);}
   verify_picture(&v,0,0);verify_picture(&v,500,450);verify_picture(&v,999,899);
   close_view(&v);assert(!live&&!v.window&&!v.picture&&!v.gadgets);
  }

  { /* resize, then the chosen size through a scale change and a resize there */
   int target=scale>=300?scale-100:scale+100;struct ScreenRenderContext tctx=ctx;tctx.src_Scale=target;
   struct view v={0};v.screen=&screen;v.index=3;calls=control_count=0;assert(open_at(&v,100,30));resize_cases(&v);
   LONG mw,mh;view_minimum(&v,&mw,&mh);LONG uw=mw+(57*scale+50)/100,uh=mh+(33*scale+50)/100;resize_to(v.window,uw,uh);WidgetPlaceChanged(&v.place,v.window,scale);resize_view(&v);
   LONG rw=(uw*target+scale/2)/scale,rh=(uh*target+scale/2)/scale;v.window->Width=rw;v.window->Height=rh;v.window->LeftEdge=100*target/scale;v.window->TopEdge=30*target/scale;
   v.window->Flags|=WFLG_WINDOWACTIVE;font.tf_YSize=(13*target+50)/100;active=&tctx;control_count=0;
   stale_show=1;struct Window*oldw=v.window;assert(!replace_view(&v));assert(v.window==oldw&&v.context==&ctx);stale_show=0;control_count=0;
   assert(replace_view(&v));assert(v.context==&tctx&&v.scale==(ULONG)target&&v.index==3&&(v.window->Flags&WFLG_WINDOWACTIVE));
   LONG nw,nh;view_minimum(&v,&nw,&nh);
   /* The user's size in 100 % units, rounded once each way. */
   assert(labs(v.window->Width-maxl(rw,nw))<=target/100+1&&labs(v.window->Height-maxl(rh,nh))<=target/100+1);
   assert(v.width==inner_w(&v)&&v.height==inner_h(&v));resize_cases(&v);
   v.window->Width=v.window->Height=target/100;WidgetPlaceChanged(&v.place,v.window,target);font.tf_YSize=(13*scale+50)/100;active=&ctx;control_count=0;
   assert(replace_view(&v));view_minimum(&v,&nw,&nh);
   assert(v.window->Width>=nw&&v.window->Width<=nw+scale/100+1&&v.window->Height>=nh&&v.window->Height<=nh+scale/100+1&&v.window->MinWidth==nw&&v.window->MinHeight==nh);
   close_view(&v);assert(!live);
  }
  { /* Born automatic in its slot below the status widget's; replaced at
     * another scale it takes that scale's slot; an event for the unchanged
     * window keeps it automatic; a user move survives a round trip. */
   int target=scale==200?100:200;struct ScreenRenderContext tctx=ctx;tctx.src_Scale=target;tctx.src_PhysicalBarHeight=(18*target+50)/100;
   ctx.src_PhysicalBarHeight=(18*scale+50)/100;
   struct view v={0};v.screen=&screen;v.index=2;calls=control_count=0;WidgetPlaceAutomatic(&v.place);assert(open_view(&v)&&ShowWindow(v.window,NULL));
   const struct ScreenRenderContext*steps[3]={&ctx,&tctx,&ctx};
   for(int k=0;k<3;k++){
    if(k){font.tf_YSize=(13*steps[k]->src_Scale+50)/100;active=steps[k];control_count=0;v.window->LeftEdge=0;v.window->TopEdge=0;assert(replace_view(&v));}
    struct WidgetRect r,alive;assert(WidgetScreenSlot(&screen,active,WIDGET_SLOT_MAGNIFIER,&r)&&WidgetScreenSlot(&screen,active,WIDGET_SLOT_ALIVE,&alive));
    LONG sc=active->src_Scale,l=(r.Left*sc+50)/100,t=(r.Top*sc+50)/100,w=((r.Left+r.Width)*sc+50)/100-l,h=((r.Top+r.Height)*sc+50)/100-t,mw,mh;view_minimum(&v,&mw,&mh);
    if(w>=mw&&h>=mh){
     if(labs(v.window->LeftEdge-l)>1||labs(v.window->TopEdge-t)>1||labs(v.window->Width-w)>1||labs(v.window->Height-h)>1){fprintf(stderr,"Magnifier %d,%d %dx%d not in its slot %ld,%ld %ldx%ld at %ld%%\n",v.window->LeftEdge,v.window->TopEdge,v.window->Width,v.window->Height,(long)l,(long)t,(long)w,(long)h,(long)sc);abort();}
     assert(v.window->TopEdge>=((alive.Top+alive.Height)*sc+50)/100);}
    WidgetPlaceChanged(&v.place,v.window,sc);assert(v.place.Automatic);}
   v.window->LeftEdge=(10*scale+50)/100;v.window->TopEdge=(40*scale+50)/100;WidgetPlaceChanged(&v.place,v.window,scale);assert(!v.place.Automatic);
   struct WidgetRect user=v.place.Logical;
   for(int k=1;k<3;k++){font.tf_YSize=(13*steps[k]->src_Scale+50)/100;active=steps[k];control_count=0;v.window->LeftEdge=0;assert(replace_view(&v));assert(!v.place.Automatic&&!memcmp(&user,&v.place.Logical,sizeof user));}
   assert(labs(v.window->LeftEdge-(10*scale+50)/100)<=1&&labs(v.window->TopEdge-(40*scale+50)/100)<=1);
   close_view(&v);assert(!live);active=&ctx;font.tf_YSize=(13*scale+50)/100;
  }
  for(int cut=1;cut<=9;cut++){
   struct view v={0};v.screen=&screen;calls=control_count=0;fail_at=cut;
   assert(!open_view(&v));assert(!live&&!v.window&&!v.picture&&!v.gadgets);faults++;
  }
  fail_at=0;scales++;
 }
 ctx.src_Scale=200;font.tf_YSize=34;
 struct view v={0};v.screen=&screen;calls=control_count=0;
 assert(open_view(&v));assert(v.bar==54&&controls[0]->h==42);close_view(&v);assert(!live);
 struct BitMap small={200,900};screen.RastPort.BitMap=&small;ctx.src_Scale=400;calls=control_count=0;
 assert(!open_view(&v));assert(!live&&!v.window&&!v.gadgets);screen.RastPort.BitMap=&bitmap;
 stale_open=TRUE;control_count=0;assert(!open_view(&v));assert(!live);
 printf("PASS actualMagnifier scales=%d zooms=7 corners=3 resize=limits+row+area+oom+scalechange alloccuts=%d same_scale_font=34 stale_constructor=refused reads=%d writes=%d\n",scales,faults,reads,writes);
}
'''
loop=s[s.index('    while (running) {'):s.index('\nout:')]
assert re.search(r'class == IDCMP_NEWSIZE \|\| class == IDCMP_CHANGEWINDOW\) \{\n\s+if \(current_context\(v\.screen\) == v\.context\) \{\n(?:\s+/\*[^*]*\*/\n)?\s+WidgetPlaceChanged\(&v\.place, v\.window, v\.scale\);\n\s+if \(class == IDCMP_NEWSIZE\)\n\s+resize_view\(&v\);',loop),'NEWSIZE/CHANGEWINDOW dispatch'
assert 'replace_view(&v)' in loop,'scale change replaces through replace_view'
c=c.replace('struct TextFont {',geometry_header(a.root)+'\nstruct TextFont {',1)
c=c.replace('static struct Window *OpenWindowTagList',geometry_bodies(a.root)+'\nstatic struct Window *OpenWindowTagList',1)
with tempfile.TemporaryDirectory(prefix='magnifierscale-') as tmp:
    tmp=a.out or pathlib.Path(tmp);tmp.mkdir(parents=True,exist_ok=True); (tmp/'test.c').write_text(c)
    subprocess.run(['cc','-std=c11','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
    if a.negative_controls:
        mutants = [
            ('minimum-default', 'geometry.MinWidth = geometry.MinHeight = 1;', 'geometry.MinWidth = geometry.MinHeight = 0;'),
            ('minimum-tracking', 'WindowFitRecordLimits(window, AG_MINWIDTH, FALSE);', '(void)window;'),
            ('zoom', 'UWORD zoom = (factors[v->index] * v->scale + 50) / 100;', 'UWORD zoom = factors[v->index];'),
            ('raster', 'v->raster_width = GetBitMapAttr(v->screen->RastPort.BitMap, BMA_WIDTH);', 'v->raster_width = GetBitMapAttr(v->screen->RastPort.BitMap, BMA_HEIGHT);'),
            ('control-height', 'ng.ng_Height = fh + pixels(v, 4);', 'ng.ng_Height = fh + 4;'),
            ('text-lifetime', '{ GTTX_Text, (IPTR)v->label_text }', '{ GTTX_Text, 0 }'),
            ('tagged-font', 'ng.ng_TextAttr = (struct TextAttr *)&fontattr;', 'ng.ng_TextAttr = NULL;'),
            ('resize-no-gadget', '{ WA_SizeGadget, TRUE }, { WA_SizeBBottom, TRUE },\n', ''),
            ('resize-no-newsize', 'IDCMP_NEWSIZE | IDCMP_CHANGEWINDOW | IDCMP_CHANGESCALE', 'IDCMP_CHANGEWINDOW | IDCMP_CHANGESCALE'),
            ('resize-stale-area', '    v->width = width;\n    v->height = height;\n    return complete;', '    return complete;'),
            ('resize-capacity', 'if ((ULONG)width * (ULONG)height > v->capacity) {', 'if (FALSE) {'),
            ('resize-oom-no-erase', '        if (full_width > width)\n', '        if (FALSE)\n'),
            ('resize-no-limits', 'if (!WindowLimits(v->window, min_width, min_height, (UWORD)-1, (UWORD)-1)) goto fail;', '(void)min_height;'),
            ('resize-size-lost', 'WidgetPlaceCopy(&next.place, &v->place);', 'WidgetPlaceAutomatic(&next.place);'),
            ('refit-place-kept', 'WidgetPlaceCopy(&next.place, &v->place);', 'WidgetPlaceUser(&next.place, v->window->LeftEdge * 100 / v->scale, v->window->TopEdge * 100 / v->scale, 0, 0);'),
            ('move-not-recorded', '    WidgetPlaceOpened(&v->place, v->window);\n', ''),
            ('slot-of-the-status-widget', 'WIDGET_SLOT_MAGNIFIER, &place))', 'WIDGET_SLOT_ALIVE, &place))'),
            ('resize-no-reopen', 'if (attempt) goto fail;', 'if (TRUE) break;'),
            ('resize-zoom-lost', '    layout_view(v);\n    draw(v, v->mx, v->my);', '    layout_view(v);\n    v->index = 0;\n    draw(v, v->mx, v->my);'),
        ]
        for name, before, after in mutants:
            assert c.count(before) == 1, name
            changed=c.replace(before,after)
            if name=='minimum-default':changed=changed.replace('if (!WindowLimits(v->window,','if (FALSE && !WindowLimits(v->window,',1)
            mutant=tmp/(name+'.c');mutant.write_text(changed);mutant_exe=tmp/name
            subprocess.run(['cc','-std=c11','-g','-O1','-fsanitize=address,undefined',str(mutant),'-o',str(mutant_exe)],check=True)
            r = subprocess.run([str(mutant_exe)],capture_output=True)
            (tmp/(name+'.log')).write_bytes(r.stdout+r.stderr)
            assert r.returncode != 0, name
            print('REJECT mutant='+name)

print('source_sha256='+hashlib.sha256(s.encode()).hexdigest())
