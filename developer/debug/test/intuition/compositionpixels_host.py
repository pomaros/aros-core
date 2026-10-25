#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Run actual prepared composition transport with clipped odd physical extents."""
import argparse
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path);a=p.parse_args();root=(a.source or Path(__file__).resolve().parents[4]).resolve()
s=(root/'rom/intuition/windowcomposition.c').read_text();start=s.index('BOOL PrepareWindowCompositionPixels(');end=s.index('\nstatic void capture_composition',start);body=s[start:end]
prefix=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG;typedef int32_t LONG;typedef uint8_t UBYTE;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 0
#define MEMF_CLEAR 1
struct PreparedPixel{uint16_t red,green,blue,alpha;};
struct IntuitionBase{struct LayersBase*LayersBase;};struct LayersBase{int x;};
#define GetPrivIBase(b) (b)
struct ScaleComposeTransform{ULONG factor;};
struct WindowCompositionValues{struct ScaleComposeTransform transform;LONG logicalWidth,logicalHeight,logicalLeft,logicalTop;};
struct WindowComposition{void*proxy,*logical;ULONG width,height;};
struct WindowCompositionScale{struct WindowComposition*state;struct WindowCompositionValues next;ULONG physicalWidth,physicalHeight;};
struct LayerGeometryBatch{int x;};
static unsigned fail,ordinal,live,calls;
static void*AllocVec(ULONG n,ULONG f){(void)f;if(++ordinal==fail)return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void FreeVec(void*p){if(p){assert(live);live--;free(p);}}
static BOOL ReadLayerGeometryPixels(struct LayerGeometryBatch*b,void*l,ULONG x,ULONG y,ULONG w,ULONG h,struct PreparedPixel*p,ULONG stride){(void)b;(void)l;assert(h==1&&stride>=w*sizeof(*p));for(unsigned i=0;i<w;i++)p[i]=(struct PreparedPixel){x+i,y,0x1234,0xffff};return TRUE;}
static struct WindowCompositionScale*expected;
static BOOL WriteLayerGeometryPixels(struct LayerGeometryBatch*b,void*l,const struct PreparedPixel*p,ULONG stride,ULONG height){(void)b;(void)l;calls++;assert(stride==expected->physicalWidth*sizeof(*p)&&height==expected->physicalHeight);for(unsigned y=0;y<height;y++)for(unsigned x=0;x<expected->physicalWidth;x++){
 unsigned f=expected->next.transform.factor;const struct PreparedPixel*q=(const void*)((const UBYTE*)p+y*stride+x*sizeof(*p));LONG sx=expected->next.logicalLeft+x/f,sy=expected->next.logicalTop+y/f;
 BOOL valid=x/f<(ULONG)expected->next.logicalWidth&&y/f<(ULONG)expected->next.logicalHeight&&sx>=0&&sy>=0&&sx<(LONG)expected->state->width&&sy<(LONG)expected->state->height;
 if(valid)assert(q->red==sx&&q->green==sy&&q->blue==0x1234&&q->alpha==0xffff);else assert(!q->red&&!q->green&&!q->blue&&!q->alpha);
 }return TRUE;}
'''
main=r'''
int main(void){struct IntuitionBase base={0};struct WindowComposition state={(void*)1,(void*)2,50,40};unsigned cases=0;
for(unsigned factor=1;factor<=4;factor++)for(int x=-2;x<=1;x++)for(int y=-2;y<=1;y++)for(int dx=-1;dx<=1;dx++)for(int dy=-1;dy<=1;dy++){
 struct WindowCompositionScale p={&state,{{factor},7,5,x,y},7*factor+dx,5*factor+dy};expected=&p;
 for(fail=0;fail<=2;fail++){ordinal=calls=0;BOOL ok=PrepareWindowCompositionPixels(&p,NULL,&base);assert(ok==(!fail));assert(calls==(!fail));assert(!live);}cases++;}
printf("PASS actual prepared composition: %u clipped/odd extent cases, factors1..4, negative origins, both allocation faults, exact source/destination bounds\n",cases);return 0;}
'''
mutants={'logical-instead-of-physical':body.replace('width = prepared->physicalWidth;','width = (ULONG)next->logicalWidth * factor;'),'read-row-overrun':body.replace('x < width && x/factor < (ULONG)next->logicalWidth','x < width')}
with tempfile.TemporaryDirectory(prefix='composition-pixels-') as td:
 for name,b in [('production',body),*mutants.items()]:
  c=Path(td)/(name+'.c');exe=Path(td)/name;c.write_text(prefix+b+main)
  r=subprocess.run(['clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],capture_output=True,text=True);assert r.returncode==0,r.stderr
  r=subprocess.run([str(exe)],capture_output=True,text=True)
  if name=='production':assert r.returncode==0,r.stdout+r.stderr;print(r.stdout,end='')
  else:assert r.returncode!=0,'mutation survived '+name;print('REJECT '+name)
print('actual composition transport ASan/UBSan result=PASS')
