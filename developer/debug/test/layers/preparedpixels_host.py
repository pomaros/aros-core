#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Sanitize the actual prepared pixel backend with allocation/map fault injection."""
from pathlib import Path
import re, subprocess, tempfile
root = Path(__file__).resolve().parents[4]
source = (root/'rom/hyperlayers/preparedpixels.c').read_text()
source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
map_header=(root/'rom/hidds/gfx/include/preparedpixels.h').read_text()
map_header=re.sub(r'^#include[^\n]*\n','',map_header,flags=re.M)
cursor_header=(root/'rom/hidds/gfx/include/preparedcursor.h').read_text()
cursor_header=re.sub(r'^#include[^\n]*\n','',cursor_header,flags=re.M)
# The caller-owned Layers refusal record and its LGR codes, from the header.
lt=(root/'compiler/include/graphics/layertransaction.h').read_text()
refusal_header='struct Layer;\n'+'\n'.join(l for l in lt.splitlines() if re.match(r'#define\s+LGR',l))+'\n'+\
    lt[lt.index('struct LayerGeometryRefusal\n{'):lt.index('};',lt.index('struct LayerGeometryRefusal\n{'))+2]+'\n'
preamble=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint8_t UBYTE; typedef uint16_t UWORD; typedef uint32_t ULONG;typedef int32_t LONG;
typedef int BOOL; typedef void VOID; typedef void *APTR; typedef void *OOP_Object;
typedef void *OOP_Msg;typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define MEMF_ANY 0
#define MEMF_CLEAR 1
#define IID_Hidd_BitMap "bitmap"
#define moHidd_BitMap_PreparePixels 1
#define moHidd_BitMap_ReleasePixels 2
#define moHidd_BitMap_RenderPreparedCursor 3
struct Library { int x; }; struct BitMap { int supported; };
struct PreparedPixel { UWORD red,green,blue,alpha; };
#define IS_HIDD_BM(b) ((b)->supported)
#define HIDD_BM_OBJ(b) ((OOP_Object *)(b))
#define HIDD_BM_COLMAP(b) ((OOP_Object*)NULL)
#define IID_Hidd_ColorMap "colormap"
#define moHidd_ColorMap_GetPixel 4
struct pHidd_ColorMap_GetPixel{ULONG id,pen;};
static int fail,point,active,opens,mapped,transaction,refuse;
static void *AllocVec(ULONG size,ULONG flags) {
 assert(!transaction); if (++point==fail) return NULL;
 void *p=malloc(size); if(p) {++active;if(flags&MEMF_CLEAR)memset(p,0,size);} return p;
}
static void FreeVec(void *p) {assert(!transaction);if(p){--active;free(p);}}
static struct Library *OpenLibrary(const char *n,int v) {
 (void)n;(void)v;assert(!transaction);if(++point==fail)return NULL;++opens;return (void *)1;
}
static void CloseLibrary(struct Library *p) {(void)p;assert(!transaction);--opens;}
static ULONG OOP_GetMethodID(const char *s,ULONG id) {(void)s;assert(!transaction);return id;}
'''
mock=r'''
struct pHidd_BitMap_PreparePixels { ULONG id; struct HIDD_PixelMap *map; };
struct pHidd_BitMap_ReleasePixels { ULONG id; struct HIDD_PixelMap *map; };
struct pHidd_BitMap_RenderPreparedCursor { ULONG id;struct HIDD_PixelMap *map;UBYTE *stage;struct HIDD_CursorState *proposed; };
static void HostRender(struct HIDD_PixelMap *,UBYTE *);
static struct HIDD_PixelMap fixture;
static struct HIDD_CursorState *expected_proposed;
static int proposed_calls;
/* Transport adapter: actual cursor clipping/rendering is checked below. */
static void HostProposed(struct HIDD_PixelMap *map,UBYTE *stage,struct HIDD_CursorState *proposed) {
 assert(proposed==expected_proposed&&map->cursorState&&map->cursorCookie&&map->cursorSemaphore);
 ++proposed_calls;proposed->cursor_drawn=map->cursorState->cursor_visible;
 if(!proposed->cursor_drawn)return;
 for(ULONG y=0;y<map->height;y++) {
  memcpy(proposed->cursor_backup+y*map->width*4,stage+y*map->stride,map->width*4);
  memset(stage+y*map->stride,0xb7,map->width*4);
 }
}
static ULONG OOP_DoMethod(OOP_Object *o,OOP_Msg msg) {
 struct pHidd_BitMap_PreparePixels *m=msg;(void)o;assert(!transaction);
 if(m->id==1){if(refuse){m->map->refusal=HIDD_PIXELS_REFUSE_SCANOUT;m->map->refusalValue[0]=HIDD_SCANOUT_NOT_OWNED;m->map->refusalClass="hidd.bitmap.efifb";return 0;}
  if(++point==fail)return 0;*m->map=fixture;++mapped;return 1;}
 if(m->id==3){struct pHidd_BitMap_RenderPreparedCursor *r=msg;if(++point==fail){r->map->refusal=HIDD_PIXELS_REFUSE_CURSOR_RENDER;r->map->refusalValue[0]=HIDD_CURSOR_REFUSE_RENDERER;r->map->refusalClass="CursorFB";return 0;}if(r->proposed)HostProposed(r->map,r->stage,r->proposed);else HostRender(r->map,r->stage);return 1;}
 assert(m->id==2);--mapped;return 0;
}
static void ReleasePreparedPixels(struct PreparedPixels *);
'''.replace('static void ReleasePreparedPixels(struct PreparedPixels *);','struct PreparedPixels;\nvoid ReleasePreparedPixels(struct PreparedPixels *);')
# Actual ordinary BGR032 renderer plus generic buffered fallback are the oracle.
cb_source=(root/'rom/hidds/gfx/gfx_chunkybitmapclass.c').read_text()
bm_source=(root/'rom/hidds/gfx/gfx_bitmapclass.c').read_text()
alpha_cb=cb_source[cb_source.index('int static inline\n__attribute__((always_inline, const)) do_alpha'):cb_source.index('VOID CBM__Hidd_BitMap__GetImage')]
alpha_bm=bm_source[bm_source.index('#if defined(EXACT_ALPHA)'):bm_source.index('/*\n * Non-premultiplied')]
alpha_bm=alpha_bm.replace('do_alpha(', 'bm_do_alpha(')
over=bm_source[bm_source.index('static inline ULONG over_alpha'):bm_source.index('/*****************************************************************************************',bm_source.index('static inline ULONG over_alpha'))]
buffered=bm_source[bm_source.index('static void PutAlphaImageBuffered'):bm_source.index('VOID BM__Hidd_BitMap__PutAlphaImage')].replace('do_alpha(', 'bm_do_alpha(')
renderer_preamble=r'''
#define AROS_BIG_ENDIAN 0
#define EnterFunc(x)
typedef int16_t WORD;typedef int HIDDT_StdPixFmt;typedef void OOP_Class;
struct chunkybm_data { UBYTE *buffer;ULONG bytesperrow,bytesperpixel; };
struct pHidd_BitMap_PutAlphaImage { UBYTE *pixels;ULONG modulo;WORD x,y,width,height; };
struct HostFormat { HIDDT_StdPixFmt stdpixfmt; };
static struct HostFormat host_format;
#define BM_PIXFMT(o) (&host_format)
#define OOP_INST_DATA(cl,o) ((void)(cl),(void *)(o))
#define vHidd_StdPixFmt_BGR032 1
#define vHidd_StdPixFmt_RGB16_LE 2
static void OOP_DoSuperMethod(OOP_Class *,OOP_Object *,void *);
struct paib_data { void *pixels;ULONG modulo;BOOL dst_opaque; };
'''
renderer_suffix=r'''
static struct HIDD_PixelMap *render_map;
static void OOP_DoSuperMethod(OOP_Class *cl,OOP_Object *obj,void *message) {
 (void)cl;struct chunkybm_data *data=(void *)obj;
 struct pHidd_BitMap_PutAlphaImage *msg=message;
 for(int y=0;y<msg->height;++y)for(int x=0;x<msg->width;++x){
  UBYTE *dst=data->buffer+(y+msg->y)*data->bytesperrow+(x+msg->x)*4;
  ULONG v=Load(dst),argb=((ULONG)(Component(v,render_map->red)>>8)<<8)|
   ((ULONG)(Component(v,render_map->green)>>8)<<16)|((ULONG)(Component(v,render_map->blue)>>8)<<24);
  struct paib_data paib={msg->pixels+y*msg->modulo+x*4,1,TRUE};
  PutAlphaImageBuffered(&argb,0,1,1,&paib);
  Store(dst,Pack((argb>>8&255)<<8,render_map->red)|Pack((argb>>16&255)<<8,render_map->green)|Pack((argb>>24&255)<<8,render_map->blue));
 }
}
static void HostRender(struct HIDD_PixelMap *m,UBYTE *stage) {
 struct chunkybm_data data={stage,m->stride,4};render_map=m;
 host_format.stdpixfmt=m->red==0xff0000?1:0;
 struct pHidd_BitMap_PutAlphaImage msg={m->cursorARGB,m->cursorARGBStride,m->cursorX,m->cursorY,m->cursorW,m->cursorH};
 CBM__Hidd_BitMap__PutAlphaImage(NULL,(void *)&data,&msg);
}
'''
renderer=renderer_preamble+alpha_bm+over+buffered+alpha_cb+renderer_suffix
tests=r'''
static UBYTE cpu[7*28],hw[7*32],savedcpu[sizeof(cpu)],savedhw[sizeof(hw)];
static UBYTE backup[16],oldbackup[16],argb[16];
static struct BitMap bm={1};static struct LayerGeometryRefusal refusal;
static void refused(ULONG lease){assert(refusal.lgr_Check==LGR_PIXELS&&refusal.lgr_Pixels==LGR_PIXELS_STACK&&refusal.lgr_Lease==lease);}
static void init(int thirty,int cursor) {
 unsigned i;memset(&fixture,0,sizeof(fixture));
 for(i=0;i<sizeof(cpu);++i)cpu[i]=(i*17+5)&255;
 for(i=0;i<sizeof(hw);++i)hw[i]=(i*31+7)&255;
 fixture.pixels=cpu;fixture.width=5;fixture.height=7;fixture.stride=28;fixture.size=sizeof(cpu);
 fixture.presentation=hw;fixture.presentationStride=32;fixture.presentationSize=sizeof(hw);
 fixture.red=thirty?0x3ff00000:0xff0000;fixture.green=thirty?0xffc00:0xff00;
 fixture.blue=thirty?0x3ff:0xff;
 if(cursor){
  for(i=0;i<sizeof(backup);++i)backup[i]=(i*11+3)&255;
  memset(argb,255,sizeof(argb));
  fixture.cursorBackup=backup;fixture.cursorARGB=argb;fixture.cursorARGBStride=8;
  fixture.cursorX=1;fixture.cursorY=2;fixture.cursorW=2;fixture.cursorH=2;
 }
 memcpy(savedcpu,cpu,sizeof(cpu));memcpy(savedhw,hw,sizeof(hw));memcpy(oldbackup,backup,sizeof(backup));
 point=fail=refuse=0;assert(!active&&!opens&&!mapped);
}
static void unchanged(void) {
 assert(!memcmp(cpu,savedcpu,sizeof(cpu)));assert(!memcmp(hw,savedhw,sizeof(hw)));
 assert(!memcmp(backup,oldbackup,sizeof(backup)));assert(!active&&!opens&&!mapped);
}
int main(void) {
 int mode,cursor,n,total=0;struct PreparedPixels *p;struct PreparedPixel pixels[35],out[35];
 for(mode=0;mode<2;++mode)for(cursor=0;cursor<2;++cursor){
  init(mode,cursor);p=PreparePixels(&bm,TRUE,&refusal,LGR_PIXELS_STACK);assert(p);n=point;ReleasePreparedPixels(p);unchanged();
  for(int i=1;i<=n;++i){init(mode,cursor);memset(&refusal,0,sizeof refusal);fail=i;p=PreparePixels(&bm,TRUE,&refusal,LGR_PIXELS_STACK);assert(!p);unchanged();++total;
   /* Each failing step names itself: allocation, library or driver map. */
   assert(refusal.lgr_Check==LGR_PIXELS&&(refusal.lgr_Lease==LGR_LEASE_MEMORY||refusal.lgr_Lease==LGR_LEASE_OOP||refusal.lgr_Lease==LGR_LEASE_DRIVER));
   if(refusal.lgr_Lease==LGR_LEASE_MEMORY)assert(refusal.lgr_Value[0]>0);}
  init(mode,cursor);p=PreparePixels(&bm,TRUE,&refusal,LGR_PIXELS_STACK);assert(p);
  assert(ReadOldPixels(p,0,0,5,7,pixels,5*sizeof(*pixels)));
  if(cursor){ULONG v=Load(oldbackup);assert(pixels[11].red==Component(v,p->map.red));
   assert(pixels[11].green==Component(v,p->map.green));assert(pixels[11].blue==Component(v,p->map.blue));}
  assert(WritePreparedPixels(p,0,0,5,7,pixels,5*sizeof(*pixels)));
  assert(ReadStagedPixels(p,0,0,5,7,out,5*sizeof(*out)));assert(!memcmp(pixels,out,sizeof(out)));
  assert(!WritePreparedPixels(p,4,0,2,1,pixels,sizeof(pixels)));
  refused(LGR_LEASE_RANGE);assert(refusal.lgr_Value[0]==4&&refusal.lgr_Value[2]==2&&refusal.lgr_Value[3]==1);
  memset(&refusal,0,sizeof refusal);assert(!ReadOldPixels(p,0,0,5,7,out,1));refused(LGR_LEASE_RANGE);
  assert(!ReadOldPixels(p,0,0,5,7,out,0xffffffff));
  assert(!CopyPreparedPixels(p,0,0,p,0,0,6,1,TRUE));
  assert(CopyPreparedPixels(p,1,1,p,0,0,4,6,TRUE));
  assert(ReadStagedPixels(p,1,1,4,6,out,4*sizeof(*out)));
  for(int y=0;y<6;++y)assert(!memcmp(out+y*4,pixels+y*5,4*sizeof(*out)));
  assert(SealPreparedPixels(p));assert(!SealPreparedPixels(p));refused(LGR_LEASE_SEALED);
  assert(!WritePreparedPixels(p,0,0,1,1,pixels,sizeof(*pixels)));refused(LGR_LEASE_SEALED);
  transaction=1;CommitPreparedPixels(p);transaction=0;
  assert(memcmp(cpu,savedcpu,sizeof(cpu)));
  for(int y=0;y<7;++y){assert(!memcmp(cpu+y*28,hw+y*32,20));
   assert(!memcmp(cpu+y*28+20,savedcpu+y*28+20,8));
   assert(!memcmp(hw+y*32+20,savedhw+y*32+20,12));}
  if(cursor){assert(memcmp(backup,oldbackup,sizeof(backup)));
   for(int y=0;y<2;++y)for(int x=0;x<2;++x){ULONG v=Load(cpu+(y+2)*28+(x+1)*4);
    assert((v&(fixture.red|fixture.green|fixture.blue))==(mode?0x3fcff3fc:(fixture.red|fixture.green|fixture.blue)));}}
  transaction=1;RestorePreparedPixels(p);transaction=0;
  ReleasePreparedPixels(p);unchanged();
  init(mode,cursor);p=PreparePixels(&bm,FALSE,&refusal,LGR_PIXELS_STACK);assert(p);assert(ReadOldPixels(p,0,0,5,7,out,5*sizeof(*out)));
  assert(!WritePreparedPixels(p,0,0,1,1,out,sizeof(*out)));assert(!SealPreparedPixels(p));
  transaction=1;CommitPreparedPixels(p);RestorePreparedPixels(p);transaction=0;
  ReleasePreparedPixels(p);unchanged();
 }
 for(mode=0;mode<2;++mode){
  init(mode,1);memset(cpu,0,sizeof(cpu));memset(backup,0,sizeof(backup));
  for(int i=0;i<4;++i){argb[i*4]=128;argb[i*4+1]=1;argb[i*4+2]=argb[i*4+3]=0;}
  memcpy(savedcpu,cpu,sizeof(cpu));memcpy(oldbackup,backup,sizeof(backup));
  p=PreparePixels(&bm,TRUE,&refusal,LGR_PIXELS_STACK);assert(p);fail=point+1;assert(!SealPreparedPixels(p));
  refused(LGR_LEASE_CURSOR);assert(refusal.lgr_Driver==HIDD_PIXELS_REFUSE_CURSOR_RENDER&&refusal.lgr_Value[0]==HIDD_CURSOR_REFUSE_RENDERER&&!strcmp((char*)refusal.lgr_Class,"CursorFB"));
  assert(!memcmp(cpu,savedcpu,sizeof(cpu)));assert(!memcmp(hw,savedhw,sizeof(hw)));
  fail=0;assert(SealPreparedPixels(p));transaction=1;CommitPreparedPixels(p);transaction=0;
  assert(Load(cpu+2*28+4)==(mode?0x00400000:0x00010000));
  transaction=1;RestorePreparedPixels(p);transaction=0;ReleasePreparedPixels(p);unchanged();
 }
 /* Proposed transport is independent of the old drawn rectangle. The
    adapter checks full-owner publication; actual HIDD clipping is below. */
 for(int old=0;old<2;old++)for(int visible=0;visible<2;visible++) {
  init(0,old);UBYTE next_backup[140]={0};
  struct HIDD_CursorState live={.cursor_visible=visible},untouched=live;
  struct HIDD_CursorState next={.cursor_backup=next_backup};
  fixture.cursorState=&live;fixture.cursorCookie=&live;fixture.cursorSemaphore=&live;
  p=PreparePixels(&bm,TRUE,&refusal,LGR_PIXELS_STACK);assert(p);expected_proposed=&next;proposed_calls=0;
  assert(!PreparePixelsCursor(p,NULL));
  assert(PreparePixelsCursor(p,&next)==&p->map);
  assert(!PreparePixelsCursor(p,&next));
  assert(!memcmp(&live,&untouched,sizeof live));
  assert(SealPreparedPixels(p)&&proposed_calls==1);
  assert(!PreparePixelsCursor(p,&next)&&!memcmp(&live,&untouched,sizeof live));
  if(visible) {
   for(int y=0;y<7;y++)for(int x=0;x<5;x++) {
    ULONG prior=old&&x>=1&&x<3&&y>=2&&y<4?Load(oldbackup+((y-2)*2+x-1)*4):Load(savedcpu+y*28+x*4);
    assert(Load(next_backup+(y*5+x)*4)==prior);
   }
  }
  transaction=1;CommitPreparedPixels(p);transaction=0;
  assert(!memcmp(backup,oldbackup,sizeof backup));
  for(int y=0;y<7;y++)for(int x=0;x<5;x++) {
   ULONG prior=old&&x>=1&&x<3&&y>=2&&y<4?Load(oldbackup+((y-2)*2+x-1)*4):Load(savedcpu+y*28+x*4);
   assert(Load(cpu+y*28+x*4)==(visible?0xb7b7b7b7:prior));
  }
  transaction=1;RestorePreparedPixels(p);transaction=0;ReleasePreparedPixels(p);unchanged();
 }
 init(0,0);struct HIDD_CursorState rejected={0};
 p=PreparePixels(&bm,TRUE,&refusal,LGR_PIXELS_STACK);assert(p&&!PreparePixelsCursor(p,&rejected));ReleasePreparedPixels(p);unchanged();
 p=PreparePixels(&bm,FALSE,&refusal,LGR_PIXELS_STACK);assert(p&&!PreparePixelsCursor(p,&rejected));ReleasePreparedPixels(p);unchanged();
 puts("proposed cursor backend transport adapter PASS old=hidden/drawn next=hidden/full-owner, old save-under immutable, exact rollback");
 puts("actual ordinary CBM BGR032 + base30-bit buffered renderer oracle PASS; alpha128/red1 rounding and Xbits checked");
 init(0,0);refuse=1;assert(!PreparePixels(&bm,TRUE,&refusal,LGR_PIXELS_STACK));unchanged();
 refused(LGR_LEASE_DRIVER);assert(refusal.lgr_Driver==HIDD_PIXELS_REFUSE_SCANOUT&&refusal.lgr_Value[0]==HIDD_SCANOUT_NOT_OWNED&&!strcmp((char*)refusal.lgr_Class,"hidd.bitmap.efifb"));
 /* A lease without a record refuses identically and writes nothing. */
 init(0,0);refuse=1;assert(!PreparePixels(&bm,TRUE,NULL,LGR_PIXELS_STACK));unchanged();
 for(int i=0;i<7;++i){init(0,0);switch(i){
 case 0:fixture.stride=1;break;case 1:fixture.size=0;break;case 2:fixture.red=0x101;break;
 case 3:fixture.blue=fixture.red;break;case 4:fixture.presentationSize=1;break;
 case 5:fixture.height=0xffffffff;break;case 6:fixture.cursorBackup=backup;fixture.cursorARGB=argb;fixture.cursorX=6;fixture.cursorW=2;fixture.cursorH=2;break;}
 assert(!PreparePixels(&bm,TRUE,&refusal,LGR_PIXELS_STACK));unchanged();refused(LGR_LEASE_MAP);}
 init(0,0);bm.supported=0;assert(!PreparePixels(&bm,TRUE,&refusal,LGR_PIXELS_STACK));unchanged();refused(LGR_LEASE_NOT_HIDD);bm.supported=1;
 printf("preparedpixels production ASan/UBSan PASS allocations=%d formats=32,30 cursor=hidden,drawn directcommit=allocator-dispatch-forbidden\n",total);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='aros-preparedpixels-') as temp:
    unit=Path(temp)/'test.c';binary=Path(temp)/'test'
    unit.write_text(preamble+refusal_header+cursor_header+map_header+mock+source+renderer+tests)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-but-set-variable','-Wno-bitwise-op-parentheses','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    mutations={
        'proposed-hidden-not-rendered':source.replace('if(p->oldCursor || p->proposedCursor)', 'if(p->oldCursor)'),
        'proposed-argument-lost':source.replace('&p->map, p->stage, p->proposedCursor}', '&p->map, p->stage, NULL}'),
        'old-saveunder-overwritten':source.replace('if(p->newCursor && !p->proposedCursor)', 'if(p->newCursor)'),
        'duplicate-binding':source.replace('p->sealed || p->proposedCursor || !proposed', 'p->sealed || !proposed'),
        'driver-cause-dropped':source.replace('r->lgr_Driver = m->refusal;','(void)m;'),
        'range-unrecorded':source.replace('return Refuse(p->refusal, p->kind, LGR_LEASE_RANGE, x, y, w, h);\n    if (staged','return FALSE;\n    if (staged'),
    }
    for name,mutated in mutations.items():
        assert mutated!=source,name
        unit.write_text(preamble+refusal_header+cursor_header+map_header+mock+mutated+renderer+tests)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-but-set-variable','-Wno-bitwise-op-parentheses','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
        run=subprocess.run([str(binary)],capture_output=True,text=True)
        assert run.returncode!=0,name+' escaped detection'
        print('REJECT '+name)

# Exercise the actual opt-in HIDD methods too: unknown subclasses, unbounded
# storage, unsupported presentation and failed cursor delegation must not mutate.
chunky=(root/'rom/hidds/gfx/gfx_chunkybitmapclass.c').read_text()
efi=(root/'rom/hidds/efifbgfx/efifbgfx_bitmapclass.c').read_text()
cursor=(root/'rom/hidds/gfx/gfx_cursorfbclass.c').read_text()
chunky=chunky[chunky.index('static BOOL cbm_refuse'):]
efi=efi[efi.index('static BOOL efifb_refuse'):]
cursor=cursor[cursor.index('static BOOL cursorfb_isdisplay'):cursor.index('/* Default: forward')]
driver_preamble=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t UBYTE;typedef uint16_t UWORD;typedef uint32_t ULONG;
typedef intptr_t IPTR;typedef int64_t QUAD;typedef int BOOL;typedef int32_t LONG;typedef void VOID;typedef void *APTR;typedef const char *CONST_STRPTR;
#define CLID_Hidd_BitMap_EFIFB "hidd.bitmap.efifb"
#define TRUE 1
#define FALSE 0
struct Library { int dummy; };struct SignalSemaphore { int count; };
typedef struct OOP_Class { APTR instance, owner; struct OOP_Class *superclass; } OOP_Class;
typedef struct OOP_Object { OOP_Class *cl; } OOP_Object;
typedef APTR OOP_Msg;
#define OOP_OCLASS(o) ((o)->cl)
#define OOP_INST_DATA(cl,o) ((void)(o),(cl)->instance)
#define CSD(cl) ((struct class_static_data *)(cl)->owner)
#define XSD(cl) ((struct EFIFBGfx_staticdata *)(cl)->owner)
#define MNAME_BM(n) EFI_##n
#define IID_Hidd_BitMap "bitmap"
#define moHidd_BitMap_ReleasePixels 2
#define vHidd_ColorModel_TrueColor 1
#define LOCK_FRAMEBUFFER(x) ObtainSemaphore(&(x)->framebufferlock)
#define UNLOCK_FRAMEBUFFER(x) ReleaseSemaphore(&(x)->framebufferlock)
struct class_static_data { struct Library *cs_OOPBase;OOP_Class *displayclass; };
struct chunkybm_data { UBYTE *buffer;ULONG bytesperrow,bytesperpixel;BOOL own_buffer;ULONG bufsize; };
struct HWData { UBYTE *framebuffer;ULONG width,height,bytesperline,bytesperpixel,redmask,greenmask,bluemask,fbsize;BOOL owned;APTR present; };
struct EFIFBGfx_staticdata { struct HWData data;struct SignalSemaphore framebufferlock; };
struct EFIFBGfxBitMapData { UBYTE *VideoData;ULONG bytesperpix,width,height,bytesperline;OOP_Object *pixfmtobj;BOOL disp;LONG xoffset,yoffset; };
struct CursorFBData { OOP_Object *realbm,*display; };
struct HIDDDisplayData { struct HIDD_CursorState cursor; };
static struct SignalSemaphore cursorsem;
static struct SignalSemaphore *GfxDisplay_CursorSem(struct class_static_data *c,OOP_Object *o) {(void)c;(void)o;return &cursorsem;}
static void ObtainSemaphore(struct SignalSemaphore *s) {++s->count;}
static void ReleaseSemaphore(struct SignalSemaphore *s) {assert(s->count);--s->count;}
#define CFB_INIT struct Library *OOPBase=CSD(cl)->cs_OOPBase; \
 struct CursorFBData *data=OOP_INST_DATA(cl,o);struct class_static_data *csd=CSD(cl); \
 struct SignalSemaphore *sem=GfxDisplay_CursorSem(csd,data->display);BOOL inside=FALSE;IPTR retval;ObtainSemaphore(sem);
enum { aHidd_BitMap_PixFmt=1,aHidd_PixFmt_ColorModel,aHidd_PixFmt_SwapPixelBytes,
 aHidd_BitMap_Width,aHidd_BitMap_Height,aHidd_PixFmt_RedMask,aHidd_PixFmt_GreenMask,
 aHidd_PixFmt_BlueMask,aHidd_PixFmt_AlphaMask,aHidd_ChunkyBM_StorageSize,
 aHidd_BitMap_Display,aHidd_BitMap_BytesPerRow,aHidd_ChunkyBM_Buffer,aHidd_BitMap_LeftEdge,aHidd_BitMap_TopEdge };
static IPTR attrs[16];
static void OOP_GetAttr(OOP_Object *o,ULONG a,IPTR *p) {(void)o;*p=attrs[a];}
static ULONG OOP_GetMethodID(const char *name,ULONG id){(void)name;return id;}
'''
driver_preamble=driver_preamble.replace('struct HIDDDisplayData {',cursor_header+'\n#define CopyMem(s,d,n) memcpy(d,s,n)\nstruct HIDDDisplayData {')
driver_proto=r'''
#define TAG_DONE 0
#define CLID_Hidd_ChunkyBM "chunky"
struct TagItem { ULONG ti_Tag;IPTR ti_Data; };
static int new_fail,temporary_live,rendered,proposed_render;
static UBYTE *expected_argb;
static OOP_Object temporary_object;
static OOP_Object *OOP_NewObject(OOP_Class *cl,const char *id,struct TagItem *tags) {
 (void)cl;(void)id;assert(tags[0].ti_Tag==aHidd_BitMap_Display);
 assert(tags[1].ti_Tag==aHidd_BitMap_PixFmt);assert(tags[2].ti_Data==8);
 assert(tags[3].ti_Data==4);assert(tags[4].ti_Data==32);
 assert(tags[5].ti_Tag==aHidd_ChunkyBM_Buffer&&tags[5].ti_Data);
 if(new_fail)return NULL;++temporary_live;return &temporary_object;
}
static void OOP_DisposeObject(OOP_Object *o) {assert(o==&temporary_object&&temporary_live==1);--temporary_live;}
static void HIDD_BM_PutAlphaImage(OOP_Object *o,APTR gc,UBYTE *argb,ULONG stride,ULONG x,ULONG y,ULONG w,ULONG h) {
 (void)gc;assert(o==&temporary_object&&temporary_live==1&&argb);
 if(proposed_render){assert(argb==expected_argb&&stride==12&&x==0&&y==0&&w==2&&h==2);}
 else{assert(stride==8&&x==1&&y==1&&w==2&&h==2);}++rendered;
}
struct pHidd_BitMap_PreparePixels { ULONG mID;struct HIDD_PixelMap *map; };
struct pHidd_BitMap_ReleasePixels { ULONG mID;struct HIDD_PixelMap *map; };
struct pHidd_BitMap_RenderPreparedCursor { ULONG mID;struct HIDD_PixelMap *map;UBYTE *stage;struct HIDD_CursorState *proposed; };
static int delegate_fail;
static ULONG OOP_DoMethod(OOP_Object *o,OOP_Msg msg) {
 struct pHidd_BitMap_PreparePixels *p=msg;(void)o;
 if(p->mID==2) {p->map->driverCookie=NULL;return 0;}
 if(delegate_fail)return 0;p->map->driverCookie=o;return 1;
}
'''
driver_tests=r'''
int main(void) {
 UBYTE storage[128],presentation[128],backup[16],image[16];
 struct HIDD_PixelMap map;
 struct chunkybm_data cb={storage,32,4,TRUE,128};
 OOP_Class cbcl={&cb,NULL},sub={&cb,NULL};OOP_Object cbobj={&cbcl},pf={NULL};
 struct class_static_data csd={0};cbcl.owner=&csd;
 attrs[aHidd_BitMap_PixFmt]=(IPTR)&pf;attrs[aHidd_PixFmt_ColorModel]=1;
 attrs[aHidd_BitMap_Width]=8;attrs[aHidd_BitMap_Height]=4;
 attrs[aHidd_PixFmt_RedMask]=0xff0000;attrs[aHidd_PixFmt_GreenMask]=0xff00;
 attrs[aHidd_PixFmt_BlueMask]=0xff;attrs[aHidd_ChunkyBM_StorageSize]=128;
 struct pHidd_BitMap_PreparePixels p={1,&map};struct pHidd_BitMap_ReleasePixels r={2,&map};
 memset(&map,0,sizeof(map));assert(CBM__Hidd_BitMap__PreparePixels(&cbcl,&cbobj,&p));
 assert(map.pixels==storage&&map.size==128);CBM__Hidd_BitMap__ReleasePixels(&cbcl,&cbobj,&r);
 cbobj.cl=&sub;assert(!CBM__Hidd_BitMap__PreparePixels(&cbcl,&cbobj,&p));cbobj.cl=&cbcl;
 assert(map.refusal==HIDD_PIXELS_REFUSE_SUBCLASS&&!strcmp(map.refusalClass,"chunky"));
 cb.own_buffer=FALSE;assert(!CBM__Hidd_BitMap__PreparePixels(&cbcl,&cbobj,&p));cb.own_buffer=TRUE;
 assert(map.refusal==HIDD_PIXELS_REFUSE_STORAGE_SIZE&&map.refusalValue[0]==0&&map.refusalValue[1]==128);
 cb.bufsize=127;assert(!CBM__Hidd_BitMap__PreparePixels(&cbcl,&cbobj,&p));cb.bufsize=128;
 assert(map.refusal==HIDD_PIXELS_REFUSE_STORAGE_SIZE&&map.refusalValue[0]==127);
 attrs[aHidd_PixFmt_SwapPixelBytes]=1;assert(!CBM__Hidd_BitMap__PreparePixels(&cbcl,&cbobj,&p));attrs[aHidd_PixFmt_SwapPixelBytes]=0;
 assert(map.refusal==HIDD_PIXELS_REFUSE_SWAPPED);
 cb.bytesperpixel=3;assert(!CBM__Hidd_BitMap__PreparePixels(&cbcl,&cbobj,&p));cb.bytesperpixel=4;
 assert(map.refusal==HIDD_PIXELS_REFUSE_STORAGE&&map.refusalValue[0]==3&&map.refusalValue[1]==1);
 attrs[aHidd_PixFmt_ColorModel]=2;assert(!CBM__Hidd_BitMap__PreparePixels(&cbcl,&cbobj,&p));attrs[aHidd_PixFmt_ColorModel]=1;
 assert(map.refusal==HIDD_PIXELS_REFUSE_COLORMODEL&&map.refusalValue[0]==2);
 struct EFIFBGfx_staticdata x={0};x.data=(struct HWData){presentation,8,4,32,4,0xff0000,0xff00,0xff,128,TRUE,NULL};
 struct EFIFBGfxBitMapData e={storage,4,8,4,32,&pf,TRUE,0,0};
 OOP_Class ec={&e,&x};OOP_Object eo={&ec};
 /* Each scanout difference is named with the scanout's own value. */
 static const LONG efi_cause[][3]={{HIDD_PIXELS_REFUSE_SCANOUT,HIDD_SCANOUT_PANNED,1},{HIDD_PIXELS_REFUSE_SCANOUT,HIDD_SCANOUT_CALLBACK,0},
  {HIDD_PIXELS_REFUSE_SCANOUT,HIDD_SCANOUT_NOT_OWNED,0},{HIDD_PIXELS_REFUSE_SCANOUT,HIDD_SCANOUT_PITCH,32},{HIDD_PIXELS_REFUSE_SCANOUT,HIDD_SCANOUT_MASKS,0},
  {HIDD_PIXELS_REFUSE_STORAGE_SIZE,127,128},{HIDD_PIXELS_REFUSE_SUBCLASS,0,0},{HIDD_PIXELS_REFUSE_SCANOUT,HIDD_SCANOUT_WIDTH,9},
  {HIDD_PIXELS_REFUSE_SCANOUT,HIDD_SCANOUT_HEIGHT,5},{HIDD_PIXELS_REFUSE_SCANOUT,HIDD_SCANOUT_DEPTH,3},{HIDD_PIXELS_REFUSE_SCANOUT,HIDD_SCANOUT_NO_FRAMEBUFFER,0}};
 for(int i=0;i<11;++i){memset(&map,0,sizeof(map));switch(i){
 case 0:e.xoffset=1;break;case 1:x.data.present=storage;break;case 2:x.data.owned=FALSE;break;
 case 3:x.data.fbsize=127;break;case 4:x.data.redmask=0;break;
 case 5:attrs[aHidd_ChunkyBM_StorageSize]=127;break;case 6:eo.cl=&sub;break;
 case 7:x.data.width=9;break;case 8:x.data.height=5;break;case 9:x.data.bytesperpixel=3;break;case 10:x.data.framebuffer=NULL;break;}
 assert(!EFI_PreparePixels(&ec,&eo,&p));assert(!x.framebufferlock.count);
 assert((LONG)map.refusal==efi_cause[i][0]&&map.refusalValue[0]==efi_cause[i][1]&&map.refusalValue[1]==efi_cause[i][2]&&!strcmp(map.refusalClass,"hidd.bitmap.efifb"));
 x.data.width=8;x.data.height=4;x.data.bytesperpixel=4;x.data.framebuffer=presentation;
 e.xoffset=0;x.data.present=NULL;x.data.owned=TRUE;x.data.fbsize=128;
 x.data.redmask=0xff0000;attrs[aHidd_ChunkyBM_StorageSize]=128;eo.cl=&ec;}
 memset(&map,0,sizeof(map));assert(EFI_PreparePixels(&ec,&eo,&p));assert(x.framebufferlock.count==1);
 assert(map.presentation==presentation);EFI_ReleasePixels(&ec,&eo,&r);assert(!x.framebufferlock.count);
 struct HIDDDisplayData d={.cursor={.cursor_drawn=TRUE,.cursor_bm=&cbobj,.cursor_backup_bpp=4,.cursor_backup=backup,.cursor_argb=image,.cursor_backupX=1,.cursor_backupY=1,.cursor_drawX=1,.cursor_drawY=1,.cursor_backupW=2,.cursor_backupH=2,.cursor_w=2,.cursor_h=2}};
 OOP_Class dc={&d,NULL};OOP_Object display={&dc};csd.displayclass=&dc;
 struct CursorFBData c={&cbobj,&display};OOP_Class cc={&c,&csd};OOP_Object co={&cc};
 /* The wrapped driver's own cause survives the CursorFB forward. */
 delegate_fail=1;memset(&map,0,sizeof(map));map.refusal=HIDD_PIXELS_REFUSE_SCANOUT;assert(!cursorfb_preparepixels(&cc,&co,&p));assert(!cursorsem.count);
 assert(map.refusal==HIDD_PIXELS_REFUSE_SCANOUT&&!map.refusalClass);
 delegate_fail=0;d.cursor.cursor_backup_bpp=3;assert(!cursorfb_preparepixels(&cc,&co,&p));assert(!cursorsem.count);
 assert(map.refusal==HIDD_PIXELS_REFUSE_CURSOR&&map.refusalValue[0]==3&&map.refusalValue[1]==1&&!strcmp(map.refusalClass,"CursorFB"));
 d.cursor.cursor_backup_bpp=4;assert(cursorfb_preparepixels(&cc,&co,&p));assert(cursorsem.count==1);
 assert(map.cursorBackup==backup&&map.cursorARGB==image&&map.cursorW==2);
 map.width=8;map.height=4;map.stride=32;
 struct pHidd_BitMap_RenderPreparedCursor render={3,&map,storage,NULL};
 render.map=NULL;assert(!cursorfb_renderprepared(&cc,&co,&render));render.map=&map;
 new_fail=1;assert(!cursorfb_renderprepared(&cc,&co,&render));assert(!temporary_live&&!rendered);
 assert(map.refusal==HIDD_PIXELS_REFUSE_CURSOR_RENDER&&map.refusalValue[0]==HIDD_CURSOR_REFUSE_RENDERER);
 new_fail=0;assert(cursorfb_renderprepared(&cc,&co,&render));assert(!temporary_live&&rendered==1);
 UBYTE proposed_argb[36]={0},proposed_backup[36]={0};
 struct HIDD_CursorState proposed={.cursor_w=3,.cursor_h=3,.cursor_argb=proposed_argb,.cursor_backup=proposed_backup,.cursor_backup_size=36,.cursor_xoffset=-1,.cursor_yoffset=-1};
 d.cursor.cursor_visible=TRUE;d.cursor.cursor_mouseX=d.cursor.cursor_mouseY=0;
 struct HIDD_CursorState untouched=d.cursor;
 render.proposed=&proposed;proposed_render=1;expected_argb=proposed_argb+16;
 assert(cursorfb_renderprepared(&cc,&co,&render));assert(!memcmp(&untouched,&d.cursor,sizeof untouched));
 assert(proposed.cursor_drawn&&proposed.cursor_drawX==-1&&proposed.cursor_drawY==-1&&proposed.cursor_backupW==2&&proposed.cursor_backupH==2&&proposed.cursor_backupX==0&&proposed.cursor_backupY==0);
 assert(!memcmp(proposed_backup,storage,8)&&!memcmp(proposed_backup+8,storage+32,8));
 proposed.cursor_xoffset=INT32_MAX;proposed.cursor_yoffset=INT32_MIN;d.cursor.cursor_mouseX=20;
 assert(cursorfb_renderprepared(&cc,&co,&render)&&!proposed.cursor_drawn&&!proposed.cursor_backupW);
 assert(d.cursor.cursor_argb==image&&d.cursor.cursor_xoffset==untouched.cursor_xoffset);
 proposed.cursor_xoffset=0;proposed.cursor_yoffset=0;proposed.cursor_backup=backup;
 assert(!cursorfb_renderprepared(&cc,&co,&render));
 assert(map.refusal==HIDD_PIXELS_REFUSE_CURSOR_RENDER&&map.refusalValue[0]==HIDD_CURSOR_REFUSE_ALIAS);
 cursorfb_releasepixels(&cc,&co,&r);assert(!cursorsem.count&&!map.cursorState);
 /* Every driver owns a Display subclass (efifbgfx on the M1): the owner is
  * admitted through the superclass chain; a class outside it refuses. */
 OOP_Class display_subclass={&d,NULL,&dc};display.cl=&display_subclass;
 memset(&map,0,sizeof(map));assert(cursorfb_preparepixels(&cc,&co,&p)&&cursorsem.count==1&&map.cursorState);
 cursorfb_releasepixels(&cc,&co,&r);assert(!cursorsem.count&&!map.cursorState);
 OOP_Class foreign={&d,NULL,NULL};display.cl=&foreign;
 memset(&map,0,sizeof(map));assert(!cursorfb_preparepixels(&cc,&co,&p)&&!cursorsem.count);
 assert(map.refusal==HIDD_PIXELS_REFUSE_DISPLAY&&!strcmp(map.refusalClass,"CursorFB"));
 puts("preparedpixels actual HIDD capability/refusal/lock-balance/proposed-clip-detachment ASan/UBSan PASS");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='aros-preparedpixels-driver-') as temp:
    unit=Path(temp)/'test.c';binary=Path(temp)/'test'
    unit.write_text(driver_preamble+map_header+driver_proto+chunky+efi+cursor+driver_tests)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-missing-field-initializers','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
