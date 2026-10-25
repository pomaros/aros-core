#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Exercise actual ASL image constructor, destructor and frame paint bodies."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
from aslscale_host import ROOT, SRC, SDK, body

PRELUDE=r'''
#include "asl_intern.h"
#include "layout.h"
#include <intuition/imageclass.h>
#include <libraries/coolimages.h>
#include <exec/memory.h>
#include <string.h>
extern void *malloc(__SIZE_TYPE__);
extern void free(void *);
extern void abort(void);
extern int printf(const char *,...);
#define CHECK(x) do{if(!(x)){printf("CHECK %d: %s\n",__LINE__,#x);abort();}}while(0)
#define AslBase ((struct AslBase_intern *)cl->cl_UserData)
static APTR CyberGfxBase=(APTR)1;
struct Instance {struct Gadget g;struct AslButtonData data;};
static ULONG live,allocs,fail;
static APTR AllocVec(ULONG n,ULONG flags) {(void)flags;++allocs;if(allocs==fail)return NULL;APTR p=malloc(n);CHECK(p);memset(p,0,n);++live;return p;}
static VOID FreeVec(APTR p) {if(p){CHECK(live);--live;free(p);}}
static struct TagItem *FindTagItem(Tag tag,struct TagItem *t) {for(;t->ti_Tag;t++)if(t->ti_Tag==tag)return t;return NULL;}
static IPTR GetTagData(Tag tag,IPTR fallback,struct TagItem *t) {struct TagItem *i=FindTagItem(tag,t);return i?i->ti_Data:fallback;}
static IPTR DoSuperMethodA(Class *cl,Object *o,Msg msg) {(void)cl;if(msg->MethodID==OM_NEW)return (IPTR)AllocVec(sizeof(struct Instance),MEMF_CLEAR);if(msg->MethodID==OM_DISPOSE)FreeVec(o);return 0;}
Object *AslFrame(struct LayoutData *ld,struct TagItem *tags,struct AslBase_intern *b) {(void)ld;(void)tags;(void)b;return AllocVec(1,0);}
static VOID DisposeObject(Object *o) {FreeVec(o);}
IPTR AslButton__OM_DISPOSE(Class *,Object *,Msg);
static IPTR CoerceMethod(Class *cl,Object *o,ULONG method) {return AslButton__OM_DISPOSE(cl,o,(Msg)&method);}
static ULONG pixels[160*120];
static LONG cursorx,cursory;
static VOID SetAPen(struct RastPort *rp,ULONG pen) {rp->FgPen=pen;}
static VOID SetDrMd(struct RastPort *rp,ULONG mode) {rp->DrawMode=mode;}
static VOID SetABPenDrMd(struct RastPort *rp,ULONG a,ULONG b,ULONG mode) {rp->FgPen=a;rp->BgPen=b;rp->DrawMode=mode;}
static VOID RectFill(struct RastPort *rp,LONG x,LONG y,LONG x2,LONG y2) {CHECK(x>=0&&y>=0&&x2<160&&y2<120&&x<=x2&&y<=y2);for(;y<=y2;y++)for(LONG a=x;a<=x2;a++)pixels[y*160+a]=rp->FgPen;}
static VOID Move(struct RastPort *rp,LONG x,LONG y) {(void)rp;cursorx=x;cursory=y;}
static VOID Draw(struct RastPort *rp,LONG x,LONG y) {CHECK(x==cursorx||y==cursory);LONG l=x<cursorx?x:cursorx,r=x>cursorx?x:cursorx,t=y<cursory?y:cursory,b=y>cursory?y:cursory;RectFill(rp,l,t,r,b);cursorx=x;cursory=y;}
'''
DRIVER=r'''
int main(void) {
 Class cl={0};cl.cl_InstOffset=__builtin_offsetof(struct Instance,data);
 struct AslBase_intern lib={0};cl.cl_UserData=(IPTR)&lib;
 struct LayoutData ld={0};ld.ld_TrueColor=TRUE;
 UBYTE source[]={0,1,1,0,1,0},palette[]={0,0,0,255,127,63};
 struct CoolImage cool={.data=source,.pal=palette,.width=3,.height=2,.numcolors=2};
 ULONG scales[]={100,125,150,175,200,225,250,275,300,325,350,375,400,100};
 for(ULONG i=0;i<sizeof(scales)/sizeof(scales[0]);i++) {
  ld.ld_RenderScale=scales[i];
  struct TagItem tags[]={{GA_UserData,(IPTR)&ld},{GA_Image,1},{ASLBT_CoolImage,(IPTR)&cool},{TAG_DONE,0}};
  struct opSet set={OM_NEW,tags,NULL};
  ULONG before=allocs;
  struct Instance *o=(APTR)AslButton__OM_NEW(&cl,NULL,&set);CHECK(o);
  ULONG points=allocs-before;
  CHECK(o->data.coolwidth==AslScaleValue(&ld,3)&&o->data.coolheight==AslScaleValue(&ld,2));
  for(ULONG y=0;y<o->data.coolheight;y++)for(ULONG x=0;x<o->data.coolwidth;x++)CHECK(o->data.coolpixels[y*o->data.coolwidth+x]==source[(y*2/o->data.coolheight)*3+x*3/o->data.coolwidth]);
  ULONG dispose=OM_DISPOSE;AslButton__OM_DISPOSE(&cl,(Object *)o,(Msg)&dispose);CHECK(!live);
  for(ULONG n=1;n<=points;n++){fail=allocs+n;o=(APTR)AslButton__OM_NEW(&cl,NULL,&set);fail=0;CHECK(!o&&!live);}
  struct {struct Image image;struct AslFrameData data;} frame={0};Class fc={0};fc.cl_InstOffset=__builtin_offsetof(typeof(frame),data);
  UWORD pens[NUMDRIPENS];for(ULONG n=0;n<NUMDRIPENS;n++)pens[n]=n+1;
  struct DrawInfo di={.dri_Pens=pens};ld.ld_Dri=&di;frame.data.ld=&ld;frame.data.type=FRAME_BUTTON;
  struct RastPort rp={.FgPen=77,.BgPen=88,.DrawMode=3};struct impDraw draw={0};draw.imp_RPort=&rp;
  LONG width=AslScaleValue(&ld,30),height=AslScaleValue(&ld,20),stroke=AslScaleValue(&ld,1);
  memset(pixels,0,sizeof(pixels));CHECK(PaintFrame(&fc,&frame.image,&draw,width,height));
  CHECK(rp.FgPen==77&&rp.BgPen==88&&rp.DrawMode==3);
  CHECK(pixels[stroke*160+stroke]==pens[BACKGROUNDPEN]);
  for(LONG n=0;n<stroke;n++)CHECK(pixels[n*160+width/2]==pens[SHINEPEN]);
  CHECK(pixels[(height-1)*160+width/2]==pens[SHADOWPEN]);
  printf("ASL resources scale%u constructor faultpoints%u pixels PASS\n",scales[i],points);
 }
 CHECK(!live);return 0;
}
'''
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path);args=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='aslresources-') as tmp:
        out=args.out or Path(tmp);out.mkdir(parents=True,exist_ok=True)
        actual=PRELUDE+'\n'+body('buttonclass.c','AslButton__OM_NEW')+'\n'+body('buttonclass.c','AslButton__OM_DISPOSE')+'\n'+body('frameclass.c','PaintFrame')+'\n'+DRIVER
        path=out/'actual.c';path.write_text(actual)
        cmd=['clang','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-pointer-sign','-I',str(SRC),'-I',str(ROOT/'compiler/include'),'-I',str(SDK/'bin/apple-aarch64/gen/include'),str(path),'-o',str(out/'actual')]
        x=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(x.stdout+x.stderr)
        if x.returncode:raise SystemExit(x.stderr)
        x=subprocess.run([cmd[-1]],capture_output=True,text=True);(out/'run.log').write_text(x.stdout+x.stderr);print(x.stdout+x.stderr,end='')
        if x.returncode:raise SystemExit(x.returncode)
        rejected=[]
        for name,old,new in [('image_scale','data->coolwidth = AslScaleValue(data->ld, data->coolimage->width);','data->coolwidth = data->coolimage->width;'),('pixel_ownership','if (data->coolpixels) FreeVec(data->coolpixels);','(void)data->coolpixels;'),('frame_stroke','LONG stroke = AslScaleValue(d->ld, 1), inset, n;','LONG stroke = 1, inset, n;')]:
            if old not in actual:raise RuntimeError(name)
            mutant=out/(name+'.c');mutant.write_text(actual.replace(old,new,1));args=cmd.copy();args[-3]=str(mutant);args[-1]=str(out/name)
            x=subprocess.run(args,capture_output=True,text=True)
            if x.returncode:raise SystemExit(x.stderr)
            x=subprocess.run([args[-1]],capture_output=True,text=True);(out/(name+'.log')).write_text(x.stdout+x.stderr)
            if not x.returncode:raise RuntimeError('mutation survived '+name)
            rejected.append(name)
        (out/'result.json').write_text(json.dumps({'status':'PASS','scope':'actual constructors/destructor/frame paint with OS/raster adapters; no native proof','rejected_mutations':rejected,'command':cmd},indent=2))
if __name__=='__main__':main()
