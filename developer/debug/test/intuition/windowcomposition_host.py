#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[4]
s=(root/'rom/intuition/windowcomposition.c').read_text()
struct=s[s.index('struct WindowComposition\n{'):s.index('\nstatic void mark_damage')]
mark=s[s.index('static void mark_damage'):s.index('\nAROS_UFH3')]
present=s[s.index('BOOL WindowCompositionPresent(struct'):s.index('\nBOOL WindowCompositionPresentScreen')]
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD; typedef int16_t WORD; typedef int64_t QUAD; typedef uint64_t UQUAD; typedef uintptr_t IPTR; typedef void *APTR; typedef unsigned char UBYTE; typedef int BOOL; typedef int OOP_MethodID; typedef void OOP_Object;
#define TRUE 1
#define FALSE 0
#define LAYERREFRESH 128
#define vHidd_StdPixFmt_Native32 7
struct WindowComposition;
struct SignalSemaphore { int unused; };
struct Hook { void *h_Data; };
struct Rectangle { LONG MinX,MinY,MaxX,MaxY; };
struct ClipRect { struct ClipRect *Next; void *lobs; struct Rectangle bounds; };
struct Layer_Info { int ordinal; };
struct Layer { ULONG Flags; BOOL visible; struct ClipRect *ClipRect; void *DamageList; };
struct BitMap { ULONG *pixels; ULONG width,height; };
struct GfxBase { int unused; }; struct LayersBase { int unused; }; struct Library { int unused; };
struct Screen { struct Layer_Info LayerInfo; struct { struct BitMap *BitMap; } RastPort; };
struct Window { LONG LeftEdge,TopEdge,Width,Height; struct Screen *WScreen; struct WindowComposition *Composition; };
struct IntuitionBase { struct GfxBase *GfxBase; struct LayersBase *LayersBase; struct Library *OOPBase; int ib_HiddBitMapBase; };
#define GetPrivIBase(b) (b)
#define IW(w) (w)
#define IS_VISIBLE(l) ((l)->visible)
#define HIDD_BM_OBJ(b) ((OOP_Object *)(b))
/* Model actual layer API ownership with host mutexes. The client owns a
 * legacy source before the presenter freezes physical clipping, then attempts
 * an aware destination while the presenter waits for the legacy source. */
static int lockstage,updates,screenAllLayers,interleave,clientReady,clientGo,cycle;
static pthread_mutex_t screenMutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t logicalMutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t awareMutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gateMutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate=PTHREAD_COND_INITIALIZER;
static void LockLayerInfo(struct Layer_Info *li) {
 assert(li->ordinal==1 && lockstage==0);
 assert(!pthread_mutex_lock(&screenMutex));lockstage=1;
}
static void UnlockLayerInfo(struct Layer_Info *li) {
 assert(li->ordinal==1 && lockstage==1);
 lockstage=0;assert(!pthread_mutex_unlock(&screenMutex));
}
static void LockLayers(struct Layer_Info *li) {
 if(li->ordinal==1) {
  LockLayerInfo(li);assert(!pthread_mutex_lock(&awareMutex));screenAllLayers=1;
 } else {
  assert(li->ordinal==2 && lockstage==1);
  if(interleave) {
   assert(!pthread_mutex_lock(&gateMutex));clientGo=1;
   assert(!pthread_cond_broadcast(&gate));assert(!pthread_mutex_unlock(&gateMutex));
  }
  assert(!pthread_mutex_lock(&logicalMutex));assert(!cycle);lockstage=2;
 }
}
static void UnlockLayers(struct Layer_Info *li) {
 if(li->ordinal==1) {
  assert(screenAllLayers);screenAllLayers=0;
  assert(!pthread_mutex_unlock(&awareMutex));UnlockLayerInfo(li);
 } else {
  assert(li->ordinal==2 && lockstage==2);lockstage=1;
  assert(!pthread_mutex_unlock(&logicalMutex));
 }
}
static void *clipblit_client(void *unused) {
 (void)unused;assert(!pthread_mutex_lock(&logicalMutex));
 assert(!pthread_mutex_lock(&gateMutex));clientReady=1;
 assert(!pthread_cond_broadcast(&gate));
 while(!clientGo)assert(!pthread_cond_wait(&gate,&gateMutex));
 assert(!pthread_mutex_unlock(&gateMutex));
 int result=pthread_mutex_trylock(&awareMutex);
 if(result==EBUSY)cycle=1; /* Would block while presenter waits on our source. */
 else {assert(!result);assert(!pthread_mutex_unlock(&awareMutex));}
 assert(!pthread_mutex_unlock(&logicalMutex));return NULL;
}
static void ObtainSemaphore(struct SignalSemaphore *s) {(void)s;}
static void ReleaseSemaphore(struct SignalSemaphore *s) {(void)s;}
static void ClearRegion(void *r) {(void)r;}
static void HIDD_BM_GetImage(OOP_Object *o,UBYTE *out,ULONG pitch,LONG x,LONG y,ULONG width,ULONG height,int fmt) {
 struct BitMap *bm=(struct BitMap *)o; assert(lockstage==2 && x>=0 && y>=0 && x+width<=bm->width && y+height<=bm->height && fmt==7);
 for(ULONG j=0;j<height;j++) for(ULONG i=0;i<width;i++) ((ULONG *)(out+j*pitch))[i]=bm->pixels[(y+j)*bm->width+x+i];
}
static void HIDD_BM_PutImage(OOP_Object *o,OOP_Object *gc,UBYTE *in,ULONG pitch,LONG x,LONG y,ULONG width,ULONG height,int fmt) {
 (void)gc; struct BitMap *bm=(struct BitMap *)o; assert(lockstage==2 && x>=0 && y>=0 && x+width<=bm->width && y+height<=bm->height && fmt==7);
 for(ULONG j=0;j<height;j++) for(ULONG i=0;i<width;i++) bm->pixels[(y+j)*bm->width+x+i]=((ULONG *)(in+j*pitch))[i];
}
static void UpdateBitMap(struct BitMap *b,LONG x,LONG y,LONG w,LONG h) { (void)b;(void)x;(void)y;assert(lockstage==2 && w>0 && h>0);++updates; }
'''
main=r'''
int main(void) {
 ULONG source[80*60],destination[80*60],sr[80],dr[80];
 struct BitMap sb={source,80,60},db={destination,80,60};
 struct Layer_Info logical={2}; struct Screen screen={{1},{&db}};
 struct IntuitionBase base={0}; struct ClipRect clip={0}; struct Layer proxy={0,1,&clip,0};
 struct Window win={2,1,8,6,&screen,0};
 struct WindowComposition state={0}; state.window=&win;state.base=&base;state.bitmap=&sb;state.logical=&logical;state.proxy=&proxy;state.sourceRow=sr;state.destRow=dr;state.width=80;state.height=60;win.Composition=&state;
 pthread_t client;interleave=1;
 assert(!pthread_mutex_lock(&gateMutex));
 assert(!pthread_create(&client,NULL,clipblit_client,NULL));
 while(!clientReady)assert(!pthread_cond_wait(&gate,&gateMutex));
 assert(!pthread_mutex_unlock(&gateMutex));
 assert(WindowCompositionPresent(&win,&base));
 assert(!pthread_join(client,NULL));interleave=0;assert(!cycle);
 puts("PASS actual presenter lock interleaving: legacy source held, aware destination stays available");

 for(unsigned k=1;k<=4;k++) {
  state.transform.x=win.LeftEdge*k; state.transform.y=win.TopEdge*k;state.transform.factor=k;
  clip.bounds=(struct Rectangle){state.transform.x+1,state.transform.y+1,state.transform.x+win.Width*k-2,state.transform.y+win.Height*k-2};
  for(unsigned i=0;i<80*60;i++){source[i]=i+1000;destination[i]=0xdeadbeef;}
  mark_damage(&state,0,0,80,60); assert(WindowCompositionPresent(&win,&base)); assert(!lockstage&&!state.dirty);
  for(int y=0;y<60;y++)for(int x=0;x<80;x++){
   ULONG want=0xdeadbeef;
   if(x>=clip.bounds.MinX&&x<=clip.bounds.MaxX&&y>=clip.bounds.MinY&&y<=clip.bounds.MaxY) want=source[(win.TopEdge+(y-state.transform.y)/k)*80+win.LeftEdge+(x-state.transform.x)/k];
   assert(destination[y*80+x]==want);
  }
  for(unsigned i=0;i<80*60;i++)destination[i]=0xdeadbeef;
  mark_damage(&state,4,3,5,4); assert(WindowCompositionPresent(&win,&base));
  for(int y=0;y<60;y++)for(int x=0;x<80;x++){
   ULONG want=0xdeadbeef;
   if(x>=4*(int)k&&x<5*(int)k&&y>=3*(int)k&&y<4*(int)k&&x>=clip.bounds.MinX&&x<=clip.bounds.MaxX&&y>=clip.bounds.MinY&&y<=clip.bounds.MaxY)want=source[3*80+4];
   assert(destination[y*80+x]==want);
  }
  mark_damage(&state,2,1,3,2);proxy.visible=0;assert(WindowCompositionPresent(&win,&base));assert(state.dirty);proxy.visible=1;
  proxy.Flags|=LAYERREFRESH;assert(WindowCompositionPresent(&win,&base));assert(!(proxy.Flags&LAYERREFRESH));
 }
 assert(updates==12);puts("PASS production presenter: factors1..4, partial-clip pixels, one-pixel damage, hidden queue, exposure, screen/logical lock order");
}
'''
with tempfile.TemporaryDirectory(prefix='aros-windowcompose-') as d:
 d=Path(d);(d/'exec').mkdir();(d/'exec/types.h').write_text('')
 for label,production in [('production',present),('missing-scale',present.replace('(x - transform.x) / transform.factor','(x - transform.x)')),('all-screen-layers',present.replace('LockLayerInfo(&window->WScreen->LayerInfo);','LockLayers(&window->WScreen->LayerInfo);').replace('UnlockLayerInfo(&window->WScreen->LayerInfo);','UnlockLayers(&window->WScreen->LayerInfo);')),('lost-hidden-damage',present.replace('mark_damage(state, damage.left, damage.top, damage.right, damage.bottom);','(void)damage;'))]:
  c=prefix+'\n#include "'+str(root/'rom/graphics/scalecompose.h')+'"\n'+struct+mark+production+main
  (d/'test.c').write_text(c)
  subprocess.run(['clang','-std=c99','-Wall','-Wextra','-Wno-unused-variable','-Wno-unused-function','-pthread','-fsanitize=address,undefined','-g','-I',str(d),str(d/'test.c'),'-o',str(d/'test')],check=True)
  result=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=15)
  if label=='production':
   assert result.returncode==0,result.stdout+result.stderr
   print(result.stdout,end='')
  else:
   assert result.returncode!=0, 'negativecontrol escaped: '+label
   print('PASS negative control:',label)
