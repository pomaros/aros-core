#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Fault-inject detached preparation using actual graphics region algorithms.

Exec pools, minlists and semaphores are host substitutes. Region algebra and
128-rectangle/20-chunk pool allocator are production code, not bitmap mocks.
Usage: preparedregions_host.py [--source CORE_CHECKOUT]
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import hashlib
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path)
a=parser.parse_args();ROOT=a.source or Path(__file__).resolve().parents[4]
def read(rel):return (ROOT/rel).read_text(encoding='latin1')
def strip(s):return '\n'.join(x for x in s.splitlines() if not x.lstrip().startswith('#include'))+'\n'
internal=read('rom/graphics/graphics_intern.h')
chunks=internal[internal.index('struct RegionRectangleExt {'):internal.index('/* PaletteExtra internals */')]
chunks+=internal[internal.index('#define _NewRegionRectangleExtChunk()'):internal.index('struct BlitWaitQNode')]
ops=[('struct Region *','NewRegion','void'),('void','ClearRegion','struct Region *region'),
 ('void','DisposeRegion','struct Region *region'),('BOOL','SetRegion','struct Region *src, struct Region *dest'),
 ('BOOL','OrRectRegion','struct Region *Reg, struct Rectangle *Rect'),
 ('void','AndRectRegion','struct Region *Reg, struct Rectangle *Rect'),
 ('BOOL','AndRegionRegion','struct Region *R1, struct Region *R2'),
 ('BOOL','ClearRegionRegion','struct Region *R1, struct Region *R2')]
protos='\n'.join(f'{typ} {name}({args});' for typ,name,args in ops)+'\n'
bodies=''
for typ,name,args in ops:
 text=read(f'rom/graphics/{name.lower()}.c')
 body=text.split('AROS_LIBFUNC_INIT',1)[1].split('AROS_LIBFUNC_EXIT',1)[0]
 bodies+=f'{typ} {name}({args})'+'{'+body+'}\n'
prefix=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef int16_t WORD; typedef uint16_t UWORD;
typedef uintptr_t IPTR; typedef uint8_t UBYTE; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MEMF_ANY 0
#define REGIONS_USE_MEMPOOL 1
#define SIZERECTBUF 128
#define ASSERT_VALID_PTR(p) assert(p)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
struct Rectangle { WORD MinX,MinY,MaxX,MaxY; };
struct RegionRectangle { struct RegionRectangle *Next,*Prev; struct Rectangle bounds; };
struct Region { struct Rectangle bounds; struct RegionRectangle *RegionRectangle; };
struct MinNode { struct MinNode *succ,*pred; };
struct MinList { struct MinNode *head,*tail,*tailpred; };
static void initlist(struct MinList*l){l->head=(void*)&l->tail;l->tail=NULL;l->tailpred=(void*)&l->head;}
static void addhead(struct MinList*l,void*p){struct MinNode*n=p;n->succ=l->head;n->pred=(void*)&l->head;l->head->pred=n;l->head=n;}
static void addtail(struct MinList*l,void*p){struct MinNode*n=p;n->succ=(void*)&l->tail;n->pred=l->tailpred;l->tailpred->succ=n;l->tailpred=n;}
static void rem(void*p){struct MinNode*n=p;n->pred->succ=n->succ;n->succ->pred=n->pred;}
#define NEWLIST(l) initlist(l)
#define ADDHEAD(l,n) addhead(l,n)
#define ADDTAIL(l,n) addtail(l,n)
#define REMOVE(n) rem(n)
static void*GetHead(struct MinList*l){return l->head->succ?l->head:NULL;}
static void*GetSucc(void*p){struct MinNode*n=p;return n->succ->succ?n->succ:NULL;}
struct GfxBase { struct MinList ChunkPoolList; int regionsem; void *regionpool; };
static struct GfxBase gfx,*GfxBase=&gfx;
#define PrivGBase(b) (b)
static unsigned ordinal,failat,live,locks,denied,hardtrap;
static void *alloc(size_t n){assert(!hardtrap);if(++ordinal==failat){denied++;return NULL;}void*p=calloc(1,n);assert(p);live++;return p;}
static void freemem(void*p){assert(p&&live);live--;free(p);}
static void*AllocMem(IPTR n,ULONG f){(void)f;return alloc(n);}
static void FreeMem(void*p,IPTR n){(void)n;freemem(p);}
static void*AllocPooled(void*p,IPTR n){assert(p);return alloc(n);}
static void FreePooled(void*pool,void*p,IPTR n){assert(pool);(void)n;freemem(p);}
static void ObtainSemaphore(int*s){(void)s;locks++;}
static void ReleaseSemaphore(int*s){(void)s;assert(locks);locks--;}
struct RegionRectangle *_NewRegionRectangle(struct RegionRectangle **p,struct GfxBase*b);
void _DisposeRegionRectangleList(struct RegionRectangle*p,struct GfxBase*b);
BOOL _LinkRegionRectangleList(struct RegionRectangle *src,struct RegionRectangle **dst,struct GfxBase*b);
struct Layer_Info;
struct Layer { struct Rectangle bounds; WORD Width,Height; UWORD Flags;
 struct Region *shape,*visibleshape,*VisibleRegion,*DamageList,*shaperegion;
 void *ClipRect,*_cliprects,*shapehook; struct Layer *parent,*front; struct Layer_Info *LayerInfo; };
struct Layer_Info { struct Layer *check_lp; };
struct LayersBase { int unused; };
#define IL(l) (l)
#define LAYERUPDATING 2
#define IS_VISIBLE(l) ((l)->Flags&1)
'''
main=r'''
#define N 8
static int point(struct Region*r,int x,int y){
 for(struct RegionRectangle*q=r->RegionRectangle;q;q=q->Next)
  if(x>=r->bounds.MinX+q->bounds.MinX&&x<=r->bounds.MinX+q->bounds.MaxX&&
   y>=r->bounds.MinY+q->bounds.MinY&&y<=r->bounds.MinY+q->bounds.MaxY)return 1;
 return 0;
}
static uint64_t hashregion(struct Region*r){uint64_t h=1;
 for(int y=-4;y<34;y++)for(int x=-4;x<34;x++)h=h*33+point(r,x,y);return h;}
static struct Region*rect(int x,int y,int w,int h){struct Region*r=NewRegion();assert(r);
 struct Rectangle b={x,y,x+w-1,y+h-1};assert(OrRectRegion(r,&b));return r;}
static void initlayers(struct Layer*l,struct Layer_Info*lis){
 memset(l,0,sizeof(*l)*N);memset(lis,0,sizeof(*lis)*2);lis[0].check_lp=&l[0];lis[1].check_lp=&l[6];
 for(int i=0;i<N;i++){
  int root=i==0||i==6;
  l[i].bounds=(struct Rectangle){root?0:(WORD)(i+1),root?0:(WORD)i,root?31:(WORD)(i+13),root?31:(WORD)(i+11)};
  l[i].Width=l[i].bounds.MaxX-l[i].bounds.MinX+1;l[i].Height=l[i].bounds.MaxY-l[i].bounds.MinY+1;
  l[i].Flags=root?0:1;l[i].LayerInfo=&lis[i>=6];
  l[i].shape=rect(l[i].bounds.MinX,l[i].bounds.MinY,l[i].Width,l[i].Height);
  l[i].visibleshape=rect(l[i].bounds.MinX,l[i].bounds.MinY,l[i].Width,l[i].Height);
  l[i].VisibleRegion=rect(0,0,32,32);l[i].DamageList=rect(-2,-2,10,10);
  if(i!=0&&i!=6)l[i].front=&l[i-1];
 }
 /* physical child is clipped to physical parent; private stack remains distinct */
 l[4].parent=&l[2];l[3].Flags=0;
 l[2].shaperegion=rect(0,0,5,8);
}
static void dispose_layers(struct Layer*l){for(int i=0;i<N;i++){
 DisposeRegion(l[i].shape);DisposeRegion(l[i].visibleshape);DisposeRegion(l[i].VisibleRegion);DisposeRegion(l[i].DamageList);
 if(l[i].shaperegion)DisposeRegion(l[i].shaperegion);}}
static int inrect(struct Rectangle r,int x,int y){return x>=r.MinX&&x<=r.MaxX&&y>=r.MinY&&y<=r.MaxY;}
static int shapeoracle(struct PreparedLayer*p,int i,int x,int y,int parent){
 struct PreparedLayerFields*n=&p[i].next;int yes=inrect(n->bounds,x,y);
 if(n->customshape)yes&=point(n->customshape,x-n->bounds.MinX,y-n->bounds.MinY);
 if(parent&&p[i].layer->parent){int j=p[i].layer->parent-p[0].layer;yes&=shapeoracle(p,j,x,y,1);}return yes;
}
static void success(struct PreparedLayer*p){
 for(int i=0;i<N;i++)for(int y=-4;y<34;y++)for(int x=-4;x<34;x++){
  int visible=1;for(struct Layer*f=p[i].layer->front;f;f=f->front)
   if(IS_VISIBLE(f)&&shapeoracle(p,(int)(f-p[0].layer),x,y,1))visible=0;
  visible&=IS_VISIBLE(p[i].layer)!=0;visible&=x>=0&&x<=31&&y>=0&&y<=31;
  assert(point(p[i].next.shape,x,y)==shapeoracle(p,i,x,y,0));
  assert(point(p[i].next.visibleshape,x,y)==shapeoracle(p,i,x,y,1));
  assert(point(p[i].next.visible,x,y)==visible);
  assert(point(p[i].next.damage,x,y)==(x>=0&&x<=7&&y>=0&&y<=7&&x<p[i].next.width&&y<p[i].next.height));
 }
}
int main(void){
 struct LayersBase base={0};unsigned attempts=0,failures=0;
 for(unsigned occupancy=0;occupancy<SIZECHUNKBUF;occupancy++){
 for(unsigned pass=0;pass<2;pass++){
  unsigned limit=pass?attempts:0;
  for(unsigned f=pass?1:0;f<=limit;f++){
   assert(!live);NEWLIST(&gfx.ChunkPoolList);gfx.regionpool=(void*)1;ordinal=failat=denied=0;
   struct Layer l[N],before[N];struct Layer_Info lis[2];initlayers(l,lis);
   struct RegionRectangleExtChunk *ballast[SIZECHUNKBUF];
   for(unsigned k=0;k<occupancy;k++){ballast[k]=__NewRegionRectangleExtChunk(GfxBase);assert(ballast[k]);}
   memcpy(before,l,sizeof l);uint64_t hashes[N][5];for(int i=0;i<N;i++){
    hashes[i][0]=hashregion(l[i].shape);hashes[i][1]=hashregion(l[i].visibleshape);
    hashes[i][2]=hashregion(l[i].VisibleRegion);hashes[i][3]=hashregion(l[i].DamageList);
    hashes[i][4]=l[i].shaperegion?hashregion(l[i].shaperegion):0;
   }
   unsigned baseline=live;struct PreparedLayer p[N]={0};for(int i=0;i<N;i++)p[i].layer=&l[i];
   struct LayerGeometryRequest req[]={{&l[1],{4,3,19,17},NULL,0,NULL},{&l[4],{5,4,18,16},NULL,0,NULL},{&l[7],{2,2,15,14},NULL,0,NULL}};
   ordinal=0;failat=f;
   BOOL ok=PrepareLayerRegions(p,N,req,3,&base);
   if(!pass){assert(ok);attempts=ordinal;success(p);
    hardtrap=1;InstallPreparedLayerFields(p,N,FALSE);
    for(int i=0;i<N;i++)assert(l[i].shape==p[i].next.shape&&l[i].Width==p[i].next.width);
    InstallPreparedLayerFields(p,N,TRUE);hardtrap=0;
   }else{assert(!ok&&denied==1);failures++;}
   assert(!memcmp(before,l,sizeof l));
   for(int i=0;i<N;i++){
    assert(hashes[i][0]==hashregion(l[i].shape)&&hashes[i][1]==hashregion(l[i].visibleshape));
    assert(hashes[i][2]==hashregion(l[i].VisibleRegion)&&hashes[i][3]==hashregion(l[i].DamageList));
    if(l[i].shaperegion)assert(hashes[i][4]==hashregion(l[i].shaperegion));
   }
   failat=0;ReleasePreparedLayerRegions(p,N,FALSE,&base);assert(live==baseline);
   /* Refuse active updates and unresolved changed hooks without mutations. */
   if(!pass){
    memset(p,0,sizeof p);for(int i=0;i<N;i++)p[i].layer=&l[i];l[1].Flags|=LAYERUPDATING;
    assert(!PrepareLayerRegions(p,N,req,3,&base));ReleasePreparedLayerRegions(p,N,FALSE,&base);l[1].Flags&=~LAYERUPDATING;
    memset(p,0,sizeof p);for(int i=0;i<N;i++)p[i].layer=&l[i];l[1].shapehook=(void*)1;
    assert(!PrepareLayerRegions(p,N,req,3,&base));ReleasePreparedLayerRegions(p,N,FALSE,&base);
    memset(p,0,sizeof p);for(int i=0;i<N;i++)p[i].layer=&l[i];req[0].flags=LGT_RESOLVED_SHAPE;req[0].resolved_hook=l[1].shapehook;
    assert(PrepareLayerRegions(p,N,req,3,&base));ReleasePreparedLayerRegions(p,N,FALSE,&base);
    req[0].flags=0;req[0].resolved_hook=NULL;l[1].shapehook=NULL;
    memset(p,0,sizeof p);for(int i=0;i<N;i++)p[i].layer=&l[i];l[2].parent=&l[4];
    assert(!PrepareLayerRegions(p,N,req,3,&base));ReleasePreparedLayerRegions(p,N,FALSE,&base);l[2].parent=NULL;
    assert(live==baseline);
   }
   for(unsigned k=0;k<occupancy;k++)__DisposeRegionRectangleExtChunk(ballast[k],GfxBase);
   dispose_layers(l);assert(!live&&!locks&&!GetHead(&gfx.ChunkPoolList));
  }
 }
 }
 printf("PASS actual region builder: 20 pool occupancies, %u final-case allocations, %u forced failures; originals/pools intact; occlusion/parent/customshape/private stacks oracle\n",attempts,failures);return 0;
}
'''
intregions=strip(read('rom/graphics/intregions.c'))
intheader=strip(read('rom/graphics/intregions.h'))
public=strip(read('compiler/include/graphics/layertransaction.h'))
prepared=strip(read('rom/hyperlayers/preparedgeometry.h'))
builder=strip(read('rom/hyperlayers/preparedregions.c'))
mutations={'unchecked-shape-union':builder.replace('if (!OrRectRegion(next->shape, &local)) return FALSE;', 'OrRectRegion(next->shape, &local);'),
 'live-damage-mutation':builder.replace('capture_fields(layer, &layers[i].old);','ClearRegion(layer->DamageList); capture_fields(layer, &layers[i].old);'),
 'ignore-parent':builder.replace('if (entry->layer->parent)','if (FALSE && entry->layer->parent)')}
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp)
 for name,body in [('production',builder),*mutations.items()]:
  c=d/f'{name}.c';exe=d/name;c.write_text(prefix+chunks+protos+intheader+intregions+bodies+public+prepared+body+main)
  result=subprocess.run(['clang','-std=gnu11','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(exe)],capture_output=True,text=True)
  if result.returncode:raise RuntimeError(result.stderr)
  result=subprocess.run([str(exe)],capture_output=True,text=True)
  if name=='production':
   if result.returncode:raise RuntimeError(result.stdout+result.stderr)
   print(result.stdout.strip())
  elif not result.returncode:raise RuntimeError(f'mutation escaped: {name}')
  else:print(f'REJECT {name}')
print('SOURCE',ROOT)
for rel in ['rom/hyperlayers/preparedregions.c','rom/hyperlayers/preparedgeometry.h','compiler/include/graphics/layertransaction.h','rom/graphics/intregions.c','rom/graphics/intregions.h','rom/graphics/graphics_intern.h'] + [f'rom/graphics/{name.lower()}.c' for _,name,_ in ops]:
 print(hashlib.sha256((ROOT/rel).read_bytes()).hexdigest(),rel)
