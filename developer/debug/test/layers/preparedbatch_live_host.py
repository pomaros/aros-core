#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual batch on independently assembled old live clips/backing, not batch output."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=Path);a=p.parse_args()
root=(a.source or Path(__file__).resolve().parents[4]).resolve()
test=root/'developer/debug/test/layers/preparedbatch_host.py'
ns={'__name__':'batch_dependencies','__file__':str(test)}
saved=sys.argv;sys.argv=[str(test),'--source',str(root)]
exec(test.read_text().split('mutants =')[0],ns);sys.argv=saved
source=ns['source'].replace('ULONG width,height,stride,Depth;UBYTE *data,*scanout;', 'ULONG width,height,stride,Depth,pen0;UBYTE *data,*scanout;')
source=source.replace('#define HIDD_BM_COLMAP(b) ((OOP_Object*)NULL)','#define HIDD_BM_COLMAP(b) ((OOP_Object*)(b))')
source=source.replace('if(m->id==2){assert(maps);maps--;return 0;}','if(m->id==4)return b->pen0;\n if(m->id==2){assert(maps);maps--;return 0;}')
# Construct live lists with the actual ordinary Layers constructor helpers,
# including its free ClipRect cache. Region algebra/allocator stay production.
source=source.replace('struct ClipRect *_cliprects;', 'struct ClipRect *_cliprects,*SuperSaveClipRects;ULONG SuperSaveClipRectCounter;')
for name,signature in [('xorregionregion','BOOL XorRegionRegion(struct Region *R1,struct Region *R2)'),('xorrectregion','BOOL XorRectRegion(struct Region *Reg,struct Rectangle *Rect)')]:
 text=(root/('rom/graphics/'+name+'.c')).read_text()
 source+=signature+'{'+text.split('AROS_LIBFUNC_INIT',1)[1].split('AROS_LIBFUNC_EXIT',1)[0]+'}\n'
source+='#define MAX(a,b) ((a)>(b)?(a):(b))\n#define MIN(a,b) ((a)<(b)?(a):(b))\n'
alib=(root/'compiler/alib/andrectrect.c').read_text()
source+='BOOL AndRectRect(struct Rectangle*rect1,struct Rectangle*rect2,struct Rectangle*intersect){'+alib[alib.index('{\n    WORD MinX'):].split('{',1)[1]+'\n'
source+='#define RECTAREA(r) (((r)->MaxX-(r)->MinX+1)*((r)->MaxY-(r)->MinY+1))\n'
basic=(root/'rom/hyperlayers/basicfuncs.c').read_text()
def actual(signature):
 start=basic.index(signature);brace=basic.index('{',start);depth=1;end=brace+1
 while depth:
  if basic[end]=='{':depth+=1
  elif basic[end]=='}':depth-=1
  end+=1
 return basic[start:end]+'\n'
for signature in ['struct ClipRect * _AllocClipRect(', 'static void _MakeClipRectView(', 'BOOL _AllocClipRectBitMap(', 'BOOL _BackClipRect(', 'struct ClipRect * _CreateClipRectsFromRegion(']:source+=actual(signature)
source=source.replace('m->map->red=0xff0000;m->map->green=0xff00;m->map->blue=0xff;', 'm->map->red=b->Depth==30?0x3ff00000:0xff0000;m->map->green=b->Depth==30?0xffc00:0xff00;m->map->blue=b->Depth==30?0x3ff:0xff;')
batch=ns['batch'].replace('CommitPreparedPixels(batch->layers[i].super_pixels);','{ CommitPreparedPixels(batch->layers[i].super_pixels); TransactionCut(); }')
main=r'''
static struct LayerGeometryRefusal refusal;
static struct Layer ls[3];static struct RastPort ports[3];static struct Layer_Info li;
static struct BitMap *screenbm,*superbm;
static struct Region *region(int x,int y,int w,int h){struct Region*r=NewRegion();assert(r);struct Rectangle b={x,y,x+w-1,y+h-1};assert(OrRectRegion(r,&b));return r;}
static ULONG bg(int x,int y){return 0x203000+x*16+y;}
static ULONG fresh(int x,int y){return 0xa00000+x*256+y;}
static ULONG back(int x,int y){return 0x400000+x*256+y;}
static ULONG stale(int x,int y){return 0x100000+x*256+y;}
static ULONG front(int x,int y){return 0x880000+x*256+y;}
static ULONG oldscreen(int x,int y,BOOL windows){if(windows&&y>=2&&y<=7){if(x>=4&&x<=5)return front(x-4,y-2);if(x>=2&&x<=9)return fresh(x-2,y-2);}return bg(x,y);}
static void put(struct BitMap*b,int x,int y,ULONG v){if(b->Depth==30)v=((v&0xff0000)<<6)|((v&0xff00)<<4)|((v&0xff)<<2);memcpy(b->data+y*b->stride+x*4,&v,4);if(b->scanout)memcpy(b->scanout+y*b->stride+x*4,&v,4);}
static ULONG get(struct BitMap*b,int x,int y){ULONG v;memcpy(&v,b->data+y*b->stride+x*4,4);if(b->Depth==30)return((v>>6)&0xff0000)|((v>>4)&0xff00)|((v>>2)&0xff);return v&0xffffff;}
static void clip(struct Layer*l,int x,int y,int w,int h,BOOL hidden){struct ClipRect*c=AllocPooled(base.lb_ClipRectPool,sizeof(*c));assert(c);memset(c,0,sizeof(*c));c->bounds=(struct Rectangle){x,y,x+w-1,y+h-1};c->lobs=hidden?(void*)1:NULL;c->Next=l->ClipRect;l->ClipRect=c;}
static void init(BOOL windows,BOOL noback,BOOL smart,BOOL ten){
 memset(ls,0,sizeof ls);memset(ports,0,sizeof ports);memset(&li,0,sizeof li);NEWLIST(&gfx.ChunkPoolList);gfx.regionpool=(void*)1;
 screenbm=AllocBitMap(16,12,32,0,NULL);assert(screenbm);screenbm->pen0=0x26486a;screenbm->scanout=AllocVec(screenbm->stride*12,1);assert(screenbm->scanout);
 superbm=windows&&!smart?AllocBitMap(12,10,32,0,NULL):NULL;if(windows&&!smart){assert(superbm);if(ten)superbm->Depth=30;}
 int count=windows?3:1;
 for(int i=0;i<count;i++){
  struct Layer*l=&ls[i];l->LayerInfo=&li;l->rp=&ports[i];ports[i].BitMap=screenbm;ports[i].Layer=l;
  l->bounds=i==0?(struct Rectangle){0,0,15,11}:i==1?(struct Rectangle){2,2,9,7}:(struct Rectangle){4,2,5,7};
  l->Width=l->bounds.MaxX-l->bounds.MinX+1;l->Height=l->bounds.MaxY-l->bounds.MinY+1;l->visible=i!=0;
  l->Flags=i==1&&!smart?LAYERSUPER:LAYERSMART;if(smart&&i==2)l->Flags|=(1U<<11)|(1U<<12); /* protected,input-transparent overlay remains an occluder */l->parent=i?&ls[0]:NULL;l->BackFill=noback?LAYERS_NOBACKFILL:LAYERS_BACKFILL;
  l->shape=region(l->bounds.MinX,l->bounds.MinY,l->Width,l->Height);l->visibleshape=region(l->bounds.MinX,l->bounds.MinY,l->Width,l->Height);
  l->VisibleRegion=NewRegion();l->DamageList=NewRegion();assert(l->VisibleRegion&&l->DamageList);
 }
 li.check_lp=&ls[0];li.top_layer=&ls[count-1];
 for(int i=0;i<count;i++){ls[i].front=i+1<count?&ls[i+1]:NULL;ls[i].back=i?&ls[i-1]:NULL;}
 if(windows){
  ls[1].SuperBitMap=superbm;ls[1].Scroll_X=smart?0:1;ls[1].Scroll_Y=smart?0:2;
  /* Ordinary SUPER policy: hidden clips have no BitMap, visible draws go to
   * the screen; backing is stale until SyncSBitMap. These are NOT generated
   * by PrepareLayerGeometryBatch. */
  struct Rectangle visible={2,2,3,7};assert(OrRectRegion(ls[1].VisibleRegion,&visible));visible=(struct Rectangle){6,2,9,7};assert(OrRectRegion(ls[1].VisibleRegion,&visible));
  visible=ls[2].bounds;assert(OrRectRegion(ls[2].VisibleRegion,&visible));
  for(int i=1;i<=2;i++){
   struct ClipRect*cached=AllocPooled(base.lb_ClipRectPool,sizeof(*cached));assert(cached);memset(cached,0,sizeof(*cached));cached->Flags=123;ls[i].SuperSaveClipRects=cached;ls[i].SuperSaveClipRectCounter=1;
   struct Region*visiblecopy=NewRegion();assert(visiblecopy&&SetRegion(ls[i].VisibleRegion,visiblecopy));
   ls[i].ClipRect=_CreateClipRectsFromRegion(visiblecopy,&ls[i],FALSE,NULL,0,NULL,&base);DisposeRegion(visiblecopy);
   assert(ls[i].ClipRect&&!ls[i].SuperSaveClipRects&&!ls[i].SuperSaveClipRectCounter);
  }
  /* A separate client clip retains regular clips as the ordinary owner. */
  ls[1]._cliprects=ls[1].ClipRect;ls[1].ClipRegion=region(0,0,6,6);
  struct Region*domain=region(2,2,6,6),*selected=NewRegion();assert(selected&&SetRegion(ls[1].VisibleRegion,selected)&&AndRegionRegion(domain,selected));
  ls[1].ClipRect=_CreateClipRectsFromRegion(selected,&ls[1],FALSE,domain,0,ls[1]._cliprects,&base);DisposeRegion(domain);DisposeRegion(selected);
  if(superbm){for(int y=0;y<10;y++)for(int x=0;x<12;x++)put(superbm,x,y,stale(x-1,y-2));
   for(int y=0;y<6;y++)for(int x=2;x<4;x++)put(superbm,x+1,y+2,back(x,y));}
  if(smart){
   BOOL shared=FALSE;for(struct ClipRect*c=ls[1].ClipRect;c;c=c->Next)if(c->Flags&CR_SHAREDBITMAP)shared=TRUE;assert(shared);
   for(struct ClipRect*c=ls[1]._cliprects;c;c=c->Next)if(c->lobs&&c->BitMap)
    for(int y=c->bounds.MinY;y<=c->bounds.MaxY;y++)for(int x=c->bounds.MinX;x<=c->bounds.MaxX;x++)
     put(c->BitMap,x-c->bounds.MinX+CR_BMOFFX(c),y-c->bounds.MinY+CR_BMOFFY(c),back(x-2,y-2));
  }
 }
 for(int y=0;y<12;y++)for(int x=0;x<16;x++)put(screenbm,x,y,oldscreen(x,y,windows));
}
static unsigned long long hash(const void*p,size_t n){const UBYTE*q=p;unsigned long long h=1;while(n--)h=h*33+*q++;return h;}
struct Snapshot{struct Layer l[3];unsigned live;unsigned long long screen,scanout,super;};
static void capture(struct Snapshot*s){memcpy(s->l,ls,sizeof ls);s->live=live;s->screen=hash(screenbm->data,screenbm->stride*12);s->scanout=hash(screenbm->scanout,screenbm->stride*12);s->super=superbm?hash(superbm->data,superbm->stride*superbm->height):0;}
static void unchanged(struct Snapshot*s){assert(!memcmp(s->l,ls,sizeof ls));assert(live==s->live);assert(s->screen==hash(screenbm->data,screenbm->stride*12));assert(s->scanout==hash(screenbm->scanout,screenbm->stride*12));if(superbm)assert(s->super==hash(superbm->data,superbm->stride*superbm->height));assert(!maps&&!opens&&!llocks&&!lilocks);}
static void verify(BOOL windows,BOOL noback,BOOL finished,BOOL custom){
 for(int y=0;y<12;y++)for(int x=0;x<16;x++){
  ULONG expected=oldscreen(x,y,windows);BOOL exposed=FALSE;
  if(windows){
   if(y>=3&&y<=8&&x>=2&&x<=7){int sx=x-2,sy=y-3;expected=(sx==2||sx==3)?back(sx,sy):fresh(sx,sy);}
   else if(y>=2&&y<=7&&x>=12&&x<=15){int sx=x-12,sy=y-2;expected=sx<2?front(sx,sy):noback?oldscreen(x,y,TRUE):screenbm->pen0;}
   else {exposed=y>=2&&y<=7&&x>=2&&x<=9;if(exposed&&!noback)expected=finished&&custom?0x8a6c2e:screenbm->pen0;}
  }
  ULONG got=get(screenbm,x,y);if(got!=expected){fprintf(stderr,"live oracle %d,%d got%06x expected%06x noback%d finished%d custom%d\n",x,y,got,expected,noback,finished,custom);abort();}
 }
 if(superbm)for(int y=0;y<10;y++)for(int x=0;x<12;x++){
  int sx=x-1,sy=y-2;ULONG expected=stale(sx,sy);
  if(sy>=0&&sy<6&&sx>=0&&sx<8)expected=(sx==2||sx==3)?back(sx,sy):fresh(sx,sy);
  assert(get(superbm,x,y)==(expected&0xffffff));
 }
 if(windows&&IS_SMARTREFRESH(&ls[1]))assert((ls[2].Flags&((1U<<11)|(1U<<12)))==((1U<<11)|(1U<<12)));
 assert(!(ls[0].Flags&LAYERREFRESH)&&!ls[0].DamageList->RegionRectangle);
}
static struct LayerGeometryBatch*candidate(BOOL windows){struct LayerGeometryRequest r[]={
 {&ls[1],{2,3,7,8},NULL,0,NULL},{&ls[2],{12,2,15,7},NULL,0,NULL}};
 struct LayerGeometryBatch*t=PrepareLayerGeometryBatch(&li,windows?r:NULL,windows?2:0,&refusal,&base);
 if(t&&!SealLayerGeometryBatch(t,&base)){ReleaseLayerGeometryBatch(t,&base);return NULL;}return t;}
static void dispose(BOOL windows){for(int i=0;i<(windows?3:1);i++){free_clips(ls[i].ClipRect,&ls[i],&base);free_clips(ls[i]._cliprects,&ls[i],&base);DisposeRegion(ls[i].shape);DisposeRegion(ls[i].visibleshape);DisposeRegion(ls[i].VisibleRegion);DisposeRegion(ls[i].DamageList);if(ls[i].ClipRegion)DisposeRegion(ls[i].ClipRegion);}if(superbm)FreeBitMap(superbm);FreeBitMap(screenbm);assert(!live&&!bitmap_count&&!maps&&!opens&&!llocks&&!lilocks&&!GetHead(&gfx.ChunkPoolList));}
static ULONG small_expected(int bx,int by,int scrollx,int scrolly){int x=bx-scrollx,y=by-scrolly;return x>=0&&x<8&&y>=0&&y<6&&(x<2||x>=4)?fresh(x,y):0x123456;}
static void small_backing(void){
 unsigned faults=0;
 const int scrolls[4][2]={{1,2},{-2,-1},{20,20},{-20,-20}};
 for(unsigned mode=0;mode<4;mode++)for(unsigned occupancy=0;occupancy<20;occupancy++){
  failat=0;init(TRUE,FALSE,FALSE,FALSE);FreeBitMap(superbm);superbm=AllocBitMap(5,4,32,0,NULL);assert(superbm);ls[1].SuperBitMap=superbm;ls[1].Scroll_X=scrolls[mode][0];ls[1].Scroll_Y=scrolls[mode][1];
  for(int y=0;y<4;y++)for(int x=0;x<5;x++)put(superbm,x,y,0x123456);
  struct RegionRectangleExtChunk*ballast[20];for(unsigned b=0;b<occupancy;b++){ballast[b]=__NewRegionRectangleExtChunk(GfxBase);assert(ballast[b]);}
  struct Snapshot before;capture(&before);
  ordinal=0;struct LayerGeometryBatch*t=candidate(TRUE);assert(t);unsigned attempts=ordinal;hardtrap=1;CommitLayerGeometryBatch(t,&base);hardtrap=0;
  for(int y=0;y<4;y++)for(int x=0;x<5;x++)assert(get(superbm,x,y)==small_expected(x,y,ls[1].Scroll_X,ls[1].Scroll_Y));
  for(int y=0;y<6;y++)for(int x=0;x<6;x++){int bx=x+ls[1].Scroll_X,by=y+ls[1].Scroll_Y;ULONG expected=(x<2||x>=4)?fresh(x,y):bx>=0&&bx<5&&by>=0&&by<4?small_expected(bx,by,ls[1].Scroll_X,ls[1].Scroll_Y):screenbm->pen0;assert(get(screenbm,x+2,y+3)==expected);}
  hardtrap=1;RollbackLayerGeometryBatch(t,&base);hardtrap=0;ReleaseLayerGeometryBatch(t,&base);unchanged(&before);
  for(unsigned f=1;f<=attempts;f++){ordinal=denied=0;failat=f;t=candidate(TRUE);failat=0;assert(!t&&denied==1);unchanged(&before);faults++;}
  struct LayerGeometryRequest r[]={{&ls[1],{2,3,7,8},NULL,0,NULL},{&ls[2],{12,2,15,7},NULL,0,NULL}};
  t=PrepareLayerGeometryBatch(&li,r,2,&refusal,&base);assert(t);struct PreparedPixel pixels[36];for(unsigned i=0;i<36;i++)pixels[i]=(struct PreparedPixel){0xab00,0xcd00,0xef00,0xffff};assert(WriteLayerGeometryPixels(t,&ls[1],pixels,6*sizeof(*pixels),6,&base));assert(SealLayerGeometryBatch(t,&base));hardtrap=1;CommitLayerGeometryBatch(t,&base);hardtrap=0;
  for(int y=0;y<4;y++)for(int x=0;x<5;x++){int lx=x-ls[1].Scroll_X,ly=y-ls[1].Scroll_Y;assert(get(superbm,x,y)==(lx>=0&&lx<6&&ly>=0&&ly<6?0xabcdef:small_expected(x,y,ls[1].Scroll_X,ls[1].Scroll_Y)));}
  for(int y=0;y<6;y++)for(int x=0;x<6;x++)assert(get(screenbm,x+2,y+3)==0xabcdef);
  hardtrap=1;RollbackLayerGeometryBatch(t,&base);hardtrap=0;ReleaseLayerGeometryBatch(t,&base);unchanged(&before);for(unsigned b=0;b<occupancy;b++)__DisposeRegionRectangleExtChunk(ballast[b],GfxBase);dispose(TRUE);
 }
 printf("PASS actual oversized SUPER viewport and override:5x4 backing,6x6/8x6 views; positive,negative,fully outside scroll; clip/seed/override/bitexact rollback;%u forced faults across20 pooloccupancies\n",faults);
}
int main(void){unsigned failures=0,cuts_total=0;
 for(unsigned mode=0;mode<10;mode++)for(unsigned occupancy=0;occupancy<20;occupancy++){
  BOOL windows=mode>=2,noback=mode==1||mode==3||mode==7||mode==9,custom=mode==4||mode==5,smart=mode==6||mode==7,ten=mode>=8;
  unsigned attempts=0;
  for(unsigned f=0;;f++){
   failat=0;init(windows,noback,smart,ten);if(custom)ls[0].BackFill=(struct Hook*)2;if(mode==5)ls[1].Flags|=(1U<<6); /* actual LAYERBACKDROP */
   struct RegionRectangleExtChunk*ballast[20];for(unsigned b=0;b<occupancy;b++){ballast[b]=__NewRegionRectangleExtChunk(GfxBase);assert(ballast[b]);}
   struct Snapshot before;capture(&before);ordinal=denied=0;failat=f;struct LayerGeometryBatch*t=candidate(windows);unsigned used=ordinal;failat=0;
   if(!f){assert(t);attempts=used;hardtrap=1;cutting=1;cut_at=~0U;cut_counter=0;assert(!setjmp(cut_jump));CommitLayerGeometryBatch(t,&base);unsigned cuts=cut_counter;cuts_total+=cuts;cutting=0;verify(windows,noback,FALSE,custom);RollbackLayerGeometryBatch(t,&base);hardtrap=0;ReleaseLayerGeometryBatch(t,&base);unchanged(&before);
    for(unsigned cut=1;cut<=cuts;cut++){t=candidate(windows);assert(t);cut_at=cut;cut_counter=0;cutting=1;hardtrap=1;if(!setjmp(cut_jump)){CommitLayerGeometryBatch(t,&base);assert(!"unreached cut");}cutting=0;RollbackLayerGeometryBatch(t,&base);hardtrap=0;ReleaseLayerGeometryBatch(t,&base);unchanged(&before);}
    t=candidate(windows);assert(t);hardtrap=1;CommitLayerGeometryBatch(t,&base);hardtrap=0;FinishLayerGeometryBatch(t,&base);verify(windows,noback,TRUE,custom);ReleaseLayerGeometryBatch(t,&base);
    if(windows&&!smart){
     /* Restore the original viewport after shrinking. Fresh old visible
      * columns6/7 must return from backing, not the stale pre-Sync bitmap. */
     struct Snapshot restored;capture(&restored);
     struct LayerGeometryRequest grow={&ls[1],{2,2,9,7},NULL,0,NULL};
     t=PrepareLayerGeometryBatch(&li,&grow,1,&refusal,&base);assert(t&&SealLayerGeometryBatch(t,&base));
     hardtrap=1;CommitLayerGeometryBatch(t,&base);hardtrap=0;
     for(int y=2;y<=7;y++)for(int x=2;x<=9;x++)assert(get(screenbm,x,y)==((x==4||x==5)?back(x-2,y-2):fresh(x-2,y-2)));
     hardtrap=1;RollbackLayerGeometryBatch(t,&base);hardtrap=0;ReleaseLayerGeometryBatch(t,&base);unchanged(&restored);
    }
   }else{if(t||denied!=1){fprintf(stderr,"fault mode%u occupancy%u f%u attempts%u t%p denied%u used%u\n",mode,occupancy,f,attempts,(void*)t,denied,used);abort();}unchanged(&before);failures++;}
   for(unsigned b=0;b<occupancy;b++)__DisposeRegionRectangleExtChunk(ballast[b],GfxBase);dispose(windows);if(f==attempts)break;
  }
 }
 small_backing();
 printf("PASS independently built live SUPER/defaultpen/NOBACKFILL/zero-window/custom background: %u allocation faults, %u install cuts; stale visible overlay + shrink-hidden writeback and backing rollback\n",failures,cuts_total);return 0;}
'''
mutants={
 'stale-super-visible':batch.replace('if (!prepare_super_pixels(batch, LayersBase)) goto fail;','if (!prepare_super_pixels(batch, LayersBase)) goto fail;'),
 'black-instead-of-pen0':batch.replace('entry->content[pixel] = seed;', 'entry->content[pixel] = (struct PreparedPixel){0,0,0,0xffff};'),
 'nobackfill-clobber':batch.replace('if (!root && layer->BackFill == LAYERS_NOBACKFILL)', 'if (FALSE)'),
 'unclamped-super-copy':batch.replace('if (bottom > limit) bottom = limit;', '(void)limit;'),
 'unclamped-super-override':batch.replace('(LONG)y+layer->Scroll_Y, width, height,\n                        (const struct PreparedPixel *)', '(LONG)y+layer->Scroll_Y, entry->next.width, entry->next.height,\n                        (const struct PreparedPixel *)'),
 'missing-super-visible-overlay':batch.replace('if (!read_clips(batch, entry, entry->old.regular, LayersBase) ||\n            !read_clips(batch, entry, entry->old.clips, LayersBase)) return FALSE;', '(void)0;'),
 'lost-super-rollback':batch.replace('RestorePreparedPixels(batch->layers[i].super_pixels);','(void)0;'),
 'whole-root-repaint':batch.replace('if (!ClearRegionRegion(entry->valid, missing)) goto region;','if (!root && !ClearRegionRegion(entry->valid, missing)) goto region;'),
}
# Disable ONLY old visible synchronization; retain a valid super mapping.
mutants['stale-super-visible']=batch.replace('if (clip->lobs) continue;\n                left =','if (TRUE) continue;\n                left =')
with tempfile.TemporaryDirectory(prefix='layers-live-') as td:
 for name,body in [('production',batch),*mutants.items()]:
  c=Path(td)/(name+'.c');exe=Path(td)/name;c.write_text(source+body+main)
  r=subprocess.run(['clang','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],capture_output=True,text=True)
  if r.returncode:raise RuntimeError(r.stderr)
  r=subprocess.run([str(exe)],capture_output=True,text=True)
  if name=='production':
   if r.returncode:raise RuntimeError(r.stdout+r.stderr)
   print(r.stdout,end='')
  elif r.returncode==0:raise AssertionError('mutation survived: '+name)
  else:print('REJECT '+name)
print('independent live Layers fixture ASan/UBSan result=PASS')
