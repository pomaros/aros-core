#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual Layers batch + real region/pixel backend, hostile allocation sweep."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path)
args = parser.parse_args()
root = (args.source or Path(__file__).resolve().parents[4]).resolve()
region_test = root/'developer/debug/test/intuition/preparedregions_host.py'
namespace = {'__name__': 'region_fixture', '__file__': str(region_test)}
old_argv = sys.argv
sys.argv = [str(region_test), '--source', str(root)]
exec(region_test.read_text().split('mutations=')[0], namespace)
sys.argv = old_argv
read, strip = namespace['read'], namespace['strip']
prefix = namespace['prefix'].split('struct Layer_Info;')[0]
prefix += r'''
#include <setjmp.h>
static jmp_buf cut_jump;static unsigned cutting,cut_at,cut_counter;
static void TransactionCut(void){if(cutting&&++cut_counter==cut_at)longjmp(cut_jump,1);}
typedef void VOID;typedef void *APTR;typedef void OOP_Object;typedef void *OOP_Msg;typedef const char *CONST_STRPTR;
#define MEMF_PUBLIC 0
#define MEMF_CLEAR 1
#define BMF_CLEAR 1
#define BMA_DEPTH 1
#define ALIGN_CLIPRECT 16
#define ALIGN_OFFSET(x) ((x)&15)
#define LAYERSIMPLE 1
#define LAYERSMART 2
#define LAYERSUPER 4
#define LAYERUPDATING 16
#define LAYERREFRESH 128
#define LAYERS_BACKFILL ((struct Hook*)0)
#define LAYERS_NOBACKFILL ((struct Hook*)1)
#define CR_SHAREDBITMAP 4
struct Layer;struct Layer_Info;struct BitMap;
struct RastPort { struct Layer *Layer;struct BitMap *BitMap; };
struct ClipRect {struct ClipRect *Next,*prev;struct Layer *lobs;struct BitMap *BitMap;
 struct Rectangle bounds;void *_p1,*_p2;LONG reserved,Flags;};
struct Layer {struct Layer *front,*back;struct ClipRect *ClipRect;struct RastPort *rp;
 struct Rectangle bounds;struct Layer *parent;UWORD Flags;struct BitMap *SuperBitMap;
 WORD Scroll_X,Scroll_Y;struct ClipRect *_cliprects;struct Layer_Info *LayerInfo;
 struct Region *VisibleRegion,*ClipRegion;WORD Width,Height;
 struct Region *shape,*shaperegion,*visibleshape,*DamageList;UBYTE visible;void *shapehook;struct Hook*BackFill;};
struct Layer_Info {struct Layer *top_layer,*check_lp;struct Layer_Info *root;};
struct LayersBase {void *lb_ClipRectPool;};
static struct LayersBase base={(void*)1};
#define IL(l) (l)
#define IS_VISIBLE(l) ((l)->visible)
#define IS_SIMPLEREFRESH(l) ((l)->Flags&LAYERSIMPLE)
#define IS_SMARTREFRESH(l) (((l)->Flags&(LAYERSMART|LAYERSUPER))==LAYERSMART)
#define IS_SUPERREFRESH(l) ((l)->Flags&LAYERSUPER)
#define CR_BMOFFX(c) ((WORD)(IPTR)(c)->_p1)
#define CR_BMOFFY(c) ((WORD)(IPTR)(c)->_p2)
#define CR_SETBMOFF(c,x,y) do{(c)->_p1=(void*)(IPTR)(WORD)(x);(c)->_p2=(void*)(IPTR)(WORD)(y);}while(0)
static unsigned lilocks,llocks,opens,maps;
static unsigned backfills;
static void _CallLayerHook(struct Hook*,struct RastPort*,struct Layer*,struct Rectangle*,WORD,WORD,struct LayersBase*);

static void LockLayerInfo(struct Layer_Info*l){(void)l;assert(!hardtrap);lilocks++;}
static void UnlockLayerInfo(struct Layer_Info*l){(void)l;assert(!hardtrap&&lilocks);lilocks--;}
static void LockLayer(int zero,struct Layer*l){(void)zero;(void)l;assert(!hardtrap);llocks++;}
static void UnlockLayer(struct Layer*l){(void)l;assert(!hardtrap&&llocks);llocks--;}
static struct Layer_Info*GetLayerInfoCompositionRoot(struct Layer_Info*l){/* as Layers: an ordinary stack is its own root */return l->root?l->root:l;}
static void*AllocVec(ULONG n,ULONG f){(void)f;return alloc(n);}
static void FreeVec(void*p){assert(!hardtrap);if(p)freemem(p);}
struct Library {int unused;};static struct Library library;
static struct Library*OpenLibrary(const char*n,int v){(void)n;(void)v;assert(!hardtrap);if(++ordinal==failat){denied++;return NULL;}opens++;return &library;}
static void CloseLibrary(struct Library*l){assert(!hardtrap&&l&&opens);opens--;}
struct BitMap {ULONG width,height,stride,Depth;UBYTE *data,*scanout;};
static void _CallLayerHook(struct Hook*h,struct RastPort*p,struct Layer*l,struct Rectangle*r,WORD x,WORD y,struct LayersBase*b){
 (void)h;(void)b;assert(!hardtrap&&!maps&&!p->Layer);backfills++;
 assert(r->MinX>=0&&r->MinY>=0&&r->MaxX<(LONG)p->BitMap->width&&r->MaxY<(LONG)p->BitMap->height);
 assert(x>=0&&y>=0);if(l!=l->LayerInfo->check_lp)assert(x<l->Width&&y<l->Height);
 for(int dy=r->MinY;dy<=r->MaxY;dy++)for(int dx=r->MinX;dx<=r->MaxX;dx++){ULONG pixel=0x8a6c2e;
  memcpy(p->BitMap->data+dy*p->BitMap->stride+dx*4,&pixel,4);
  if(p->BitMap->scanout)memcpy(p->BitMap->scanout+dy*p->BitMap->stride+dx*4,&pixel,4);}
}
static struct BitMap *bitmaps[512];static ULONG bitmap_count;
static struct BitMap*AllocBitMap(ULONG w,ULONG h,ULONG d,ULONG flags,struct BitMap*friend){
 (void)flags;(void)friend;struct BitMap*b=AllocVec(sizeof(*b),1);if(!b)return NULL;
 b->width=w;b->height=h;b->stride=w*4+16;b->Depth=d;b->data=AllocVec(b->stride*h,1);
 if(!b->data){FreeVec(b);return NULL;}assert(bitmap_count<512);bitmaps[bitmap_count++]=b;return b;
}
static void FreeBitMap(struct BitMap*b){ULONG i;for(i=0;i<bitmap_count;i++)if(bitmaps[i]==b)break;
 assert(i<bitmap_count);bitmaps[i]=bitmaps[--bitmap_count];FreeVec(b->scanout);FreeVec(b->data);FreeVec(b);}
static ULONG GetBitMapAttr(struct BitMap*b,int a){assert(a==BMA_DEPTH);return b->Depth;}
#define IS_HIDD_BM(b) ((b)!=NULL)
#define HIDD_BM_OBJ(b) ((OOP_Object*)(b))
#define HIDD_BM_COLMAP(b) ((OOP_Object*)NULL)
#define IID_Hidd_ColorMap "colormap"
#define moHidd_ColorMap_GetPixel 4
struct pHidd_ColorMap_GetPixel{ULONG id,pen;};
#define IID_Hidd_BitMap "bitmap"
#define moHidd_BitMap_PreparePixels 1
#define moHidd_BitMap_ReleasePixels 2
#define moHidd_BitMap_RenderPreparedCursor 3
static ULONG OOP_GetMethodID(const char*s,ULONG id){(void)s;assert(!hardtrap);return id;}
#define AROS_LHA(t,n,r) t n
#define AROS_LH1(t,n,a,b,bn,...) t n(a,b bn)
#define AROS_LH2(t,n,a,c,b,bn,...) t n(a,c,b bn)
#define AROS_LH3(t,n,a,c,d,b,bn,...) t n(a,c,d,b bn)
#define AROS_LH4(t,n,a,c,d,e,b,bn,...) t n(a,c,d,e,b bn)
#define AROS_LH5(t,n,a,c,d,e,f,b,bn,...) t n(a,c,d,e,f,b bn)
#define AROS_LH8(t,n,a,c,d,e,f,g,h,i,b,bn,...) t n(a,c,d,e,f,g,h,i,b bn)
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
'''
protos = namespace['protos']+'BOOL OrRegionRegion(struct Region *R1,struct Region *R2);\n'
text = read('rom/graphics/orregionregion.c')
bodies = namespace['bodies']+'BOOL OrRegionRegion(struct Region *R1,struct Region *R2){'+text.split('AROS_LIBFUNC_INIT',1)[1].split('AROS_LIBFUNC_EXIT',1)[0]+'}\n'
map_mock = r'''
struct pHidd_BitMap_PreparePixels{ULONG id;struct HIDD_PixelMap*map;};
struct pHidd_BitMap_ReleasePixels{ULONG id;struct HIDD_PixelMap*map;};
struct pHidd_BitMap_RenderPreparedCursor{ULONG id;struct HIDD_PixelMap*map;UBYTE*stage;struct HIDD_CursorState *proposed;};
static ULONG OOP_DoMethod(OOP_Object*object,OOP_Msg msg){
 struct BitMap*b=(void*)object;struct pHidd_BitMap_PreparePixels*m=msg;assert(!hardtrap);
 if(m->id==2){assert(maps);maps--;return 0;}
 if(++ordinal==failat){denied++;return 0;}
 assert(m->id==1);memset(m->map,0,sizeof(*m->map));m->map->pixels=b->data;
 m->map->width=b->width;m->map->height=b->height;m->map->stride=b->stride;m->map->size=b->stride*b->height;
 m->map->red=0xff0000;m->map->green=0xff00;m->map->blue=0xff;m->map->presentation=b->scanout;
 m->map->presentationStride=b->stride;m->map->presentationSize=b->stride*b->height;maps++;return 1;
}
struct PreparedPixels;void ReleasePreparedPixels(struct PreparedPixels*);
'''
main = r'''
static struct LayerGeometryRefusal refusal;
#define N 8
static struct Layer l[N];static struct RastPort rp[N];static struct Layer_Info info[2];
static struct Region*rect(int x,int y,int w,int h){struct Region*r=NewRegion();assert(r);struct Rectangle b={x,y,x+w-1,y+h-1};assert(OrRectRegion(r,&b));return r;}
static struct PreparedPixel color(unsigned n){return(struct PreparedPixel){(n+1)*17<<8,n*23<<8,7<<8,0xffff};}
static void initialize(void){
 memset(l,0,sizeof l);memset(info,0,sizeof info);memset(rp,0,sizeof rp);
 NEWLIST(&gfx.ChunkPoolList);gfx.regionpool=(void*)1;
 struct BitMap*b[2]={AllocBitMap(64,48,32,0,NULL),AllocBitMap(64,48,32,0,NULL)};assert(b[0]&&b[1]);
 b[0]->scanout=AllocVec(b[0]->stride*48,0);assert(b[0]->scanout);memset(b[0]->scanout,0x5a,b[0]->stride*48);
 int order[2][4]={{1,2,3,0},{7,6,5,4}};
 struct Rectangle bounds[N]={{0,0,63,47},{14,12,33,25},{12,10,35,27},{4,4,19,15},{0,0,63,47},{5,6,24,20},{7,8,22,18},{8,9,13,13}};
 for(int i=0;i<N;i++){
  int s=i>=4;l[i].LayerInfo=&info[s];l[i].bounds=bounds[i];l[i].Width=bounds[i].MaxX-bounds[i].MinX+1;l[i].Height=bounds[i].MaxY-bounds[i].MinY+1;
  l[i].visible=i!=0&&i!=4;l[i].Flags=(i==1||i==6)?LAYERSMART:LAYERSIMPLE;
  l[i].rp=&rp[i];rp[i].Layer=&l[i];rp[i].BitMap=b[s];l[i].shape=rect(bounds[i].MinX,bounds[i].MinY,l[i].Width,l[i].Height);
  l[i].visibleshape=rect(bounds[i].MinX,bounds[i].MinY,l[i].Width,l[i].Height);l[i].VisibleRegion=NewRegion();l[i].DamageList=NewRegion();assert(l[i].VisibleRegion&&l[i].DamageList);
  l[i].parent=(i==0||i==4)?NULL:&l[s?4:0];
 }
 l[1].parent=&l[2];l[6].parent=&l[5];l[7].parent=&l[6];
 l[1].ClipRegion=rect(0,0,12,14);
 for(int s=0;s<2;s++){info[s].check_lp=&l[s?4:0];info[s].top_layer=&l[order[s][0]];
  for(int j=0;j<4;j++){int i=order[s][j];l[i].front=j?&l[order[s][j-1]]:NULL;l[i].back=j<3?&l[order[s][j+1]]:NULL;}}
 info[1].root=&info[0];
 struct LayerGeometryRequest pin={&l[5],l[5].bounds,NULL,0,NULL};
 struct LayerGeometryBatch*t=PrepareLayerGeometryBatch(&info[0],&pin,1,&refusal,&base);assert(t);
 for(int i=1;i<N;i++)if(i!=4){struct PreparedPixel pixels[2048];for(int j=0;j<2048;j++)pixels[j]=color(i);assert(WriteLayerGeometryPixels(t,&l[i],pixels,l[i].Width*sizeof(*pixels),l[i].Height,&base));}
 assert(SealLayerGeometryBatch(t,&base));CommitLayerGeometryBatch(t,&base);ReleaseLayerGeometryBatch(t,&base);assert(!maps&&!opens&&!llocks&&!lilocks);
}
static uint64_t hash(const void*p,size_t n){uint64_t h=1;const UBYTE*b=p;while(n--)h=h*33+*b++;return h;}
struct Snapshot{struct Layer layers[N];struct BitMap*bm[512];uint64_t pixels[512],scanout[512];ULONG count;unsigned allocations;};
static int inregion(struct Region*r,int x,int y){for(struct RegionRectangle*q=r->RegionRectangle;q;q=q->Next)if(x>=r->bounds.MinX+q->bounds.MinX&&x<=r->bounds.MinX+q->bounds.MaxX&&y>=r->bounds.MinY+q->bounds.MinY&&y<=r->bounds.MinY+q->bounds.MaxY)return 1;return 0;}
static int contains(struct Layer*view,int i,int x,int y){struct Layer*a=&view[i];
 if(x<a->bounds.MinX||x>a->bounds.MaxX||y<a->bounds.MinY||y>a->bounds.MaxY)return 0;
 if(a->shaperegion&&!inregion(a->shaperegion,x-a->bounds.MinX,y-a->bounds.MinY))return 0;
 if(a->parent)return contains(view,(int)(a->parent-l),x,y);return 1;}
static int owner(struct Layer*view,int stack,int x,int y){for(struct Layer*a=info[stack].top_layer;a;){int i=(int)(a-l);if(view[i].visible&&contains(view,i,x,y))return i;a=view[i].back;}return -1;}
static void oracle(struct Snapshot*old,BOOL after){for(int stack=0;stack<2;stack++)for(int y=0;y<48;y++)for(int x=0;x<64;x++){
 int i=owner(l,stack,x,y);ULONG expected=0,got;struct BitMap*b=rp[stack?4:0].BitMap;
 if(i>=0){int sx=x-l[i].bounds.MinX,sy=y-l[i].bounds.MinY;int known=sx<old->layers[i].Width&&sy<old->layers[i].Height;
  if(!IS_SMARTREFRESH(&l[i]))known=known&&owner(old->layers,stack,old->layers[i].bounds.MinX+sx,old->layers[i].bounds.MinY+sy)==i;
  if(i==3||known){struct PreparedPixel c=color(i==3?12:(unsigned)i);expected=((ULONG)c.red>>8)<<16|((ULONG)c.green>>8)<<8|((ULONG)c.blue>>8);}}
 if(after&&stack==0){int known=i>=0&&x-l[i].bounds.MinX<old->layers[i].Width&&y-l[i].bounds.MinY<old->layers[i].Height;
 if(i==2)known=known&&owner(old->layers,stack,old->layers[i].bounds.MinX+x-l[i].bounds.MinX,old->layers[i].bounds.MinY+y-l[i].bounds.MinY)==i;
 if((i<0&&owner(old->layers,stack,x,y)>=0)||(i==2&&!known))expected=0x8a6c2e;}
 memcpy(&got,b->data+y*b->stride+x*4,4);if((got&0xffffff)!=expected){fprintf(stderr,"oracle stack%d at%d,%d owner%d got%06x expected%06x\n",stack,x,y,i,got&0xffffff,expected);abort();}
}}
static void snapshot(struct Snapshot*s){memcpy(s->layers,l,sizeof l);s->count=bitmap_count;s->allocations=live;
 for(ULONG i=0;i<bitmap_count;i++){s->bm[i]=bitmaps[i];s->pixels[i]=hash(bitmaps[i]->data,bitmaps[i]->stride*bitmaps[i]->height);s->scanout[i]=bitmaps[i]->scanout?hash(bitmaps[i]->scanout,bitmaps[i]->stride*bitmaps[i]->height):0;}}
static void unchanged(struct Snapshot*s){assert(!memcmp(s->layers,l,sizeof l));assert(bitmap_count==s->count&&live==s->allocations);
 for(ULONG i=0;i<s->count;i++){struct BitMap*b=s->bm[i];assert(hash(b->data,b->stride*b->height)==s->pixels[i]);if(b->scanout)assert(hash(b->scanout,b->stride*b->height)==s->scanout[i]);}
 assert(!maps&&!opens&&!llocks&&!lilocks);}
static struct LayerGeometryBatch*candidate(void){
 struct LayerGeometryRequest r[]={
 {&l[1],{32,12,51,25},NULL,0,NULL},{&l[2],{30,10,53,27},NULL,0,NULL},
 {&l[3],{8,8,51,41},NULL,LGT_REPLACE_SHAPE,NULL},
 {&l[5],{2,3,23,19},NULL,0,NULL},{&l[6],{4,5,21,17},NULL,0,NULL},{&l[7],{5,6,10,10},NULL,0,NULL}};
 struct LayerGeometryBatch*t=PrepareLayerGeometryBatch(&info[0],r,6,&refusal,&base);if(!t)return NULL;
 struct PreparedPixel output[44*34];for(unsigned i=0;i<44*34;i++)output[i]=color(12);
 assert(!WriteLayerGeometryPixels(t,&l[3],output,44*sizeof(*output),33,&base));
 assert(refusal.lgr_Check==LGR_SOURCE_SIZE&&refusal.lgr_Layer==&l[3]&&refusal.lgr_Value[1]==33&&refusal.lgr_Value[3]==34&&!(refusal.lgr_Flags&(LGRF_ROOT|LGRF_PRIVATE)));
 assert(!WriteLayerGeometryPixels(t,&l[0],output,44*sizeof(*output),34,&base));
 assert(refusal.lgr_Check==LGR_ROOT_CANVAS&&refusal.lgr_Layer==&l[0]&&refusal.lgr_Flags==LGRF_ROOT);
 assert(!WriteLayerGeometryPixels(t,&l[6],output,44*sizeof(*output),1,&base));
 assert(refusal.lgr_Check==LGR_SOURCE_SIZE&&refusal.lgr_Layer==&l[6]&&refusal.lgr_Flags==LGRF_PRIVATE);
 if(!WriteLayerGeometryPixels(t,&l[3],output,44*sizeof(*output),34,&base)||!SealLayerGeometryBatch(t,&base)){ReleaseLayerGeometryBatch(t,&base);return NULL;}
 return t;
}
static void dispose(void){
 for(int i=0;i<N;i++){free_clips(l[i].ClipRect,&l[i],&base);free_clips(l[i]._cliprects,&l[i],&base);DisposeRegion(l[i].shape);DisposeRegion(l[i].visibleshape);DisposeRegion(l[i].VisibleRegion);DisposeRegion(l[i].DamageList);if(l[i].ClipRegion)DisposeRegion(l[i].ClipRegion);}
 FreeBitMap(rp[0].BitMap);FreeBitMap(rp[4].BitMap);assert(!live&&!bitmap_count&&!maps&&!opens&&!locks&&!lilocks&&!llocks&&!GetHead(&gfx.ChunkPoolList));
}
static unsigned seen[64];
int main(void){unsigned failures=0,attempts=0,cutpoints=0;
 for(unsigned occupancy=0;occupancy<20;occupancy++)for(unsigned f=0;;f++){
  failat=0;initialize();struct RegionRectangleExtChunk*ballast[20];for(unsigned b=0;b<occupancy;b++){ballast[b]=__NewRegionRectangleExtChunk(GfxBase);assert(ballast[b]);}
  struct Snapshot before;snapshot(&before);ordinal=denied=0;failat=f;
  struct LayerGeometryBatch*t=candidate();unsigned used=ordinal;failat=0;
  if(!f){assert(t);attempts=used;hardtrap=1;cutting=1;cut_at=~0U;cut_counter=0;
   assert(!setjmp(cut_jump));CommitLayerGeometryBatch(t,&base);unsigned cuts=cut_counter;cutpoints+=cuts;cutting=0;
   assert(l[3].Width==44&&l[5].Width==22);oracle(&before,FALSE);RollbackLayerGeometryBatch(t,&base);hardtrap=0;ReleaseLayerGeometryBatch(t,&base);unchanged(&before);
   for(unsigned cut=1;cut<=cuts;cut++){
    t=candidate();assert(t);cut_at=cut;cut_counter=0;cutting=1;hardtrap=1;
    if(!setjmp(cut_jump)){CommitLayerGeometryBatch(t,&base);assert(!"cutpoint not reached");}
    cutting=0;RollbackLayerGeometryBatch(t,&base);hardtrap=0;ReleaseLayerGeometryBatch(t,&base);unchanged(&before);
   }
   /* Finish is explicitly outside the no-hook transaction. Maps must all be
    * released before any custom hook, and a second Finish is inert. */
   l[0].BackFill=(struct Hook*)2;l[2].BackFill=(struct Hook*)2;
   t=candidate();assert(t);hardtrap=1;CommitLayerGeometryBatch(t,&base);hardtrap=0;
   unsigned bf=backfills;FinishLayerGeometryBatch(t,&base);assert(backfills>bf&&!maps);oracle(&before,TRUE);
   bf=backfills;FinishLayerGeometryBatch(t,&base);assert(backfills==bf);
   RollbackLayerGeometryBatch(t,&base);assert(l[3].Width==44);
   ReleaseLayerGeometryBatch(t,&base);
  }
  else {assert(!t&&denied==1);unchanged(&before);failures++;
   /* Every refusing prepare, write or seal names its check. */
   if(refusal.lgr_Check==LGR_NONE){fprintf(stderr,"unrecorded refusal at forced failure %u\n",f);abort();}
   assert(refusal.lgr_Check<64);seen[refusal.lgr_Check]++;
   if(refusal.lgr_Check==LGR_PIXELS)assert(refusal.lgr_Pixels&&refusal.lgr_Lease);}
  for(unsigned b=0;b<occupancy;b++)__DisposeRegionRectangleExtChunk(ballast[b],GfxBase);
  dispose();if(f==attempts)break;
 }
 printf("PASS actual Layers batch: 20 region-pool occupancies, %u final prepare points, %u forced failures; %u statement cutpoints; mixed stacks/GZZ/hidden/clips/requester; noalloc commitrollback; raw raster+scanout restore\n",attempts,failures,cutpoints);
 printf("PASS every forced refusal recorded; checks seen:");for(unsigned i=0;i<64;i++)if(seen[i])printf(" %u:%u",i,seen[i]);printf("\n");
 assert(seen[LGR_NO_MEMORY]&&seen[LGR_PIXELS]);return 0;
}
'''
protos += 'BOOL ClearRectRegion(struct Region *Reg, const struct Rectangle *Rect);\n'
clearrect = read('rom/graphics/clearrectregion.c')
bodies += 'BOOL ClearRectRegion(struct Region *Reg, const struct Rectangle *Rect){'+clearrect.split('AROS_LIBFUNC_INIT',1)[1].split('AROS_LIBFUNC_EXIT',1)[0]+'}\n'
source = prefix+namespace['chunks']+protos+namespace['intheader']+namespace['intregions']+bodies
source += strip(read('compiler/include/graphics/layertransaction.h'))
source += strip(read('rom/hidds/gfx/include/preparedpixels.h'))+map_mock
source += strip(read('rom/hyperlayers/preparedpixels.h'))+strip(read('rom/hyperlayers/preparedpixels.c'))
source += strip(read('rom/hyperlayers/preparedgeometry.h'))
source += strip(read('rom/hyperlayers/preparedregions.c'))+strip(read('rom/hyperlayers/preparedclips.c'))
import re
source=re.sub(r'(layer->[A-Za-z_]+ = f->[A-Za-z_]+;)',r'\1 TransactionCut();',source)
batch = strip(read('rom/hyperlayers/preparedbatch.c'))
batch=batch.replace('for (i = batch->stack_count; i > 0; --i) CommitPreparedPixels(batch->stacks[i - 1].pixels);',
                    'for (i = batch->stack_count; i > 0; --i) { CommitPreparedPixels(batch->stacks[i - 1].pixels); TransactionCut(); }')
batch=batch.replace('batch->state = BATCH_COMMITTED;','batch->state = BATCH_COMMITTED; TransactionCut();')
mutants = {'missing-source-height': batch.replace('height < (ULONG)entry->next.height ||', ''),
           'rollback-without-pixels': batch.replace('RestorePreparedPixels(batch->stacks[i - 1].pixels);', '(void)0;'),
           'unsigned-chunk-index': batch,
           'move-only-client-clip': batch.replace('entry->next.regular ? entry->next.regular : entry->next.clips','entry->next.clips'),
           'content-unrecorded': batch.replace('''    if (!entry->content)
        return batch_refuse(batch, LGR_NO_MEMORY, layer, array_bytes(rows, entry->stride),
            LGR_MEM_CONTENT, next->width, rows);''','''    if (!entry->content) return FALSE;'''),
           'stack-lease-unrecorded': batch.replace('PreparePixels(layer->rp->BitMap, TRUE, refusal, LGR_PIXELS_STACK)','PreparePixels(layer->rp->BitMap, TRUE, NULL, LGR_PIXELS_STACK)'),
           'source-size-unrecorded': batch.replace('''                return batch_refuse(batch, LGR_SOURCE_SIZE, layer, stride, height,
                    entry->stride, entry->next.height);''','''                return FALSE;'''),
           'private-flag-lost': batch.replace('if (layer->LayerInfo != root) r->lgr_Flags |= LGRF_PRIVATE;','(void)root;')}
with tempfile.TemporaryDirectory(prefix='layers-batch-') as temp:
    temp = Path(temp)
    for name, body in [('production', batch), *mutants.items()]:
        c, exe = temp/(name+'.c'), temp/name
        combined=source+body+main
        if name=='unsigned-chunk-index': combined=combined.replace('-(LONG)Counter(x)','-Counter(x)')
        c.write_text(combined)
        result = subprocess.run(['clang','-std=gnu11','-O1','-g','-fsanitize=address,undefined',
                                 '-fno-sanitize-recover=all',str(c),'-o',str(exe)], capture_output=True,text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        result = subprocess.run([str(exe)],capture_output=True,text=True)
        if name == 'production':
            if result.returncode:
                import re
                match = re.search(r'production.c:(\d+):', result.stderr)
                context = ''
                if match:
                    line = int(match[1])
                    context = '\n'.join(f'{i+1}: {s}' for i,s in enumerate(c.read_text().splitlines())
                                        if line-4 <= i+1 <= line+4)
                raise RuntimeError(result.stdout+result.stderr+'\n'+context)
            print(result.stdout,end='')
        elif result.returncode == 0:
            raise AssertionError('negative control survived: '+name)
        else:
            print('REJECT',name)
print('prepared Layers batch ASan/UBSan result=PASS')
