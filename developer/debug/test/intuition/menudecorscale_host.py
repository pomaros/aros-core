#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise actual typed shipped MenuDecor image/popup preparation and cleanup."""
from pathlib import Path
import argparse, json, os, re, subprocess, tempfile
from menuscale_host import function
ROOT=Path(__file__).resolve().parents[4]
PREAMBLE=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;typedef int32_t LONG;typedef uint32_t ULONG;typedef uint16_t UWORD;typedef uint64_t UQUAD;typedef int64_t QUAD;
typedef uintptr_t IPTR;typedef void *APTR;typedef struct Object Object;typedef struct Class Class;
#define TRUE 1
#define FALSE 0
#define MDP_SCALE_UNAVAILABLE 2
#define AMIGAKEY 1
#define MENUCHECK 2
#define SUBMENUIMAGE 3
#define HIGHITEM 8192
#define OCLASS(o) ((o)->cl)
#define INST_DATA(cl,o) ((o)->data)
struct Class { int id; };
struct GfxBase { int unused; };struct IntuitionBase { struct GfxBase *GfxBase; };
#define GetPrivIBase(i) (i)
#define DEFSIZE_WIDTH 14
#define JAM1 1
#define BARBLOCKPEN 0
#define FILLPEN 1
#define BACKGROUNDPEN 2
#define BARDETAILPEN 3
#define IDS_SELECTED 1
#define MENUS_AMIGALOOK(i) 0
struct Object { Class *cl; void *data; };
struct DecorImage { ULONG *data;UWORD w,h; BOOL ok;ULONG subimagescols,subimagesrows; };
struct TileInfo { UWORD TileLeft,TileRight,TileTop,TileBottom; };
struct BitMap { int unused; };
struct TextFont { UWORD tf_YSize,tf_XSize; };
struct DrawInfo { struct TextFont *dri_Font;UWORD *dri_Pens; };
struct RastPort { struct TextFont *Font;LONG TxBaseline,x,y; };
struct Screen { LONG Width,Height; };
struct ScreenRenderContext { UWORD src_Scale,src_LegacyScaleFactor;struct DrawInfo *src_LogicalDrawInfo,*src_PhysicalDrawInfo; };
struct DecorConfig { LONG MenuInnerLeft,MenuInnerTop,MenuInnerRight,MenuInnerBottom;
 LONG MenuTileLeft,MenuTileTop,MenuTileRight,MenuTileBottom;BOOL MenuIsTiled;ULONG MenuHighlightTint; };
struct DecorImages { struct DecorImage *img_menu; };
struct ScreenData { struct DecorImages *di; };
struct menudecor_data { struct DecorConfig *dc;struct DecorImage *img_amigakey,*img_menucheck,*img_submenu; };
struct MenuData { struct DecorImage *di,*highlight,*scaled_menu; const struct ScreenRenderContext *context;BOOL logical;
 struct BitMap *map;struct TileInfo *img_menu_ti; };
struct mdpGetMenuSpaces { LONG mdp_InnerLeft,mdp_InnerTop,mdp_InnerRight,mdp_InnerBottom;
 LONG mdp_ItemInnerLeft,mdp_ItemInnerTop,mdp_ItemInnerRight,mdp_ItemInnerBottom,mdp_MinWidth,mdp_MinHeight; };
struct mdpGetScaleSpaces { struct mdpGetMenuSpaces base; const struct ScreenRenderContext *context;BOOL logical; };
struct mdpGetDefSizeSysImage { ULONG mdp_Which;ULONG *mdp_Width,*mdp_Height;struct TextFont *mdp_ReferenceFont; };
struct mdpGetScaleDefSizeSysImage { struct mdpGetDefSizeSysImage base;
 const struct ScreenRenderContext *context;BOOL logical;ULONG requestedWidth,requestedHeight;APTR *prepared; };
struct mdpDrawSysImage { struct RastPort *mdp_RPort;LONG mdp_X,mdp_Y,mdp_Width,mdp_Height;struct DrawInfo *mdp_Dri;ULONG mdp_Which,mdp_State; };
struct mdpDrawScaleSysImage { struct mdpDrawSysImage base;const struct ScreenRenderContext *context;BOOL logical;APTR prepared; };
struct mdpReleaseScaleSysImage { ULONG MethodID;APTR prepared; };
struct mdpInitMenu { struct RastPort *mdp_RPort;struct Screen *mdp_Screen;ULONG mdp_Left,mdp_Top,mdp_Width;LONG mdp_Height;
 IPTR mdp_UserBuffer,mdp_ScreenUserBuffer; };
struct mdpInitScaleMenu { struct mdpInitMenu base;const struct ScreenRenderContext *context;BOOL logical; };
struct mdpExitMenu { IPTR mdp_UserBuffer; };
static int calls,fail_at,live,draws,vector_active;static LONG vl,vt,vr,vb;
static void point(LONG x,LONG y){if(vector_active)assert(x>=vl&&x<=vr&&y>=vt&&y<=vb);}
static void InitRastPort(struct RastPort *r){memset(r,0,sizeof *r);}
static void DeinitRastPort(struct RastPort *r){(void)r;}
static void SetFont(struct RastPort *r,struct TextFont*f){r->Font=f;r->TxBaseline=f->tf_YSize-2;}
static LONG TextLength(struct RastPort*r,const char*s,ULONG n){assert(s);return n*r->Font->tf_XSize;}
static void SetDrMd(struct RastPort*r,ULONG m){(void)r;(void)m;}
static void SetAPen(struct RastPort*r,ULONG p){(void)r;(void)p;}
static void RectFill(struct RastPort*r,LONG x,LONG y,LONG right,LONG bottom){(void)r;point(x,y);point(right,bottom);assert(right>=x&&bottom>=y);}
static void Move(struct RastPort*r,LONG x,LONG y){point(x,y);r->x=x;r->y=y;}
static void Draw(struct RastPort*r,LONG x,LONG y){point(x,y);r->x=x;r->y=y;}

static void *allocate(size_t n){if(++calls==fail_at)return NULL;void*p=calloc(1,n);if(p)live++;return p;}
static void release(void*p){if(p){live--;free(p);}}
static struct DecorImage *DNewDecorImageContainer(UWORD w,UWORD h){struct DecorImage *d=allocate(sizeof *d);if(!d)return NULL;
 d->w=w;d->h=h;d->subimagescols=d->subimagesrows=1;d->data=allocate((size_t)w*h*sizeof(ULONG));if(!d->data){release(d);return NULL;}return d;}
static void DDisposeImageContainer(struct DecorImage*d){assert(d);release(d->data);release(d);}
static struct DecorImage *DGetImageFromRP(struct RastPort *rp,UWORD x,UWORD y,UWORD w,UWORD h){(void)rp;
 struct DecorImage *d=DNewDecorImageContainer(w,h);if(d)for(ULONG j=0;j<h;j++)for(ULONG i=0;i<w;i++)d->data[j*w+i]=0xff000000|((x+i)%256)<<16|((y+j)%256)<<8;return d;}
static void DDrawPartImageToRP(struct RastPort*rp,struct DecorImage*d,LONG x,LONG y,LONG sx,LONG sy,LONG w,LONG h){(void)rp;(void)x;(void)y;
 assert(d&&d->ok&&sx>=0&&sy>=0&&w>0&&h>0&&sx+w<=d->w&&sy+h<=d->h);draws++;}
static void DSetImageTint(struct DecorImage*d,UWORD ratio,ULONG color){assert(d&&d->ok);for(ULONG i=0;i<(ULONG)d->w*d->h;i++)d->data[i]^=color*!!ratio;}
static void CopyMem(const void*a,void*b,ULONG n){memcpy(b,a,n);}
static void FreeBitMap(struct BitMap*b){release(b);}
static void FreeVec(APTR p){release(p);}
'''
TEST=r'''
int main(void){
 Class c={1},unknown={2};struct DecorConfig config={.MenuInnerLeft=2,.MenuInnerTop=1,.MenuInnerRight=2,.MenuInnerBottom=1,
 .MenuTileLeft=1,.MenuTileTop=1,.MenuTileRight=1,.MenuTileBottom=1,.MenuIsTiled=TRUE,.MenuHighlightTint=0xff303030};
 struct DecorImage *texture=DNewDecorImageContainer(8,8);assert(texture);texture->ok=TRUE;
 for(ULONG i=0;i<64;i++)texture->data[i]=0xffffffff;
 for(LONG radius=1;radius<=4;radius++)for(LONG y=0;y<8;y++)for(LONG x=0;x<8;x++)
  assert(menu_blur_channel(texture,x,y,16,radius)==255);
 for(ULONG i=0;i<64;i++)texture->data[i]=0xff000000|i;
 struct menudecor_data data={&config,texture,texture,texture};Object obj={&c,&data};
 struct DecorImages images={texture};struct ScreenData sd={&images};struct Screen screen={2560,1600};struct RastPort rp={0};
 struct ScreenRenderContext context={.src_Scale=100,.src_LegacyScaleFactor=1};ULONG w=0,h=0;APTR prepared=NULL;
 struct mdpGetScaleDefSizeSysImage size={.base={.mdp_Which=MENUCHECK,.mdp_Width=&w,.mdp_Height=&h},.context=&context,.prepared=&prepared};
 int scales[]={100,125,150,200,300,400,100};
 for(unsigned k=0;k<sizeof(scales)/sizeof(scales[0]);k++){
  context.src_Scale=scales[k];context.src_LegacyScaleFactor=(scales[k]+50)/100;
  assert(menudecor_prepare_image(&c,&obj,&size)==1);assert(prepared);
  assert(w==(ULONG)(8*scales[k]+50)/100&&h==w);
  struct DecorImage *p=prepared;
  // Independent nearest-sample position/colour oracle, exact replication at integers.
  for(ULONG y=0;y<h;y++)for(ULONG x=0;x<w;x++)assert(p->data[y*w+x]==(0xff000000UL|((y*8/h)*8+x*8/w)));
  struct mdpDrawScaleSysImage draw={.base={.mdp_RPort=&rp,.mdp_X=1,.mdp_Y=2,.mdp_Width=w,.mdp_Height=h},.context=&context,.prepared=prepared};
  int before=calls;assert(menudecor_draw_prepared(&c,&obj,&draw));assert(calls==before);
  struct mdpReleaseScaleSysImage dispose={.prepared=prepared};assert(menudecor_release_prepared(&c,&obj,&dispose));prepared=NULL;
  struct mdpGetScaleSpaces spaces={.context=&context};assert(menudecor_scale_spaces(&c,&obj,&spaces));assert(spaces.base.mdp_InnerLeft==(2*scales[k]+50)/100);
  struct MenuData md={0};struct mdpInitScaleMenu init={.base={&rp,&screen,10,20,100,70,(IPTR)&md,(IPTR)&sd},.context=&context};
  assert(menudecor_init_scale(&c,&obj,&init));assert(md.di&&md.highlight&&md.scaled_menu&&md.context==&context);
  assert(md.di->w==100&&md.di->h==70&&md.scaled_menu->w==w);
  for(ULONG i=0;i<7000;i++)assert((md.di->data[i]>>24)==255);
  struct mdpExitMenu exit={.mdp_UserBuffer=(IPTR)&md};menudecor_exitmenu(&c,&obj,&exit);assert(live==2);
 }
 // Explicit extents are prepared eagerly, never scaled by a visible draw.
 size.requestedWidth=19;size.requestedHeight=23;assert(menudecor_prepare_image(&c,&obj,&size)==1&&w==19&&h==23);
 struct mdpReleaseScaleSysImage dispose={.prepared=prepared};menudecor_release_prepared(&c,&obj,&dispose);prepared=NULL;
 size.requestedWidth=0;size.requestedHeight=19;
 assert(menudecor_prepare_image(&c,&obj,&size)==1&&w==19&&h==19);
 dispose.prepared=prepared;menudecor_release_prepared(&c,&obj,&dispose);prepared=NULL;
 size.requestedWidth=size.requestedHeight=0;
 data.img_menucheck=NULL;w=h=42;prepared=(void*)1;assert(menudecor_prepare_image(&c,&obj,&size)==2&&w==0&&h==0&&!prepared);
 data.img_menucheck=texture;obj.cl=&unknown;assert(!menudecor_prepare_image(&c,&obj,&size));obj.cl=&c;
 // Image prepare OOM must be FAILED, never UNAVAILABLE fallback.
 for(int n=1;n<=2;n++){calls=0;fail_at=n;assert(!menudecor_prepare_image(&c,&obj,&size));assert(!prepared&&live==2);}fail_at=0;
 // Sweep all partial popup allocation states using the actual Exit method.
 struct MenuData md={0};struct mdpInitScaleMenu init={.base={&rp,&screen,10,20,100,70,(IPTR)&md,(IPTR)&sd},.context=&context};
 calls=0;assert(menudecor_init_scale(&c,&obj,&init));int allocations=calls;
 struct mdpExitMenu exit={.mdp_UserBuffer=(IPTR)&md};menudecor_exitmenu(&c,&obj,&exit);
 for(int n=1;n<=allocations;n++){memset(&md,0,sizeof md);calls=0;fail_at=n;assert(!menudecor_init_scale(&c,&obj,&init));menudecor_exitmenu(&c,&obj,&exit);assert(live==2);}
 fail_at=0;memset(&md,0,sizeof md);init.base.mdp_Left=2500;assert(!menudecor_init_scale(&c,&obj,&init));menudecor_exitmenu(&c,&obj,&exit);assert(live==2);
 init.base.mdp_Left=10;config.MenuTileLeft=-1;memset(&md,0,sizeof md);assert(!menudecor_init_scale(&c,&obj,&init));menudecor_exitmenu(&c,&obj,&exit);assert(live==2);
 config.MenuTileLeft=1;context.src_Scale=200;context.src_LegacyScaleFactor=2;init.logical=TRUE;memset(&md,0,sizeof md);
 assert(menudecor_init_scale(&c,&obj,&init));assert(md.logical&&md.scaled_menu->w==8);menudecor_exitmenu(&c,&obj,&exit);assert(live==2);
 // Actual shared vector route: system logical font14 versus screen base13.
 struct TextFont base={13,7},system={14,8},physical={26,14};UWORD pens[8]={0};
 struct DrawInfo logical={&base,pens},phys={&physical,pens};
 context.src_LogicalDrawInfo=&logical;context.src_PhysicalDrawInfo=&phys;
 struct IntuitionBase ib={0};
 for(unsigned k=0;k<sizeof(scales)/sizeof(scales[0]);k++){
  context.src_Scale=scales[k];physical.tf_YSize=(13*scales[k]+50)/100;physical.tf_XSize=(7*scales[k]+50)/100;
  for(ULONG which=AMIGAKEY;which<=SUBMENUIMAGE;which++){
   struct mdpGetScaleDefSizeSysImage sizev={.base={.mdp_Which=which,.mdp_Width=&w,.mdp_Height=&h,.mdp_ReferenceFont=&physical},.context=&context,.prepared=&prepared};
   assert(MenuScaleVectorSize(&sizev,&ib));assert(!prepared&&w&&h);
   if(which==MENUCHECK)assert(w==(ULONG)(10*scales[k]+50)/100&&h==physical.tf_YSize);
   vl=7;vt=9;vr=vl+w-1;vb=vt+h-1;vector_active=1;
   struct mdpDrawScaleSysImage vector={.base={.mdp_RPort=&rp,.mdp_X=vl,.mdp_Y=vt,.mdp_Width=w,.mdp_Height=h,.mdp_Dri=&phys,.mdp_Which=which},.context=&context};
   int before=calls;assert(MenuScaleVectorDraw(&vector,&ib));assert(calls==before);
   vector.base.mdp_Width=1;vector.base.mdp_Height=1;vr=vl;vb=vt;assert(MenuScaleVectorDraw(&vector,&ib));vector_active=0;
  }
 }
 struct mdpGetScaleDefSizeSysImage logicalsize={.base={.mdp_Which=MENUCHECK,.mdp_Width=&w,.mdp_Height=&h,.mdp_ReferenceFont=&system},.context=&context,.logical=TRUE,.prepared=&prepared};
 assert(MenuScaleVectorSize(&logicalsize,&ib)&&w==11&&h==14);
 logicalsize.requestedHeight=19;assert(MenuScaleVectorSize(&logicalsize,&ib)&&w==15&&h==19);
 logicalsize.requestedWidth=3;assert(MenuScaleVectorSize(&logicalsize,&ib)&&w==3&&h==19);
 // Shipped Ice theme (images/Themes/Ice/menu/config, 71x71 Background): the
 // 15+15 tile frame exceeds the 18-row 100 % menu bar (Decoration 18/36 rows).
 // The bar is the MDM_INITSCALEMENU popup at 0,0, screen width x BarHeight+1.
 {
  struct DecorConfig ice={.MenuInnerLeft=6,.MenuInnerTop=6,.MenuInnerRight=12,.MenuInnerBottom=11,
   .MenuTileLeft=6,.MenuTileTop=15,.MenuTileRight=13,.MenuTileBottom=15,.MenuIsTiled=TRUE,.MenuHighlightTint=0x9900aadd};
  struct DecorImage *background=DNewDecorImageContainer(71,71);assert(background);background->ok=TRUE;
  for(ULONG i=0;i<71*71;i++)background->data[i]=0xff000000|i;
  struct menudecor_data icedata={&ice,texture,texture,texture};Object iceobj={&c,&icedata};
  struct DecorImages iceimages={background};struct ScreenData icesd={&iceimages};
  struct ScreenRenderContext icecontext={.src_Scale=100,.src_LegacyScaleFactor=1};
  int bars[][2]={{100,18},{100,1},{100,29},{100,30},{200,36},{125,22},{150,27}};
  for(unsigned k=0;k<sizeof(bars)/sizeof(bars[0]);k++){
   icecontext.src_Scale=bars[k][0];
   for(int logical=0;logical<=1;logical++){
    icecontext.src_LegacyScaleFactor=logical?(bars[k][0]+50)/100:1;
    if(logical&&bars[k][0]%100)continue;
    ULONG bar=logical?bars[k][1]/icecontext.src_LegacyScaleFactor:bars[k][1];
    ULONG width=logical?screen.Width/icecontext.src_LegacyScaleFactor:screen.Width;
    struct MenuData imd={0};
    struct mdpInitScaleMenu iceinit={.base={&rp,&screen,0,0,width,bar,(IPTR)&imd,(IPTR)&icesd},.context=&icecontext,.logical=logical};
    assert(menudecor_init_scale(&c,&iceobj,&iceinit));
    assert(imd.di&&imd.highlight&&imd.di->w==width&&imd.di->h==bar);
    struct mdpExitMenu iceexit={.mdp_UserBuffer=(IPTR)&imd};menudecor_exitmenu(&c,&iceobj,&iceexit);
   }
  }
  DDisposeImageContainer(background);
 }
 DDisposeImageContainer(texture);assert(!live);
 printf("PASS actual MenuDecor image/popup prep: 7 scales, explicit extents, unavailable/unknown/OOM; %d popup allocation faults; bounded tiles; no draw allocation; Ice tile frame over 1..36-row bars\n",allocations);
 return 0;
}
'''

def source():
 text=(ROOT/'workbench/c/Decoration/menudecorclass.c').read_text()
 names=('menu_checked_image','menu_typed_context','menu_typed_metric','menudecor_getmenuspaces','menudecor_scale_spaces',
 'menudecor_prepare_image','menudecor_draw_prepared','menudecor_release_prepared','menu_sample_axis','menu_blur_channel','menudecor_init_scale','menudecor_exitmenu')
 vector=(ROOT/'rom/intuition/menudecorclass.c').read_text()
 return PREAMBLE+''.join(function(text,n) for n in names)+''.join(function(vector,n) for n in ('menu_scale_metric','MenuScaleVectorSize','MenuScaleVectorDraw'))+TEST

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path);args=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='menudecor-') as temporary:
  directory=args.output or Path(temporary);directory.mkdir(parents=True,exist_ok=True);c=directory/'menudecor.c';exe=directory/'menudecor'
  original=source();c.write_text(original)
  command=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',str(c),'-o',str(exe)]
  subprocess.run(command,check=True);subprocess.run([str(exe)],check=True)
  mutations={
   'blur-channel-overflow':('{-1,1,1},{0,1,1},{1,1,1},{0,2,1}', '{-1,1,1},{0,1,2},{1,1,1},{0,2,1}'),
   'unknown-inherited-capability':('OCLASS(obj) == cl && context &&','context &&'),
   'OOM-is-unavailable':('if (!image) return FALSE;','if (!image) return MDP_SCALE_UNAVAILABLE;'),
   'explicit-width-ignored':('if (msg->requestedWidth) width = msg->requestedWidth;','if (msg->requestedWidth) width = 1;'),
   'integer-pixels-not-replicated':('(UQUAD)x * source->w / width','x % source->w'),
   'draw-consumes-image':('msg->base.mdp_Y, 0, 0, image->w, image->h);','msg->base.mdp_Y, 0, 0, image->w, image->h); DDisposeImageContainer(image);'),
   'highlight-leak':('if (md->highlight) DDisposeImageContainer(md->highlight);','(void)md->highlight;'),
   'logical-double-scale':('scale = msg->logical ? 100 : msg->context->src_Scale;','scale = msg->context->src_Scale;'),
   'negative-tile-wrap':('left >= tw || right >= tw || top >= th || bottom >= th ||','FALSE ||'),
   'bar-under-tile-frame-refused':('left + right >= tw || top + bottom >= th) return FALSE;','left + right >= tw || top + bottom >= th ||\n                left + right > md->di->w || top + bottom > md->di->h) return FALSE;'),
   'bar-tile-unmapped':('(UQUAD)y * tileh / md->di->h, tileh,','y + tileh, tileh,'),
  }
  results=[]
  for label,(old,new) in mutations.items():
   assert old in original,label
   c.write_text(original.replace(old,new))
   subprocess.run(command,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
   result=subprocess.run([str(exe)],capture_output=True,text=True)
   results.append(dict(label=label,returncode=result.returncode,stderr=result.stderr))
   assert result.returncode != 0,'mutation survived: '+label
  (directory/'mutants.json').write_text(json.dumps(results,indent=2))
  c.write_text(original);subprocess.run(command,check=True)
  print('PASS rejected '+str(len(mutations))+' actual MenuDecor mutants')
if __name__=='__main__':main()
