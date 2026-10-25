#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Production decoration prepare/install/release under allocator/hook traps.
BOOPSI dispatch and image allocation are host substitutes; actual built-in and
shipped decorator preparation methods plus Intuition production bodies execute.
"""
from pathlib import Path
from scaleresult_fixture import result_definitions
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[4]
def read(rel):return (ROOT/rel).read_text(encoding='latin1')
public=read('compiler/include/intuition/scrdecorclass.h');public=public[public.index('struct ScreenScaleMetrics'):public.index('struct sdpGetDefSizeSysImage')]
header=read('rom/intuition/prepareddecor.h');header='\n'.join(l for l in header.splitlines() if not l.startswith('#include'))+'\n'
body=read('rom/intuition/decoration.c');helper=body[body.index('static LONG scale_bar_height('):body.index('BOOL int_InitDecorator(')]
body=helper+body[body.index('struct PreparedDepthGadget'):]
builtin=read('rom/intuition/scrdecorclass.c');builtin=builtin[builtin.index('IPTR ScrDecorClass__SDM_PREPARESCALE('):]
external=read('workbench/c/Decoration/screendecorclass.c');external=external[external.index('static IPTR scrdecor_querycapabilities('):external.index('static BOOL scrdecor_initscreen_metrics(')]
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uint8_t UBYTE;typedef int32_t LONG;typedef uint32_t ULONG;typedef int16_t WORD;typedef uint16_t UWORD;
typedef uintptr_t IPTR;typedef void*APTR;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define STACKED
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define SCREENQUIET 1
#define SCREENHIRES 2
#define GFLG_RELWIDTH 4
#define GFLG_RELRIGHT 8
#define SYSISIZE_MEDRES 1
#define SYSISIZE_LOWRES 0
#define SDEPTHIMAGE 10
#define SYSICLASS "sysiclass"
#define IA_Height 1
#define SYSIA_Which 2
#define SYSIA_DrawInfo 3
#define SYSIA_ReferenceFont 4
#define SYSIA_Size 5
#define TAG_DONE 0
#define GTYP_SYSTYPEMASK 255
#define GTYP_SDEPTH 1
#define SDF_LSG_MULTIPLE 8
#define D(x)
#define SDM_QUERYCAPABILITIES 98
#define SDM_PREPARESCALE 99
#define SDM_CAP_PREPARESCALE 1
struct TextFont{UWORD tf_YSize;};
typedef struct Class{int id;}Class;
typedef struct Object{WORD Width,Height;Class*class;void*data;}Object;
#define Image Object
#define OCLASS(obj) ((obj)->class)
#define INST_DATA(cl,obj) ((void)(cl),(obj)->data)
typedef void*Msg;
struct Gadget {WORD LeftEdge,TopEdge,Width,Height;UWORD Flags;APTR GadgetRender;struct Gadget*NextGadget;UWORD GadgetType;};
struct sdpLayoutScreenGadgets {struct Gadget*sdp_Gadgets;IPTR sdp_UserBuffer;ULONG sdp_Flags;};
struct Screen {int8_t BarHeight,BarVBorder,BarHBorder,MenuVBorder,MenuHBorder,WBorTop,WBorLeft,WBorRight,WBorBottom;UWORD Flags;};
struct ScreenRenderContext;
struct IntScreen {struct Screen screen;Object*depthgadget;IPTR DecorUserBuffer;Object*ScrDecorObj;int DInfo;struct ScreenRenderContext*context;};
#define GetPrivScreen(s) ((struct IntScreen*)(s))
struct IntuitionBase{int unused;};
struct DecorConfig {LONG BarHeight,SBarHeight,LeftBorder,RightBorder,BottomBorder,SBarGadPost_s;};
struct ThemeImage{int h;};
struct ScreenData {void*dts;struct ThemeImage*img_sdepth;int pen,unchanged;};
struct scrdecor_data{struct DecorConfig*dc;void*theme;Object*FirstChild;BOOL ChildLayoutPending;};
static unsigned allocs,failat,live,hardtrap,methods,disposes;
static void*allocate(size_t n){assert(!hardtrap);if(++allocs==failat)return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void release(void*p){assert(!hardtrap);if(p){assert(live);live--;free(p);}}
static void*AllocVec(IPTR n,ULONG f){(void)f;return allocate(n);}
static void FreeVec(void*p){release(p);}
static void DisposeObject(Object*p){assert(!hardtrap);if(p)disposes++;release(p);}
static IPTR DoMethodA(Object*o,Msg message);
'''
context_boundary=r'''
struct DrawInfo {struct Screen*dri_Screen;};
struct ScreenRenderContext{struct DrawInfo*src_PhysicalDrawInfo;ULONG src_PhysicalBarHeight;struct ScreenScaleMetrics logical;Object*depth;};
static struct DrawInfo currentdi,nextdi;
static struct ScreenRenderContext current,next;
static Object *oldimage,*nextimage;
static const struct ScreenRenderContext*CurrentScreenRenderContext(struct Screen*s){return GetPrivScreen(s)->context;}
static const struct ScreenScaleMetrics*ScreenRenderContextLogicalMetrics(const struct ScreenRenderContext*c){return c?&c->logical:NULL;}
static Object*ScreenRenderContextDepthImage(const struct ScreenRenderContext*c){return c?c->depth:NULL;}
'''
dispatch=r'''
static Class built={1},externalclass={2},unknown={3};
static IPTR DoMethodA(Object*o,Msg message){assert(!hardtrap);methods++;
 if(*(ULONG*)message==SDM_QUERYCAPABILITIES)return SDM_CAP_PREPARESCALE;
 if(o->class==&externalclass)return scrdecor_preparescale(&externalclass,o,message);
 return ScrDecorClass__SDM_PREPARESCALE(&built,o,message);
}
'''
main=r'''
static void setup(struct IntScreen*is,struct Gadget*g,Object*decor,struct ScreenData*sd){
 memset(is,0,sizeof*is);memset(g,0,sizeof*g);
 is->screen=(struct Screen){14,1,1,1,1,1,2,2,2,SCREENHIRES};
 is->depthgadget=(Object*)g;is->ScrDecorObj=decor;is->DecorUserBuffer=(IPTR)sd;
 g->LeftEdge=-13;g->TopEdge=2;g->Width=14;g->Height=15;g->Flags=GFLG_RELWIDTH;
 oldimage=allocate(sizeof(Object));nextimage=allocate(sizeof(Object));assert(oldimage&&nextimage);
 oldimage->Width=nextimage->Width=14;oldimage->Height=15;g->GadgetRender=oldimage;
 currentdi.dri_Screen=nextdi.dri_Screen=&is->screen;
 current=(struct ScreenRenderContext){&currentdi,15,{14,1,1,1,1,1,2,2,2,-1,0},oldimage};
 next=(struct ScreenRenderContext){&nextdi,0,{0},nextimage};is->context=&current;
}
int main(void){
 struct IntuitionBase base={0};struct TextFont font={26},basefont={13};
 struct DecorConfig config={20,18,3,4,5,2};struct ThemeImage themeimage={14};
 struct ScreenData sd={(void*)0x1234,&themeimage,37,91};
 struct scrdecor_data data={&config,(void*)0x4567,(Object*)0x9876,TRUE};
 for(int mode=0;mode<4;mode++)for(int outcome=0;outcome<3;outcome++){
  assert(!live);allocs=failat=methods=disposes=0;
  Object decor={0,0,mode==1?&externalclass:&built,&data};
  struct IntScreen is;struct Gadget gadget;setup(&is,&gadget,&decor,&sd);
  if(mode==2)is.screen.Flags|=SCREENQUIET;
  if(mode==3){is.depthgadget=NULL;gadget.GadgetRender=NULL;}
  struct IntScreen before=is;struct Gadget beforeg=gadget;struct ScreenData beforesd=sd;struct scrdecor_data beforedata=data;
  unsigned baseline=live;allocs=0;
  struct PreparedScreenScaleDecoration*p=PrepareScreenScaleDecoration(&is.screen,&basefont,&font,200,NULL,&base);assert(p);
  unsigned total=allocs;assert(total==1);
  assert(!memcmp(&before,&is,sizeof is)&&!memcmp(&beforeg,&gadget,sizeof gadget));
  assert(!memcmp(&beforesd,&sd,sizeof sd)&&!memcmp(&beforedata,&data,sizeof data));
  const struct ScreenScaleMetrics*m=PreparedScreenScaleDecorationMetrics(p);
  assert(m&&m->BarHeight==(mode==1?35:29));
  if(mode==1)assert(m->WBorTop==6&&m->WBorLeft==3&&m->DepthRightInset==4);
  next.src_PhysicalBarHeight=m->BarHeight+1;nextimage->Height=m->BarHeight+1;
  assert(BindScreenScaleDecoration(p,&next));
  if(mode<2){struct Screen foreign={0};nextdi.dri_Screen=&foreign;assert(!BindScreenScaleDecoration(p,&next));nextdi.dri_Screen=&is.screen;next.src_PhysicalBarHeight++;assert(!BindScreenScaleDecoration(p,&next));next.src_PhysicalBarHeight--;next.depth=NULL;assert(!BindScreenScaleDecoration(p,&next));next.depth=nextimage;assert(BindScreenScaleDecoration(p,&next));}
  if(outcome){
   hardtrap=1;InstallScreenScaleDecoration(p,FALSE);hardtrap=0;
   assert(is.screen.BarHeight==m->BarHeight);
   if(is.depthgadget){assert(gadget.Height==m->BarHeight+1);
    if(mode<2){assert(gadget.TopEdge==0);assert(gadget.GadgetRender!=beforeg.GadgetRender);assert(gadget.LeftEdge==(mode==1?-18:-13));}}
   struct PreparedScreenScaleDecoration*again=PrepareScreenScaleDecoration(&is.screen,&basefont,&font,200,NULL,&base);assert(again);
   assert(!memcmp(PreparedScreenScaleDecorationMetrics(again),m,sizeof(*m)));ReleaseScreenScaleDecoration(again,&base);
   struct PreparedScreenScaleDecoration*reset=PrepareScreenScaleDecoration(&is.screen,&basefont,&basefont,100,NULL,&base);assert(reset);
   assert(PreparedScreenScaleDecorationMetrics(reset)->BarHBorder==1&&PreparedScreenScaleDecorationMetrics(reset)->MenuVBorder==1);ReleaseScreenScaleDecoration(reset,&base);
   if(outcome==2){hardtrap=1;InstallScreenScaleDecoration(p,TRUE);hardtrap=0;
    assert(!memcmp(&before,&is,sizeof is)&&!memcmp(&beforeg,&gadget,sizeof gadget));}
  }
  ReleaseScreenScaleDecoration(p,&base);assert(live==baseline);
  DisposeObject(oldimage);DisposeObject(nextimage);
  assert(!live);
  for(unsigned f=1;f<=total;f++){
   allocs=failat=0;setup(&is,&gadget,&decor,&sd);
   if(mode==2)is.screen.Flags|=SCREENQUIET;
   if(mode==3){is.depthgadget=NULL;gadget.GadgetRender=NULL;}
   before=is;beforeg=gadget;baseline=live;allocs=0;failat=f;
   p=PrepareScreenScaleDecoration(&is.screen,&basefont,&font,200,NULL,&base);assert(!p);
   assert(live==baseline&&!memcmp(&before,&is,sizeof is)&&!memcmp(&beforeg,&gadget,sizeof gadget));
   assert(!memcmp(&beforesd,&sd,sizeof sd)&&!memcmp(&beforedata,&data,sizeof data));
   failat=0;DisposeObject(oldimage);DisposeObject(nextimage);assert(!live);
  }
 }
 /* Same proposed scale with a different baseline must use the explicit
  * basefont, independent of the old screen font and live scale. */
 for(unsigned scale=100;scale<=400;scale+=25){
  struct ScreenScaleMetrics m={0};
  assert(scrdecor_scale_metrics(&config,13,(13*scale+50)/100,scale,&m));
  assert(m.WBorTop==6&&m.WBorLeft==3&&m.WBorRight==4&&m.WBorBottom==5);
  assert(m.BarHeight+1==(18*scale+50)/100);
  assert(m.DepthRightInset==(2*scale+50)/100);
  struct ScreenScaleMetrics larger={0};
  assert(scrdecor_scale_metrics(&config,26,(26*scale+50)/100,scale,&larger));
  assert(larger.WBorTop==1);
 }
 struct DecorConfig negative=config;negative.SBarGadPost_s=-2;
 struct ScreenScaleMetrics signedmetrics={0};
 assert(scrdecor_scale_metrics(&negative,13,16,125,&signedmetrics));
 assert(signedmetrics.DepthRightInset==-3);
 assert(!scrdecor_scale_metrics(&config,0xffffffffu,26,200,&signedmetrics));
 assert(!scrdecor_scale_metrics(&config,13,0,200,&signedmetrics));
 assert(scale_bar_height(26,1,1,200)==29);
 assert(scale_bar_height(26,0,0,200)==23);
 assert(!scrdecor_scale_metrics(&config,13,26,101,&(struct ScreenScaleMetrics){0}));
 /* Unknown subclasses cannot accidentally inherit a supported capability. */
 Object builtinobject={0,0,&built,&data},externalobject={0,0,&externalclass,&data};
 assert(ScrDecorClass__SDM_QUERYCAPABILITIES(&built,&builtinobject,NULL)==SDM_CAP_PREPARESCALE);
 assert(scrdecor_querycapabilities(&externalclass,&externalobject,NULL)==SDM_CAP_PREPARESCALE);
 Object object={0,0,&unknown,&data};
 assert(!ScrDecorClass__SDM_QUERYCAPABILITIES(&built,&object,NULL));
 assert(!scrdecor_querycapabilities(&externalclass,&object,NULL));struct ScreenScaleMetrics metrics={0};
 struct sdpPrepareScale msg={SDM_PREPARESCALE,NULL,&font,&basefont,200,(IPTR)&sd,&metrics};
 assert(!ScrDecorClass__SDM_PREPARESCALE(&built,&object,&msg));
 assert(!scrdecor_preparescale(&externalclass,&object,&msg));
 object.class=&externalclass;sd.dts=NULL;assert(!scrdecor_preparescale(&externalclass,&object,&msg));
 sd.dts=(void*)0x1234;
 /* Invalid public BYTE metrics reject before image or live mutation. */
 Object decor={0,0,&built,&data};struct IntScreen is;struct Gadget gadget;
 allocs=failat=0;setup(&is,&gadget,&decor,&sd);struct IntScreen before=is;
 font.tf_YSize=200;assert(!PrepareScreenScaleDecoration(&is.screen,&basefont,&font,200,NULL,&base));
 assert(!memcmp(&before,&is,sizeof is));DisposeObject(oldimage);DisposeObject(nextimage);assert(!live);
 font.tf_YSize=26;allocs=failat=0;decor.class=&externalclass;
 setup(&is,&gadget,&decor,&sd);
 struct PreparedScreenScaleDecoration*p=PrepareScreenScaleDecoration(&is.screen,&basefont,&font,200,NULL,&base);assert(p);
 next.src_PhysicalBarHeight=PreparedScreenScaleDecorationMetrics(p)->BarHeight+1;nextimage->Height=next.src_PhysicalBarHeight;assert(BindScreenScaleDecoration(p,&next));
 InstallScreenScaleDecoration(p,FALSE);gadget.GadgetType=GTYP_SDEPTH;
 data.ChildLayoutPending=FALSE;struct sdpLayoutScreenGadgets layout={&gadget,(IPTR)&sd,0};
 scrdecor_layoutscrgadgets(&externalclass,&decor,&layout);assert(data.ChildLayoutPending);
 hardtrap=1;InstallScreenScaleDecoration(p,FALSE);hardtrap=0;assert(gadget.TopEdge==0);
 ReleaseScreenScaleDecoration(p,&base);DisposeObject(oldimage);DisposeObject(nextimage);assert(!live);
 /* A font which fits the BYTE by itself can still overflow once the
    physical title padding is included. The complete prepare must refuse
    without changing the screen, gadget or installed theme resources. */
 allocs=failat=0;setup(&is,&gadget,&decor,&sd);
 before=is;struct Gadget beforegadget=gadget;
 struct ScreenData beforetheme=sd;struct scrdecor_data beforedecor=data;
 font.tf_YSize=125;
 assert(!PrepareScreenScaleDecoration(&is.screen,&basefont,&font,200,NULL,&base));
 assert(!memcmp(&before,&is,sizeof is)&&!memcmp(&beforegadget,&gadget,sizeof gadget));
 assert(!memcmp(&beforetheme,&sd,sizeof sd)&&!memcmp(&beforedecor,&data,sizeof data));
 DisposeObject(oldimage);DisposeObject(nextimage);assert(!live);
 font.tf_YSize=26;
 printf("PASS actual decoration prepare: builtin/shipped/read-only theme; allocation failures; install+rollback hardtrap; abort/commit ownership; subclasses refuse\n");
}
'''
mutations={'install-hook':body.replace('    screen = p->screen;','    DoMethodA(GetPrivScreen(p->screen)->ScrDecorObj, NULL);\n    screen = p->screen;'),
 'install-allocation':body.replace('    screen = p->screen;','    AllocVec(1,0);\n    screen = p->screen;'),
 'release-borrowed-image':body.replace('    FreeVec(p);','    DisposeObject(ScreenRenderContextDepthImage(CurrentScreenRenderContext(p->screen)));\n    FreeVec(p);'),
 'double-scaled-live-padding':body.replace('const struct ScreenScaleMetrics *logical = ScreenRenderContextLogicalMetrics(context);','struct ScreenScaleMetrics live; capture_scale_metrics(screen, &live); (void)context; const struct ScreenScaleMetrics *logical = &live;'),
 'unknown-subclass-inherits':builtin.replace('OCLASS(obj) != cl || ',''),
 'missing-child-layout':external.replace('if (data->FirstChild) data->ChildLayoutPending = TRUE;', 'data->ChildLayoutPending = FALSE;'),
 'physical-font-used-as-baseline':external.replace('msg->sdp_BaseFont->tf_YSize,','msg->sdp_Font->tf_YSize,'),
 'ignore-proposed-scale':external.replace('msg->sdp_Font->tf_YSize, msg->sdp_Scale, metrics)','msg->sdp_Font->tf_YSize, 100, metrics)'),
 'prepare-live-mutation':body.replace('    p->screen = screen;','    screen->BarHeight++;\n    p->screen = screen;')}
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp)
 for name,changed in [('production',body),*mutations.items()]:
  b=builtin if name!='unknown-subclass-inherits' else changed
  code=body if name in ('unknown-subclass-inherits','missing-child-layout','physical-font-used-as-baseline','ignore-proposed-scale') else changed
  e=changed if name in ('missing-child-layout','physical-font-used-as-baseline','ignore-proposed-scale') else external
  c=d/f'{name}.c';exe=d/name;c.write_text(prefix+result_definitions(ROOT)+public+context_boundary+header+b+e+dispatch+code+main)
  result=subprocess.run(['clang','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(exe)],capture_output=True,text=True)
  if result.returncode:raise RuntimeError(result.stderr)
  result=subprocess.run([str(exe)],capture_output=True,text=True)
  if name=='production':
   if result.returncode:raise RuntimeError(result.stdout+result.stderr)
   print(result.stdout.strip())
  elif not result.returncode:raise RuntimeError(f'mutation escaped: {name}')
  else:print(f'REJECT {name}')
