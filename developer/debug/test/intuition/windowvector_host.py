#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License (APL) Version 1.1.
"""Actual builtin window vector primitives versus integer pixel enlargement.
Exec is absent; real vector code draws into a checked host pixel RastPort.
"""
import argparse,pathlib,re,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[4]);a=p.parse_args();s=(a.source/'rom/intuition/windecorclass.c').read_text();h=(a.source/'compiler/include/intuition/windecorclass.h').read_text()
def block(source,start):
 left=source.index('{',start);depth=1;end=left+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
def structure(name):
 m=re.search(r'struct\s+'+name+r'\s*\{',h);return block(h,m.start())+';\n'
body=block(s,s.index('static UWORD getbgpen('))+'\n'+s[s.index('static ULONG window_scale_metric('):s.index('\nstatic void window_scale_refuse(')]
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;typedef int8_t BYTE;typedef uint8_t UBYTE;typedef uint16_t UWORD;typedef uint32_t ULONG;typedef int32_t LONG;typedef uint64_t UQUAD;typedef uintptr_t IPTR;typedef void*APTR;
#define STACKED
#define TRUE 1
#define FALSE 0
#define DEFSIZE_WIDTH 14
#define IDS_NORMAL 0
#define IDS_SELECTED 1
#define IDS_INACTIVESELECTED 3
#define FILLPEN 0
#define BACKGROUNDPEN 1
#define SHADOWPEN 2
#define SHINEPEN 3
#define JAM1 0
enum{DEPTHIMAGE=0x00,ZOOMIMAGE=0x01,SIZEIMAGE=0x02,CLOSEIMAGE=0x03,LEFTIMAGE=0x0A,UPIMAGE=0x0B,RIGHTIMAGE=0x0C,DOWNIMAGE=0x0D,ICONIFYIMAGE=0x12,LOCKIMAGE=0x13,MUIIMAGE=0x14,POPUPIMAGE=0x15,SNAPSHOTIMAGE=0x16,JUMPIMAGE=0x17};
struct TextFont;struct DrawInfo{UWORD*dri_Pens;};struct ScreenRenderContext{UWORD src_Scale;struct DrawInfo*src_LogicalDrawInfo,*src_PhysicalDrawInfo;};
struct GfxBase{int unused;};struct IntuitionBase{struct GfxBase*GfxBase;};
#define GetPrivIBase(b) (b)
struct RastPort{unsigned char pixels[96*96],pen;LONG width,height;};
#define SetDrMd(rp,mode) ((void)GfxBase,(void)(mode),(void)(rp))
#define SetAPen(rp,p) ((void)GfxBase,(rp)->pen=(p))
#define RectFill(rp,l,t,r,b) ((void)GfxBase,fill(rp,l,t,r,b))
static void fill(struct RastPort*rp,LONG l,LONG t,LONG r,LONG b){assert(l>=0&&t>=0&&r<rp->width&&b<rp->height&&l<=r&&t<=b);for(LONG y=t;y<=b;y++)for(LONG x=l;x<=r;x++)rp->pixels[y*96+x]=rp->pen;}
'''
pre+=structure('wdpScaleRefusal')+''.join(l.split('/*')[0].rstrip()+'\n' for l in h.splitlines() if re.match(r'#define\s+WDP_REFUSE_\w+\s',l))
pre+=''.join(structure(n) for n in ['wdpGetDefSizeSysImage','wdpGetScaleDefSizeSysImage','wdpDrawSysImage','wdpDrawScaleSysImage'])
main=r'''
static void render(struct RastPort*rp,struct IntuitionBase*base,unsigned scale,unsigned logical,unsigned type,unsigned state,unsigned width,unsigned height){UWORD pens[]={1,2,3,4};struct DrawInfo di={pens};struct ScreenRenderContext ctx={scale,&di,&di};memset(rp,0,sizeof(*rp));rp->width=width;rp->height=height;struct wdpDrawScaleSysImage m={.base={.wdp_Dri=&di,.wdp_RPort=rp,.wdp_Width=width,.wdp_Height=height,.wdp_Which=type,.wdp_State=state},.context=&ctx,.logical=logical};assert(WindowScaleVectorDraw(&m,base));for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)assert(rp->pixels[y*96+x]);}
int main(void){struct GfxBase gfx;struct IntuitionBase base={&gfx};struct RastPort old,actual;unsigned cases=0;for(unsigned ti_=0,type;ti_<sizeof((unsigned[]){DEPTHIMAGE,ZOOMIMAGE,SIZEIMAGE,CLOSEIMAGE,LEFTIMAGE,UPIMAGE,RIGHTIMAGE,DOWNIMAGE,ICONIFYIMAGE,LOCKIMAGE,MUIIMAGE,POPUPIMAGE,SNAPSHOTIMAGE,JUMPIMAGE})/sizeof(unsigned)&&((type=((unsigned[]){DEPTHIMAGE,ZOOMIMAGE,SIZEIMAGE,CLOSEIMAGE,LEFTIMAGE,UPIMAGE,RIGHTIMAGE,DOWNIMAGE,ICONIFYIMAGE,LOCKIMAGE,MUIIMAGE,POPUPIMAGE,SNAPSHOTIMAGE,JUMPIMAGE})[ti_]),1);ti_++)for(unsigned state=0;state<4;state++)for(unsigned width=13;width<=15;width++)for(unsigned height=13;height<=15;height++){render(&old,&base,100,0,type,state,width,height);for(unsigned scale=100;scale<=400;scale+=25){unsigned w=(width*scale+50)/100,h=(height*scale+50)/100;render(&actual,&base,scale,0,type,state,w,h);if(scale%100==0){unsigned f=scale/100;for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)assert(actual.pixels[y*96+x]==old.pixels[(y/f)*96+x/f]);}render(&actual,&base,scale,1,type,state,width,height);assert(!memcmp(actual.pixels,old.pixels,sizeof(old.pixels)));cases++;}}
for(unsigned width=1;width<=6;width++)for(unsigned height=1;height<=6;height++)for(unsigned ti_=0,type;ti_<sizeof((unsigned[]){DEPTHIMAGE,ZOOMIMAGE,SIZEIMAGE,CLOSEIMAGE,LEFTIMAGE,UPIMAGE,RIGHTIMAGE,DOWNIMAGE,ICONIFYIMAGE,LOCKIMAGE,MUIIMAGE,POPUPIMAGE,SNAPSHOTIMAGE,JUMPIMAGE})/sizeof(unsigned)&&((type=((unsigned[]){DEPTHIMAGE,ZOOMIMAGE,SIZEIMAGE,CLOSEIMAGE,LEFTIMAGE,UPIMAGE,RIGHTIMAGE,DOWNIMAGE,ICONIFYIMAGE,LOCKIMAGE,MUIIMAGE,POPUPIMAGE,SNAPSHOTIMAGE,JUMPIMAGE})[ti_]),1);ti_++)render(&actual,&base,400,0,type,0,width,height);
printf("PASS actual builtin vectors: %u cases all13scales/14images/4states, integer whole-pixel and logical-domain oracle, tiny extents clipped\n",cases);return 0;}
'''
variants=[('actual',body),('ignore-domain',body.replace('scale = msg->logical ? 100 : msg->context->src_Scale;','scale = msg->context->src_Scale;')),('lose-endpixel',body.replace('(UQUAD)(r + 1)','(UQUAD)r')),('physical-loops',body.replace('w = ((UQUAD)m->wdp_Width * 100 + scale / 2) / scale;','w = m->wdp_Width;'))]
with tempfile.TemporaryDirectory(prefix='window-vector-') as tmp:
 tmp=pathlib.Path(tmp)
 for name,code in variants:
  c=tmp/(name+'.c');exe=tmp/name;c.write_text(pre+code+main);subprocess.run(['clang','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True);q=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  if name=='actual':
   if q.returncode:raise SystemExit(q.stderr)
   print(q.stdout,end='')
  elif not q.returncode:raise SystemExit('escaped mutation '+name)
  else:print('REJECTED '+name)
