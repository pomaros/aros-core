#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Real graphics-region allocator faults in actual default shape preparation.
The companion preparedregions fixture supplies extraction/scaffolding only;
its builder tests are not executed. Image alpha/title layout and preparation
are production bodies; TextFit/Exec/BOOPSI environment are host substitutes.
"""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[4]
ns={'__file__':str(Path(__file__).with_name('preparedregions_host.py'))}
exec(Path(ns['__file__']).read_text().split('intregions=')[0],ns)
def read(rel):return (ROOT/rel).read_text(encoding='latin1')
def strip(s):return '\n'.join(x for x in s.splitlines() if not x.lstrip().startswith('#include'))+'\n'
foundation=ns['prefix'].replace('int regionsem; void *regionpool;', 'int regionsem; void *regionpool;struct TextFont*DefaultFont;')
foundation+=ns['chunks']+ns['protos']+strip(read('rom/graphics/intregions.h'))+strip(read('rom/graphics/intregions.c'))+ns['bodies']
public=read('compiler/include/intuition/windecorclass.h');public=public[public.index('struct wdpPrepareScaleShape'):public.index('struct wdpWindowShape')]
builtin=read('rom/intuition/windecorclass.c');builtin=builtin[builtin.index('IPTR WinDecorClass__WDM_PREPARESCALESHAPE('):]
# Extract the actual method, independently of following production methods.
left=builtin.index('{');depth=1;right=left+1
while depth:
 depth+=(builtin[right]=='{')-(builtin[right]=='}');right+=1
builtin=builtin[:right]
external=read('workbench/c/Decoration/windowdecorclass.c')
draw=external[external.index('static int WriteTiledImageShape('):external.index('static VOID DrawPartialTitleBar(')]
body=external[external.index('static BOOL prepare_shape_parts('):external.index('static IPTR windecor_windowshape(')]
# Execute the actual ordinary aware resize branch as well as typed transaction
# preparation, with proposed dimensions deliberately different from Window.
ordinary=external[external.index('static IPTR windecor_windowshape('):]
start=ordinary.index('    if (wd->scaleData)');left=ordinary.index('{',start);depth=1;end=left+1
while depth:
 depth+=(ordinary[end]=='{')-(ordinary[end]=='}');end+=1
ordinary='static IPTR ordinary_prepared_shape(Class*cl,Object*obj,struct wdpWindowShape*msg){struct WindowData*wd=(struct WindowData*)msg->wdp_UserBuffer;struct Window*window=msg->wdp_Window;'+ordinary[start:end]+'assert(!"legacy branch outside fixture");return 0;}\n'

setfont=read('rom/graphics/setfont.c').split('AROS_LIBFUNC_INIT',1)[1].split('AROS_LIBFUNC_EXIT',1)[0]
alpha=read('workbench/libs/decorator/pixops.c');alpha=alpha[alpha.index('void pixop_extract_alpha_row('):alpha.index('void pixop_tint_row(')]
writealpha=read('workbench/libs/decorator/drawfuncs.c');writealpha=writealpha[writealpha.index('void WriteAlphaPixelArray('):writealpha.index('void  SetImageTint(')]
prefix=r'''
#define STACKED
#define MEMF_CLEAR 1
#define WFLG_WINDOWACTIVE 1
#define WFLG_TOOLBOX 2
#define GFLG_RELRIGHT 1
#define GACT_TOPBORDER 1
#define GTYP_SYSTYPEMASK 255
#define GTYP_CLOSE 1
#define GTYP_WDEPTH 2
#define GTYP_WZOOM 4
#define GTYP_WDRAGGING 8
#define WD_DWTA_LEFT 0
#define WD_DWTA_CENTER 1
#define WD_DWTA_RIGHT 2
#define GET_ARGB_A(p) ((p)>>24)
typedef void*APTR;
typedef struct Class{int id;}Class;
typedef struct Object{Class*class;void*data;}Object;
#define OCLASS(o) ((o)->class)
#define INST_DATA(c,o) ((void)(c),(o)->data)
struct TextFont{WORD tf_XSize,tf_YSize,tf_Baseline;};
struct RastPort{struct TextFont*Font;WORD TxWidth,TxHeight,TxBaseline;};
struct Screen{struct RastPort RastPort;int unchanged;};
struct Gadget{struct Gadget*NextGadget;UWORD Flags,GadgetType,Activation;WORD Width,LeftEdge;};
struct Window{WORD Width,Height;int8_t BorderTop;ULONG Flags;char*Title;struct Gadget*FirstGadget;struct Screen*WScreen;};
struct TextExtent{struct Rectangle te_Extent;};
struct DecorImage{ULONG*data;UWORD w,h;BOOL ok;};
struct DecorImageLUT8{UWORD w,h;UBYTE*data;};
struct CachedTitleBarShape{struct Region*shape;ULONG width,height,windowflags;};
struct WindowScaleData;
struct WindowData{struct DecorImage*img_winbar_normal;struct CachedTitleBarShape tbarshape;ULONG unchanged;struct WindowScaleData*scaleData;};
struct DecorConfig{BOOL BarMasking,BarRounded,CloseGadgetOnRight,FillTitleBar;LONG BarHeight;
#define PART(n) LONG n##_o,n##_s
 PART(BarPreGadget);PART(BarLGadgetFill);PART(BarPre);PART(BarJoinGB);PART(BarLFill);PART(BarJoinBT);PART(BarTitleFill);PART(BarJoinTB);PART(BarRFill);PART(BarJoinBG);PART(BarRGadgetFill);PART(BarPostGadget);PART(BarPost);
#undef PART
};
struct windecor_data{struct DecorConfig*dc;UWORD TextAlign;};
struct DrawInfo{struct TextFont*dri_Font;};
struct ScreenRenderContext{struct DrawInfo*src_PhysicalDrawInfo;};
struct WindowScaleData{struct windecor_data renderer;ULONG scale;const struct ScreenRenderContext*context;};
#define WDM_PREPARESCALESHAPE 12
struct wdpWindowShape{ULONG MethodID;BOOL wdp_TrueColor;struct Window*wdp_Window;LONG wdp_Width,wdp_Height;IPTR wdp_UserBuffer;};
static void*AllocVec(IPTR n,ULONG flags){(void)flags;return alloc(n);}
static void FreeVec(void*p){if(p)freemem(p);}
static void CopyMem(const void*s,void*d,IPTR n){memcpy(d,s,n);}
static struct TextFont*expectedfont;static ULONG textfits;
static ULONG TextFit(struct RastPort*rp,const char*t,ULONG n,struct TextExtent*e,void*x,LONG dir,LONG w,LONG h){
 (void)t;(void)x;(void)dir;(void)h;assert(rp->Font==expectedfont);textfits++;
 ULONG fit=w>0?(ULONG)w/rp->Font->tf_XSize:0;if(fit>n)fit=n;e->te_Extent=(struct Rectangle){0,0,(WORD)(fit*rp->Font->tf_XSize-1),0};return fit;}
static struct DecorImage*DScaleDecorImage(struct DecorImage*i,LONG w,LONG h){(void)i;(void)w;(void)h;assert(!"unchecked lazy scale called");return NULL;}
static void DDisposeImageContainer(struct DecorImage*i){(void)i;assert(!"borrowed image disposed");}
#define DWriteAlphaPixelArray WriteAlphaPixelArray
'''
main=r'''
static int point(struct Region*r,int x,int y){for(struct RegionRectangle*q=r->RegionRectangle;q;q=q->Next)
 if(x>=r->bounds.MinX+q->bounds.MinX&&x<=r->bounds.MinX+q->bounds.MaxX&&y>=r->bounds.MinY+q->bounds.MinY&&y<=r->bounds.MinY+q->bounds.MaxY)return 1;return 0;}
static int ysample(int y,int src,int dst){int a=(y+1)*src-src/2;return a<=0?0:(a+dst-1)/dst-1;}
int main(void){
 Class builtinclass={1},externalclass={2},unknown={3};
 unsigned faultcases=0;
 for(int mode=0;mode<5;mode++)for(unsigned occupancy=0;occupancy<SIZECHUNKBUF;occupancy++){
  unsigned attempts=0;
  for(unsigned sweep=0;sweep<2;sweep++)for(unsigned fail=sweep?1:0;fail<=(sweep?attempts:0);fail++){
   assert(!live);NEWLIST(&gfx.ChunkPoolList);gfx.regionpool=(void*)1;ordinal=failat=denied=0;
   struct Region*cache=NewRegion();assert(cache);struct Rectangle oldrect={1,2,4,5};assert(OrRectRegion(cache,&oldrect));
   struct RegionRectangle*oldrr=cache->RegionRectangle;struct Region oldcache=*cache;
   struct RegionRectangleExtChunk*ballast[SIZECHUNKBUF];for(unsigned i=0;i<occupancy;i++){ballast[i]=__NewRegionRectangleExtChunk(GfxBase);assert(ballast[i]);}
   struct TextFont oldfont={2,7,5},font={3,13,10};expectedfont=&font;
   struct Screen screen={{&oldfont,2,7,5},73};
   struct Gadget right={NULL,GFLG_RELRIGHT,GTYP_WDEPTH,GACT_TOPBORDER,5,-6};
   struct Window window={37,23,4,mode==2?0:WFLG_WINDOWACTIVE,"title",mode==4?&right:NULL,&screen};
   ULONG pixels[8*8];for(int y=0;y<8;y++)for(int x=0;x<8;x++)pixels[y*8+x]=((x+y)%3?0xff000000:0);
   struct DecorImage image={pixels,8,8,TRUE};struct WindowData wd={&image,{cache,37,23,17},99,NULL};
   struct DecorConfig config={0};config.BarMasking=mode==1||mode==2||mode==4;config.BarRounded=mode==0;config.BarHeight=4;config.BarRFill_s=4;
   struct windecor_data data={&config,WD_DWTA_LEFT};Object object={&externalclass,&data};
   struct Region*out=(void*)0x1234;
   struct wdpPrepareScaleShape msg={12,&window,53,29,5,&font,(IPTR)&wd,&out};
   struct Window oldwindow=window;struct Screen oldscreen=screen;struct WindowData oldwd=wd;struct DecorConfig oldconfig=config;struct windecor_data olddata=data;
   unsigned baseline=live;ordinal=0;failat=fail;textfits=0;
   BOOL ok=windecor_preparescaleshape(&externalclass,&object,&msg);
   if(!sweep){
    assert(ok);attempts=ordinal;
    if(mode==3)assert(!out);else{
     assert(out);if(mode==1||mode==2||mode==4)assert(textfits);
     for(int y=0;y<29;y++)for(int x=0;x<53;x++){
      int want;
      if(mode==0){int insets[]={9,6,4,3,2,2,1,1,1,0};int inset=insets[y<10?y:9];want=x>=inset&&x<53-inset;}
      else{int sy=ysample(y+(mode==2?5:0),8,10);want=y>=5||((mode!=4||x<47)&&((x%4+sy)%3!=0));}
      assert(point(out,x,y)==want);
     }
    }
   }else{assert(!ok&&!out&&denied==1);faultcases++;}
   assert(!memcmp(&oldwindow,&window,sizeof window)&&!memcmp(&oldscreen,&screen,sizeof screen));
   assert(!memcmp(&oldwd,&wd,sizeof wd)&&!memcmp(&oldconfig,&config,sizeof config)&&!memcmp(&olddata,&data,sizeof data));
   assert(!memcmp(&oldcache,cache,sizeof oldcache)&&cache->RegionRectangle==oldrr&&oldrr->bounds.MinX==0);
   failat=0;if(out)DisposeRegion(out);assert(live==baseline);
   if(!sweep&&(mode==1||mode==2||mode==4)){
    struct DrawInfo currentDI={&font};struct ScreenRenderContext currentContext={&currentDI};
    struct WindowScaleData current={{&config,WD_DWTA_LEFT},100,&currentContext};
    wd.scaleData=&current;window.BorderTop=5;
    struct wdpWindowShape resize={0,FALSE,&window,53,29,(IPTR)&wd};
    struct Region*ordinaryResult=(struct Region*)ordinary_prepared_shape(&externalclass,&object,&resize);assert(ordinaryResult);
    for(int y=0;y<29;y++)for(int x=0;x<53;x++){int sy=ysample(y+(mode==2?5:0),8,10);int wanted=y>=5||((mode!=4||x<47)&&((x%4+sy)%3!=0));assert(point(ordinaryResult,x,y)==wanted);}
    DisposeRegion(ordinaryResult);wd.scaleData=NULL;window.BorderTop=4;assert(live==baseline);
   }
   /* Unknown classes cannot inherit acceptance; rectangle is still success. */
   object.class=&unknown;out=(void*)0x1234;
   assert(!windecor_preparescaleshape(&externalclass,&object,&msg)&&!out);
   out=(void*)0x1234;assert(!WinDecorClass__WDM_PREPARESCALESHAPE(&builtinclass,&object,&msg)&&!out);
   object.class=&builtinclass;assert(WinDecorClass__WDM_PREPARESCALESHAPE(&builtinclass,&object,&msg)&&!out);
   msg.wdp_Width=0;assert(!WinDecorClass__WDM_PREPARESCALESHAPE(&builtinclass,&object,&msg)&&!out);
   for(unsigned i=0;i<occupancy;i++)__DisposeRegionRectangleExtChunk(ballast[i],GfxBase);
   DisposeRegion(cache);assert(!live&&!locks&&!GetHead(&gfx.ChunkPoolList));
  }
 }
 /* Small rounded dimensions preserve the union-of-stair-step geometry. */
 NEWLIST(&gfx.ChunkPoolList);gfx.regionpool=(void*)1;failat=0;
 struct TextFont font={3,13,10};struct Window w={0};struct DecorConfig c={0};c.BarRounded=TRUE;
 struct windecor_data d={&c,0};Object o={&externalclass,&d};struct Region*r=NULL;
 struct wdpPrepareScaleShape m={12,&w,0,0,0,&font,0,&r};
 for(int width=1;width<=25;width++)for(int height=1;height<=12;height++){
  m.wdp_Width=width;m.wdp_Height=height;assert(windecor_preparescaleshape(&externalclass,&o,&m)&&r);
  for(int y=0;y<height;y++)for(int x=0;x<width;x++){
   int insets[]={9,6,4,3,2,2,1,1,1,0};int inset=insets[y<10?y:9];assert(point(r,x,y)==(x>=inset&&x<width-inset));}
  DisposeRegion(r);r=NULL;
 }
 /* The prepared generation supplies its own scaled config/rounding, not
    * the live provider config. Verify membership independently at all13scales. */
 struct DecorConfig preparedConfig={0};preparedConfig.BarRounded=TRUE;
 struct DrawInfo preparedDI={&font};struct ScreenRenderContext preparedContext={&preparedDI};
 struct WindowScaleData scaled={{&preparedConfig,0},100,&preparedContext};
 struct WindowData preparedWD={0};preparedWD.scaleData=&scaled;
 c.BarRounded=FALSE;m.wdp_UserBuffer=(IPTR)&preparedWD;
 for(unsigned scale=100;scale<=400;scale+=25){scaled.scale=scale;m.wdp_Width=(53*scale+50)/100;m.wdp_Height=(29*scale+50)/100;
  assert(windecor_preparescaleshape(&externalclass,&o,&m)&&r);
  for(int y=0;y<m.wdp_Height;y++)for(int x=0;x<m.wdp_Width;x++){
   int wanted=0;int insets[]={9,6,4,3,2,1,0},rows[]={0,1,2,3,4,6,9};
   for(unsigned part=0;part<7;part++){int inset=(insets[part]*scale+50)/100,row=(rows[part]*scale+50)/100;if(y>=row&&x>=inset&&x<m.wdp_Width-inset)wanted=1;}
   assert(point(r,x,y)==wanted);
  }DisposeRegion(r);r=NULL;
  struct wdpWindowShape resize={0,FALSE,&w,m.wdp_Width,m.wdp_Height,(IPTR)&preparedWD};
  r=(struct Region*)ordinary_prepared_shape(&externalclass,&o,&resize);assert(r);
  for(int y=0;y<m.wdp_Height;y++)for(int x=0;x<m.wdp_Width;x++){
   int wanted=0;int insets[]={9,6,4,3,2,1,0},rows[]={0,1,2,3,4,6,9};
   for(unsigned part=0;part<7;part++){int inset=(insets[part]*scale+50)/100,row=(rows[part]*scale+50)/100;if(y>=row&&x>=inset&&x<m.wdp_Width-inset)wanted=1;}
   assert(point(r,x,y)==wanted);
  }DisposeRegion(r);r=NULL;
 }
 m.wdp_UserBuffer=0;
 c.BarMasking=TRUE;m.wdp_Width=10;m.wdp_Height=10;
 hardtrap=1;assert(windecor_preparescaleshape(&externalclass,&o,&m)&&!r);
 m.wdp_BorderTop=11;assert(!windecor_preparescaleshape(&externalclass,&o,&m)&&!r);
 m.wdp_BorderTop=5;m.wdp_Width=32768;assert(!windecor_preparescaleshape(&externalclass,&o,&m)&&!r);hardtrap=0;
 assert(!live&&!GetHead(&gfx.ChunkPoolList));
 printf("PASS actual windowshape: rounded/masked active+inactive/rectangle,20pooloccupancies,%u allocatorfailures,proposedgeometry+font,livecacheintact\n",faultcases);
}
'''
mutations={'ignore-region-failure':body.replace('if (!OrRectRegion(region, &rect)) goto fail;', 'OrRectRegion(region, &rect);'),
 'uses-live-width':body.replace('window.Width = msg->wdp_Width;', 'window.Width = msg->wdp_Window->Width;'),
 'mutates-live-cache':body.replace('    if (data->dc->BarMasking)', '    ((struct WindowData*)msg->wdp_UserBuffer)->tbarshape.width++;\n    if (data->dc->BarMasking)'),
 'inherits-capability':body.replace('OCLASS(obj) != cl || ',''),
 'loses-allocation-failure':body.replace('if (!mask.data) { FreeVec(image.data); return FALSE; }','if (!mask.data) { FreeVec(image.data); return TRUE; }')}
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp)
 for name,code in [('production',body),*mutations.items()]:
  c=d/f'{name}.c';exe=d/name;c.write_text(foundation+prefix+public+'void SetFont(struct RastPort *rp,struct TextFont *textFont){'+setfont+'}\n'+alpha+writealpha+draw+builtin+code+ordinary+main)
  result=subprocess.run(['clang','-std=gnu11','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(exe)],capture_output=True,text=True)
  if result.returncode:raise RuntimeError(result.stderr)
  result=subprocess.run([str(exe)],capture_output=True,text=True)
  if name=='production':
   if result.returncode:raise RuntimeError(result.stdout+result.stderr)
   print(result.stdout.strip())
  elif not result.returncode:raise RuntimeError(f'mutation escaped: {name}')
  else:print(f'REJECT {name}')
