#!/usr/bin/env python3
# Copyright (C) 2026 John Knipper.
# Distributed under the AROS Public License, version 1.1.
"""Execute the real bounded CLI snapshot under sanitizer OS adapters.
This verifies observation ownership/refusal, not native Cancel behavior.
"""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[4]
header = (root / 'workbench/c/DisplayScale/snapshot.h').read_text()
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uintptr_t IPTR;typedef uint32_t ULONG;typedef int32_t LONG;
typedef uint16_t UWORD;typedef uint8_t UBYTE;typedef int BOOL;typedef void Object;
#define TRUE 1
#define FALSE 0
#define SA_RenderContext 42
struct TextFont{UWORD tf_YSize;};struct RastPort{struct TextFont *Font;UWORD TxHeight;};
struct DrawInfo{struct Screen *dri_Screen;struct TextFont *dri_Font;};
struct ScreenRenderContext{struct DrawInfo *src_LogicalDrawInfo,*src_PhysicalDrawInfo;ULONG src_Scale,src_LegacyScaleFactor,src_PhysicalBarHeight;};
struct MsgPort{void *mp_SigTask;};
struct Window{struct Window *NextWindow;struct RastPort *RPort;struct MsgPort *UserPort;LONG LeftEdge,TopEdge,Width,Height,MinWidth,MinHeight,MaxWidth,MaxHeight;UWORD BorderLeft,BorderTop,BorderRight,BorderBottom;UBYTE *Title;};
struct Screen{struct Window *FirstWindow;struct RastPort RastPort;LONG Width,Height;struct ScreenRenderContext *ctx;};
static int locked,attribute_reads;
static ULONG LockIBase(int n){assert(!n&&!locked);locked=1;return 7;}
static void UnlockIBase(ULONG key){assert(key==7&&locked);locked=0;}
static BOOL GetAttr(ULONG key,Object *object,IPTR *value){assert(locked&&key==SA_RenderContext);++attribute_reads;*value=(IPTR)((struct Screen*)object)->ctx;return *value!=0;}
'''
tests = r'''
int main(void){
 struct TextFont logical={13},physical={26};struct Screen screen={0};
 struct DrawInfo ld={&screen,&logical},pd={&screen,&physical};
 struct ScreenRenderContext ctx={&ld,&pd,200,2,36};screen.ctx=&ctx;screen.RastPort.Font=&physical;screen.Width=2560;screen.Height=1600;
 struct RastPort rp={&logical,13};struct MsgPort port={(void*)123};UBYTE title[]={65,10,34,0xc3,0xa9,0};
 struct Window window={.RPort=&rp,.UserPort=&port,.LeftEdge=17,.TopEdge=19,.Width=300,.Height=200,.MinWidth=21,.MinHeight=23,.MaxWidth=999,.MaxHeight=777,.BorderLeft=2,.BorderTop=16,.BorderRight=3,.BorderBottom=4,.Title=title};screen.FirstWindow=&window;
 struct ScaleSnapshot *s=malloc(sizeof(*s));assert(s);memset(s,0xa5,sizeof(*s));
 assert(CaptureScaleSnapshot(&screen,s)&&!locked&&attribute_reads==1);
 assert(s->context==(IPTR)&ctx&&s->scale==200&&s->factor==2&&s->logicalFont==13&&s->physicalFont==26&&s->barHeight==36&&s->rasterWidth==2560&&s->count==1);
 struct ScaleWindowSnapshot *r=&s->windows[0];
 assert(r->identity==(IPTR)&window&&r->port==(IPTR)&port&&r->x==17&&r->y==19&&r->width==300&&r->height==200&&r->innerWidth==295&&r->innerHeight==180&&r->minWidth==21&&r->maxHeight==777&&r->canvasHeight==13);
 /* Closing/changing source resources after unlock cannot alter this result. */
 memset(title,0,sizeof title);window.Width=40;logical.tf_YSize=7;screen.ctx=NULL;
 assert(r->width==300&&r->canvasHeight==13&&s->scale==200);
 assert(!CaptureScaleSnapshot(&screen,s)&&!locked);
 screen.ctx=&ctx;pd.dri_Screen=NULL;assert(!CaptureScaleSnapshot(&screen,s)&&!locked);pd.dri_Screen=&screen;
 screen.RastPort.Font=&logical;assert(!CaptureScaleSnapshot(&screen,s)&&!locked);screen.RastPort.Font=&physical;
 pd.dri_Font=NULL;assert(!CaptureScaleSnapshot(&screen,s)&&!locked);pd.dri_Font=&physical;
 window.RPort=NULL;assert(!CaptureScaleSnapshot(&screen,s)&&!locked);window.RPort=&rp;
 /* Client resources can be retired independently of IBase. Their pointer
  * contents are deliberately unavailable; the cached height is sufficient. */
 window.UserPort=(struct MsgPort *)(uintptr_t)1; window.Title=NULL;rp.Font=NULL;
 assert(CaptureScaleSnapshot(&screen,s)&&!locked&&s->windows[0].canvasHeight==13&&s->windows[0].port==1);
 rp.Font=&logical;rp.TxHeight=29;
 assert(CaptureScaleSnapshot(&screen,s)&&!locked&&s->windows[0].canvasHeight==29);
 struct Window *many=calloc(SCALE_SNAPSHOT_CAPACITY+1,sizeof(*many));assert(many);
 for(unsigned i=0;i<=SCALE_SNAPSHOT_CAPACITY;i++){many[i]=window;many[i].NextWindow=i<SCALE_SNAPSHOT_CAPACITY?&many[i+1]:NULL;many[i].Title=NULL;many[i].UserPort=NULL;}
 screen.FirstWindow=many;assert(!CaptureScaleSnapshot(&screen,s)&&!locked);
 many[SCALE_SNAPSHOT_CAPACITY-1].NextWindow=NULL;
 assert(CaptureScaleSnapshot(&screen,s)&&s->count==SCALE_SNAPSHOT_CAPACITY&&!locked&&s->windows[0].port==0);
 screen.FirstWindow=NULL;assert(CaptureScaleSnapshot(&screen,s)&&s->count==0&&!locked);
 free(many);free(s);puts("PASS actual snapshot: immutable copies, exact scalar fields, null/incoherent resource refusals, capacity limits and client-independent cached height, balanced IBase; no native Cancel proof");}
'''
mutations = {
 'unsafe-client-port': ('row->port = (IPTR)window->UserPort;', 'row->port = window->UserPort ? (IPTR)window->UserPort->mp_SigTask : 0;'),
 'wrong-geometry': ('row->width = window->Width;', 'row->width = 0;'),
 'wrong-font-domain': ('row->canvasHeight = window->RPort->TxHeight;', 'row->canvasHeight = sample->physicalFont;'),
 'unsafe-client-font': ('row->canvasHeight = window->RPort->TxHeight;', 'row->canvasHeight = window->RPort->Font->tf_YSize;'),
 'unbalanced-lock': ('UnlockIBase(lock);', '(void)lock;'),
 'accept-incoherent-raster': ('screen->RastPort.Font != context->src_PhysicalDrawInfo->dri_Font', 'FALSE'),
 'omit-capacity-check': ('sample->count == SCALE_SNAPSHOT_CAPACITY ||', 'FALSE ||'),
}
with tempfile.TemporaryDirectory(prefix='scale-snapshot-') as directory:
 out=Path(directory)
 for name,body in [('production',header)]+[(k,header.replace(*v)) for k,v in mutations.items()]:
  if name!='production':assert body!=header,name
  source=out/'check.c';binary=out/'check';source.write_text(prefix+body+tests)
  result=subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(source),'-o',str(binary)],capture_output=True,text=True)
  assert result.returncode==0,(name,result.stderr)
  result=subprocess.run([str(binary)],capture_output=True,text=True)
  assert (result.returncode==0)==(name=='production'),(name,result.stdout,result.stderr)
  print(result.stdout.strip() if name=='production' else 'REJECTED '+name)
