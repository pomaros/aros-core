#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Read real detached bitmap pixels through the actual batch/backend boundary."""
from pathlib import Path
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[4]
fixture=root/'developer/debug/test/layers/preparedbatch_host.py'
namespace={'__name__':'batch_fixture','__file__':str(fixture)}
saved=sys.argv
sys.argv=[str(fixture),'--source',str(root)]
exec(fixture.read_text().split("with tempfile.TemporaryDirectory(prefix='layers-batch-')")[0],namespace)
sys.argv=saved
source=namespace['source']
source=source.replace('static ULONG OOP_DoMethod(OOP_Object*object,OOP_Msg msg){',
    'static struct BitMap *tenbit;\nstatic ULONG OOP_DoMethod(OOP_Object*object,OOP_Msg msg){')
source=source.replace('m->map->presentation=b->scanout;',
    'if(b==tenbit){m->map->red=0x3ff00000;m->map->green=0xffc00;m->map->blue=0x3ff;m->map->alpha=0xc0000000;}m->map->presentation=b->scanout;')
# Reuse setup/cleanup and scene oracle; replace only the driver's main.
setup=namespace['main'].split('int main(void)')[0]
main=r'''
int main(void){
 initialize();
 struct LayerGeometryRequest request={&l[3],{8,8,51,41},NULL,LGT_REPLACE_SHAPE,NULL};
 struct LayerGeometryBatch*t=PrepareLayerGeometryBatch(&info[0],&request,1,&refusal,&base);assert(t);
 struct BitMap*b=AllocBitMap(11,7,32,0,NULL);assert(b);
 struct PreparedPixel out[3*7];
 unsigned failures=0,points=0;
 for(int format=0;format<2;format++){
  tenbit=format?b:NULL;
  for(int y=0;y<7;y++)for(int x=0;x<11;x++){
   ULONG value=format?(0x80000000u|((ULONG)(x*77)<<20)|((ULONG)(y*131)<<10)|513u):
     ((ULONG)(x*17)<<16)|((ULONG)(y*23)<<8)|71u;
   memcpy(b->data+y*b->stride+x*4,&value,4);
  }
  uint64_t before=hash(b->data,b->stride*b->height);
  unsigned oldlive=live,oldmaps=maps,oldopens=opens,oldll=llocks,oldli=lilocks;
  for(unsigned fault=0;;fault++){
   memset(out,0x59,sizeof out);ordinal=denied=0;failat=fault;
   BOOL ok=read_bitmap_pixels(t,b,2,1,5,3,out,7*sizeof(*out),&base);
   failat=0;
   if(!fault){assert(ok);points=ordinal;
    for(unsigned y=0;y<3;y++)for(unsigned x=0;x<7;x++){
     struct PreparedPixel*p=&out[y*7+x];
     if(x>=5){assert(p->red==0x5959&&p->green==0x5959&&p->blue==0x5959&&p->alpha==0x5959);continue;}
     assert(p->red==(format?((x+2)*77<<6):((x+2)*17<<8)));
     assert(p->green==(format?((y+1)*131<<6):((y+1)*23<<8)));
     assert(p->blue==(format?(513<<6):(71<<8)));
     assert(p->alpha==(format?0x8000:0xffff));
    }
   }else{assert(!ok&&denied==1);failures++;}
   assert(live==oldlive&&maps==oldmaps&&opens==oldopens&&llocks==oldll&&lilocks==oldli);
   assert(hash(b->data,b->stride*b->height)==before);
   if(fault==points)break;
  }
  assert(!read_bitmap_pixels(t,b,9,1,5,3,out,7*sizeof(*out),&base));
  assert(!read_bitmap_pixels(t,b,2,6,5,3,out,7*sizeof(*out),&base));
  assert(!read_bitmap_pixels(t,b,2,1,5,3,out,4*sizeof(*out),&base));
  assert(live==oldlive&&maps==oldmaps&&opens==oldopens);
 }
 ordinal=0;
 assert(!read_bitmap_pixels(t,rp[0].BitMap,0,0,1,1,out,sizeof(*out),&base));
 l[3].SuperBitMap=b;
 assert(!read_bitmap_pixels(t,b,0,0,1,1,out,sizeof(*out),&base));l[3].SuperBitMap=NULL;
 for(int state=BATCH_SEALED;state<=BATCH_ROLLED_BACK;state++){
  t->state=state;assert(!read_bitmap_pixels(t,b,0,0,1,1,out,sizeof(*out),&base));
 }
 t->state=BATCH_PREPARING;
 assert(!read_bitmap_pixels(NULL,b,0,0,1,1,out,sizeof(*out),&base));
 assert(!read_bitmap_pixels(t,NULL,0,0,1,1,out,sizeof(*out),&base));
 assert(!read_bitmap_pixels(t,b,0,0,1,1,NULL,sizeof(*out),&base));
 assert(!read_bitmap_pixels(t,b,0,0,0,1,out,sizeof(*out),&base));
 assert(!ordinal);
 /* Paint a subrectangle while preserving every other prepared canvas pixel. */
 tenbit=NULL;FreeBitMap(b);b=AllocBitMap(44,34,32,0,NULL);assert(b);
 for(int y=0;y<34;y++)for(int x=0;x<44;x++){ULONG p=0x3478ab;memcpy(b->data+y*b->stride+x*4,&p,4);}
 struct PreparedLayer*entry=NULL;for(unsigned i=0;i<t->layer_count;i++)if(t->layers[i].layer==&l[3])entry=&t->layers[i];assert(entry);
 size_t size=entry->stride*entry->next.height;void*copy=malloc(size);assert(copy);memcpy(copy,entry->content,size);
 unsigned char damage[44*34],backfill[44*34];
 for(int y=0;y<34;y++)for(int x=0;x<44;x++){damage[y*44+x]=inregion(entry->next.damage,x,y);backfill[y*44+x]=entry->backfill?inregion(entry->backfill,x,y):0;}
 struct Rectangle bounds={3,4,7,6};
 assert(WriteLayerGeometryBitmap(t,&l[3],b,&bounds,&base));
 for(int y=0;y<34;y++)for(int x=0;x<44;x++){
  struct PreparedPixel*p=(void*)((UBYTE*)entry->content+y*entry->stride+x*sizeof(*p));
  BOOL painted=x>=3&&x<=7&&y>=4&&y<=6;
  assert(inregion(entry->next.damage,x,y)==(damage[y*44+x]&&!painted));
  assert((entry->backfill?inregion(entry->backfill,x,y):0)==(backfill[y*44+x]&&!painted));
  if(x>=3&&x<=7&&y>=4&&y<=6)assert(p->red==0x3400&&p->green==0x7800&&p->blue==0xab00);
  else assert(!memcmp(p,(UBYTE*)copy+y*entry->stride+x*sizeof(*p),sizeof(*p)));
 }
 assert(!WriteLayerGeometryBitmap(t,&l[0],b,&bounds,&base));
 bounds.MaxX=44;assert(!WriteLayerGeometryBitmap(t,&l[3],b,&bounds,&base));
 free(copy);FreeBitMap(b);ReleaseLayerGeometryBatch(t,&base);dispose();
 unsigned paintpoints=0,paintfaults=0;
 for(unsigned fault=0;;fault++){
  failat=0;initialize();t=PrepareLayerGeometryBatch(&info[0],&request,1,&refusal,&base);assert(t);
  b=AllocBitMap(44,34,32,0,NULL);assert(b);memset(b->data,0x37,b->stride*b->height);
  uint64_t root_before=hash(rp[0].BitMap->data,rp[0].BitMap->stride*48),src_before=hash(b->data,b->stride*b->height);
  bounds=(struct Rectangle){12,9,22,16};ordinal=denied=0;failat=fault;
  BOOL ok=WriteLayerGeometryBitmap(t,&l[3],b,&bounds,&base);failat=0;
  if(!fault){assert(ok);paintpoints=ordinal;}else{assert(!ok&&denied==1);paintfaults++;}
  assert(hash(rp[0].BitMap->data,rp[0].BitMap->stride*48)==root_before);
  assert(hash(b->data,b->stride*b->height)==src_before);
  FreeBitMap(b);ReleaseLayerGeometryBatch(t,&base);dispose();
  if(fault==paintpoints)break;
 }
 printf("PASS partial native paint: preserved canvas/damage/backfill outside client rect; %u fallible paint cuts leave source and live raster unchanged\n",paintfaults);
 printf("PASS actual bitmap read:8/10bit exact, padded subrect, bounds/stride, %u prepare faults, scene+super alias and all sealed states refused before map, unchanged buffers/leases\n",failures);
}
'''
batch=namespace['batch']
mutants={
 'release':batch.replace('ReleasePreparedPixels(source);','(void)0;'),
 'source-origin':batch.replace('ReadOldPixels(source, x, y, width, height, pixels, stride)','ReadOldPixels(source, 0, 0, width, height, pixels, stride)'),
 'scene-alias':batch.replace('if (batch->stacks[i].bitmap == bitmap) return batch_refuse(batch, LGR_SOURCE_ALIAS, NULL, i, 0, 0, 0);','if (FALSE) return FALSE;'),
 'partial-damage':batch.replace('!ClearRectRegion(entry->next.damage, &painted)', '(ClearRegion(entry->next.damage), FALSE)'),
 'partial-width':batch.replace('ULONG width = (LONG)bounds->MaxX - bounds->MinX + 1;', 'ULONG width = (LONG)bounds->MaxX - bounds->MinX;'),
 'state':batch.replace('if (batch->state != BATCH_PREPARING) return batch_refuse(batch, LGR_STATE, NULL, batch->state, 0, 0, 0);\n    if (!bitmap || !pixels','if (!bitmap || !pixels'),
}
with tempfile.TemporaryDirectory(prefix='preparedbitmap-') as tmp:
 tmp=Path(tmp)
 for name,body in [('production',batch),*mutants.items()]:
  c=tmp/(name+'.c');exe=tmp/name;c.write_text(source+body+setup+main)
  r=subprocess.run(['clang','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],capture_output=True,text=True)
  assert r.returncode==0,r.stderr
  r=subprocess.run([str(exe)],capture_output=True,text=True)
  if name=='production':assert r.returncode==0,r.stdout+r.stderr;print(r.stdout,end='')
  else:assert r.returncode!=0,'mutant survived '+name;print('REJECT '+name)
