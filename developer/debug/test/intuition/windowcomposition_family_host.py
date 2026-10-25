#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Check production mixed-parent geometry with independent pixel-phase oracles.

The graphics/layer containers are host substitutes; this does not qualify their
runtime behavior or a native display. Mutations prove detection of the original
mixed-factor origin defect, negative-floor mistakes and cross-Layer_Info use.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
source=(ROOT/'rom/intuition/windowcomposition.c').read_text()
body=source[source.index('static struct Layer *physical_parent('):source.index('\n\n/* A custom shape')]
prefix=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef int16_t WORD; typedef int64_t QUAD; typedef uint64_t UQUAD; typedef uintptr_t IPTR; typedef int BOOL;
#define TRUE 1
#define FALSE 0
struct Layer_Info { int unused; };
struct Rectangle { LONG MinX,MinY,MaxX,MaxY; };
struct Layer { struct Layer_Info *LayerInfo; struct Rectangle bounds; LONG Width,Height; };
struct BitMap { BOOL hidd; };
struct Screen { struct Layer_Info LayerInfo; struct { struct BitMap *BitMap; } RastPort; ULONG Scale; };
struct Window { LONG LeftEdge,TopEdge,Width,Height,RelLeftEdge,RelTopEdge; struct Window *parent; struct Screen *WScreen; struct Layer *layer; };
#define IS(s) (s)
#define IS_HIDD_BM(b) ((b)->hidd)
static struct Layer *WindowCompositionLayer(struct Window *w) { return w->layer; }
static LONG oracle_floor(LONG v,LONG f) { return v>=0?v/f:-((-v+f-1)/f); }
'''
main=r'''
int main(void) {
 struct BitMap bm={1}; struct Screen screen={{0},{&bm},100};
 struct Layer_Info otherLI={0}; struct Layer layer={&screen.LayerInfo,{0},0,0};
 struct Window parent={0,0,0,0,0,0,0,&screen,&layer};
 struct Window child={0,0,3,2,0,0,&parent,&screen,0};
 LONG origins[]={-13,-5,-1,0,1,5,11,15}; LONG offsets[]={-2,-1,0,1,3}; unsigned cases=0;
 for(int pf=1;pf<=4;pf++)for(int cf=1;cf<=4;cf++)for(int oi=0;oi<8;oi++)for(int ri=0;ri<5;ri++)for(int aware=0;aware<2;aware++) {
  LONG k=aware?1:cf,x,y,w,h;
  screen.Scale=cf*100; layer.bounds.MinX=origins[oi]*pf+1;layer.bounds.MinY=origins[7-oi]*pf-1;
  layer.Width=9*pf+1;layer.Height=7*pf+1;
  child.RelLeftEdge=offsets[ri];child.RelTopEdge=offsets[4-ri];
  assert(WindowCompositionChildPosition(&parent,&screen,aware,child.RelLeftEdge,child.RelTopEdge,&x,&y));
  assert(x==oracle_floor(layer.bounds.MinX,k)+child.RelLeftEdge);
  assert(y==oracle_floor(layer.bounds.MinY,k)+child.RelTopEdge);
  child.LeftEdge=x;child.TopEdge=y;
  assert(WindowCompositionChildExtent(&parent,&screen,aware,&w,&h));
  assert(w==layer.Width/k&&h==layer.Height/k);
  struct ScaleComposeRect bounds;
  assert(physical_geometry(&child,k,&bounds));
  assert(bounds.left==layer.bounds.MinX+child.RelLeftEdge*k);
  assert(bounds.top==layer.bounds.MinY+child.RelTopEdge*k);
  assert(bounds.right-bounds.left==child.Width*k);
  assert(bounds.bottom-bounds.top==child.Height*k);
  /* Physical parent phase must be retained even when public origin floors it. */
  assert(bounds.left-child.LeftEdge*k==layer.bounds.MinX-oracle_floor(layer.bounds.MinX,k)*k);
  ++cases;
 }
 LONG x=123,y=456,w=789,h=987;
 layer.LayerInfo=&otherLI;
 assert(!WindowCompositionChildPosition(&parent,&screen,0,0,0,&x,&y));assert(x==123&&y==456);
 assert(!WindowCompositionChildExtent(&parent,&screen,0,&w,&h));assert(w==789&&h==987);
 struct ScaleComposeRect b; assert(!physical_geometry(&child,2,&b));
 layer.LayerInfo=&screen.LayerInfo;layer.bounds.MinX=32767;
 assert(!WindowCompositionChildPosition(&parent,&screen,1,2147483647,0,&x,&y));assert(x==123&&y==456);
 bm.hidd=0;layer.bounds.MinX=-3;layer.bounds.MinY=-5;
 assert(WindowCompositionChildPosition(&parent,&screen,0,1,2,&x,&y));assert(x==-2&&y==-3);
 puts("PASS actual mixed-parent helpers/geometry: 1280 mixed1..4 cases, odd/negative phase, extent, overflow, nonHIDD and crossLI rejection");
 assert(cases==1280);
}
'''
with tempfile.TemporaryDirectory(prefix='aros-mixed-parent-') as d:
 d=Path(d);(d/'exec').mkdir();(d/'exec/types.h').write_text('')
 for label,implementation in [('production',body),('double-parent-origin',body.replace('transform.x = parent->bounds.MinX;', 'transform.x = parent->bounds.MinX * factor;')),('truncated-negative-origin',body.replace('ScaleComposeFloor(layer->bounds.MinX, factor)','(layer->bounds.MinX / factor)')),('cross-layerinfo',body.replace('layer->LayerInfo != &screen->LayerInfo','FALSE'))]:
  c=prefix+'\n#include "'+str(ROOT/'rom/graphics/scalecompose.h')+'"\n'+implementation+main
  (d/'test.c').write_text(c)
  subprocess.run(['clang','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I',str(d),str(d/'test.c'),'-o',str(d/'test')],check=True)
  result=subprocess.run([str(d/'test')],capture_output=True,text=True)
  if label=='production':
   assert result.returncode==0,result.stdout+result.stderr
   print(result.stdout,end='')
  else:
   assert result.returncode!=0,'negative control escaped: '+label
   print('PASS negative control:',label)
