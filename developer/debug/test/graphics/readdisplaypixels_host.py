#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Sanitize actual display readback, CursorFB and EFI mapping bodies."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[4]

def function(text, name):
    start = text.index(name + '(')
    start = text.rfind('\n', 0, start) + 1
    brace = text.index('{', start)
    n, end = 1, brace + 1
    while n:
        n += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

PRELUDE=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t UBYTE;typedef uint32_t ULONG;typedef int32_t LONG;typedef uintptr_t IPTR;typedef int BOOL;typedef void VOID;typedef void *APTR;typedef const char *CONST_STRPTR;
#define CLID_Hidd_BitMap_EFIFB "hidd.bitmap.efifb"
#define TRUE 1
#define FALSE 0
#define CHECK(x) do{if(!(x)){fprintf(stderr,"CHECK %d %s\n",__LINE__,#x);abort();}}while(0)
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
struct GfxBase {int unused;};
struct HIDD_CursorState {BOOL cursor_drawn;void *cursor_bm;UBYTE *cursor_backup,*cursor_argb;LONG cursor_backupX,cursor_backupY,cursor_drawX,cursor_drawY,cursor_backupW,cursor_backupH,cursor_w,cursor_h,cursor_backup_bpp;};
'''
ADAPTER=r'''
typedef void OOP_Object;typedef void *OOP_Msg;typedef struct OOP_Class {struct OOP_Class *superclass;} OOP_Class;
struct Library {int unused;};struct SignalSemaphore {int unused;};
struct CursorFBData {OOP_Object *realbm,*display;};
struct HIDDDisplayData {struct HIDD_CursorState cursor;};
struct class_static_data {struct Library *cs_OOPBase;OOP_Class *displayclass;};
struct HWData {UBYTE *framebuffer;void *present;BOOL owned;ULONG width,height,bytesperpixel,redmask,greenmask,bluemask,bytesperline,fbsize;};
struct EFIFBGfx_staticdata {struct HWData data;};
struct EFIFBGfxBitMapData {UBYTE *VideoData;ULONG bytesperpix,width,height,bytesperline;void *pixfmtobj;BOOL disp;LONG xoffset,yoffset;};
struct pHidd_BitMap_PreparePixels {IPTR id;struct HIDD_PixelMap *map;};
struct pHidd_BitMap_ReleasePixels {IPTR id;struct HIDD_PixelMap *map;};
struct pHidd_BitMap_GetImage {IPTR id;LONG x,y;ULONG width,height;UBYTE *pixels;};
/* As on the M1: the Display owner is a driver subclass (EFIFBGfxDisplay) of
   the generic Display class, never an instance of exactly that class. */
static OOP_Class wrapperclass,displayclass,eficlass,foreignclass,efidisplayclass={&displayclass},*displayowner;
static struct CursorFBData wrapper;
static struct HIDDDisplayData display;
static struct EFIFBGfxBitMapData efi;
static struct EFIFBGfx_staticdata efi_fixture;
static struct class_static_data csd_fixture;
static struct SignalSemaphore cursorsem;
static ULONG cursorlocks,fblocks,prepares,releases,removes,renders,unsupported;
static UBYTE backing[96],scanout[112],saved[112];
static ULONG masks[4],swap,color=1,storage=96;
struct BitMap {BOOL hidd;OOP_Object *object;};
#define IS_HIDD_BM(b) ((b)->hidd)
#define HIDD_BM_OBJ(b) ((b)->object)
#define OOP_INST_DATA(cl,o) ((void *)(o))
#define CSD(cl) (&csd_fixture)
#define XSD(cl) (&efi_fixture)
#define OOP_OCLASS(o) ((o)==&wrapper?&wrapperclass:(o)==&display?displayowner:&eficlass)
#define LOCK_FRAMEBUFFER(x) do{CHECK(cursorlocks==1&&!fblocks);++fblocks;}while(0)
#define UNLOCK_FRAMEBUFFER(x) do{CHECK(cursorlocks==1&&fblocks==1);--fblocks;}while(0)
#define MNAME_BM(x) EFI##x
#define IID_Hidd_BitMap 1
#define moHidd_BitMap_PreparePixels 1
#define moHidd_BitMap_ReleasePixels 2
#define moHidd_BitMap_GetImage 3
#define OOP_GetMethodID(i,m) (m)
enum {aHidd_PixFmt_ColorModel=1,aHidd_PixFmt_SwapPixelBytes,aHidd_ChunkyBM_StorageSize,aHidd_PixFmt_RedMask,aHidd_PixFmt_GreenMask,aHidd_PixFmt_BlueMask,aHidd_PixFmt_AlphaMask};
#define vHidd_ColorModel_TrueColor 1
static VOID OOP_GetAttr(void *o,ULONG attr,IPTR *v) {(void)o;if(attr==aHidd_PixFmt_ColorModel)*v=color;else if(attr==aHidd_PixFmt_SwapPixelBytes)*v=swap;else if(attr==aHidd_ChunkyBM_StorageSize)*v=storage;else *v=masks[attr-aHidd_PixFmt_RedMask];}
static void ObtainSemaphore(struct SignalSemaphore *s){CHECK(s==&cursorsem&&!cursorlocks&&!fblocks);++cursorlocks;}
static void ReleaseSemaphore(struct SignalSemaphore *s){CHECK(s==&cursorsem&&cursorlocks==1&&!fblocks);--cursorlocks;}
static struct SignalSemaphore *GfxDisplay_CursorSem(struct class_static_data *c,void *d){(void)c;CHECK(d==&display);return &cursorsem;}
static BOOL GfxDisplay_CursorIntersects(struct class_static_data *c,void *d,void *b,LONG l,LONG t,LONG r,LONG z){(void)c;(void)d;(void)b;(void)l;(void)t;(void)r;(void)z;CHECK(cursorlocks==1);return display.cursor.cursor_drawn;}
static void GfxDisplay_CursorRemove(struct class_static_data *c,void *d){(void)c;(void)d;CHECK(cursorlocks==1&&!fblocks);++removes;memset(scanout+32,0,8);display.cursor.cursor_drawn=FALSE;}
static void GfxDisplay_CursorRender(struct class_static_data *c,void *d){(void)c;(void)d;CHECK(cursorlocks==1&&!fblocks);++renders;memcpy(scanout+32,saved+32,8);display.cursor.cursor_drawn=TRUE;}
static IPTR OOP_DoMethod(OOP_Object *,OOP_Msg);
'''
DISPATCH=r'''
static IPTR OOP_DoMethod(OOP_Object *o,OOP_Msg raw){
 IPTR id=*(IPTR *)raw;
 if(o==&wrapper){if(id==1)return cursorfb_preparepixels(&wrapperclass,o,raw);if(id==2)return cursorfb_releasepixels(&wrapperclass,o,raw);if(id==3)return cursorfb_getimage(&wrapperclass,o,raw);}
 CHECK(o==&efi&&cursorlocks==1);
 if(id==1){++prepares;if(unsupported)return FALSE;return EFIPreparePixels(&eficlass,o,raw);}
 if(id==2){++releases;EFIReleasePixels(&eficlass,o,raw);return 0;}
 if(id==3){struct pHidd_BitMap_GetImage *m=raw;CHECK(!display.cursor.cursor_drawn&&!fblocks);memcpy(m->pixels,scanout,m->width*4);return TRUE;}
 abort();
}
static BOOL HIDD_BM_PreparePixels(void *o,struct HIDD_PixelMap *m){struct pHidd_BitMap_PreparePixels p={1,m};return OOP_DoMethod(o,&p);}
static void HIDD_BM_ReleasePixels(void *o,struct HIDD_PixelMap *m){struct pHidd_BitMap_ReleasePixels p={2,m};OOP_DoMethod(o,&p);}
'''
DRIVER=r'''
static void reset(ULONG bits){
 memset(&efi,0,sizeof efi);memset(&efi_fixture,0,sizeof efi_fixture);memset(&display,0,sizeof display);
 memset(backing,0x35,sizeof backing);memset(scanout,0xA7,sizeof scanout);
 masks[0]=bits==10?0x3ff00000:0x00ff0000;masks[1]=bits==10?0x000ffc00:0x0000ff00;masks[2]=bits==10?0x000003ff:0x000000ff;masks[3]=0;
 efi.VideoData=backing;efi.bytesperpix=4;efi.width=4;efi.height=3;efi.bytesperline=32;efi.pixfmtobj=(void*)1;efi.disp=TRUE;
 efi_fixture.data=(struct HWData){.framebuffer=scanout,.owned=TRUE,.width=4,.height=3,.bytesperpixel=4,.redmask=masks[0],.greenmask=masks[1],.bluemask=masks[2],.bytesperline=36,.fbsize=108};
 for(ULONG y=0;y<3;y++)for(ULONG x=0;x<4;x++){ULONG p=masks[(x+y)%3];memcpy(scanout+y*36+x*4,&p,4);}
 ULONG gray=0;for(ULONG c=0;c<3;c++){ULONG sh=0,m=masks[c];while(!(m&1)){++sh;m>>=1;}gray|=(m/2)<<sh;}memcpy(scanout+2*36+3*4,&gray,4);
 memcpy(saved,scanout,sizeof saved);display.cursor=(struct HIDD_CursorState){.cursor_drawn=TRUE,.cursor_bm=&efi,.cursor_backup=backing,.cursor_argb=backing,.cursor_backupX=1,.cursor_backupY=1,.cursor_drawX=1,.cursor_drawY=1,.cursor_backupW=2,.cursor_backupH=2,.cursor_w=2,.cursor_h=2,.cursor_backup_bpp=4};
 wrapper=(struct CursorFBData){&efi,&display};csd_fixture.displayclass=&displayclass;displayowner=&efidisplayclass;cursorlocks=fblocks=prepares=releases=removes=renders=unsupported=0;swap=0;color=1;storage=96;
}
int main(void){
 struct GfxBase base={0};struct BitMap bitmap={TRUE,&wrapper};UBYTE rgb[64],before[64];
 for(ULONG bits=8;bits<=10;bits+=2){reset(bits);struct HIDD_CursorState original=display.cursor;memset(rgb,0xE1,sizeof rgb);
 CHECK(ReadDisplayPixels(&bitmap,0,0,4,3,rgb,16,48,&base));CHECK(prepares==1&&releases==1&&!cursorlocks&&!fblocks&&!removes&&!renders);
 for(ULONG y=0;y<3;y++){for(ULONG x=0;x<4;x++)for(ULONG c=0;c<3;c++)CHECK(rgb[y*16+x*3+c]==((x==3&&y==2)?127:(c==(x+y)%3?255:0)));for(ULONG n=12;n<16;n++)CHECK(rgb[y*16+n]==0xE1);}
 CHECK(!memcmp(scanout,saved,sizeof scanout)&&!memcmp(&original,&display.cursor,sizeof original));for(ULONG n=0;n<sizeof backing;n++)CHECK(backing[n]==0x35);
 /* The ordinary exact GetImage body removes/re-renders; readback does neither. */
 struct pHidd_BitMap_GetImage get={3,0,0,2,1,rgb};CHECK(OOP_DoMethod(&wrapper,&get));CHECK(removes==1&&renders==1&&!cursorlocks&&!fblocks);
 reset(bits);memset(rgb,0xE1,sizeof rgb);CHECK(ReadDisplayPixels(&bitmap,1,1,2,2,rgb,8,16,&base));CHECK(rgb[0]==0&&rgb[1]==0&&rgb[2]==255&&rgb[3]==255&&rgb[4]==0&&rgb[5]==0);
 printf("actual readback RGB%u distinctscanout+paddedpitch+crop+cursorlocks PASS\n",bits);
 }
 /* Every admission failure leaves all destination bytes untouched and releases
    a successfully acquired mapping exactly once. */
 for(ULONG test=0;test<29;test++){
 reset(10);memset(rgb,0xE1,sizeof rgb);memcpy(before,rgb,sizeof rgb);LONG x=0,y=0;ULONG w=4,h=3,pitch=16,size=48;UBYTE *out=rgb;
 switch(test){case 0:unsupported=1;break;case 1:efi.disp=FALSE;break;case 2:efi_fixture.data.present=(void*)1;break;case 3:efi_fixture.data.owned=FALSE;break;case 4:efi.xoffset=1;break;case 5:efi_fixture.data.fbsize=10;break;case 6:efi_fixture.data.width=5;break;case 7:swap=1;break;case 8:color=2;break;case 9:storage=1;break;case 10:bitmap.hidd=FALSE;break;case 11:x=-1;break;case 12:y=3;break;case 13:w=5;break;case 14:h=4;break;case 15:pitch=11;break;case 16:size=47;break;case 17:w=0;break;case 18:masks[0]=masks[1];efi_fixture.data.redmask=masks[0];break;case 19:masks[0]=0x15;efi_fixture.data.redmask=masks[0];break;case 20:masks[0]=0;efi_fixture.data.redmask=0;break;case 21:out=scanout;break;case 22:out=backing;break;case 23:display.cursor.cursor_bm=(void*)2;break;case 24:display.cursor.cursor_bm=NULL;break;case 25:display.cursor.cursor_backup_bpp=3;break;case 26:display.cursor.cursor_argb=NULL;break;case 27:display.cursor.cursor_drawX=9;break;case 28:displayowner=&foreignclass;break;}
 if(ReadDisplayPixels(&bitmap,x,y,w,h,out,pitch,size,&base)){fprintf(stderr,"unexpected admission %u\n",test);abort();}CHECK(!memcmp(rgb,before,sizeof rgb)&&!cursorlocks&&!fblocks&&!removes&&!renders);CHECK(!memcmp(scanout,saved,sizeof scanout));
 ULONG expectedRelease=(test==1||test==12||test==13||(test>=18&&test!=28));if(releases!=expectedRelease){fprintf(stderr,"release case%u got%u expected%u\n",test,releases,expectedRelease);abort();}ULONG expectedPrepare=!(test==10||test==11||test==14||test==15||test==16||test==17||test==28);CHECK(prepares==expectedPrepare);bitmap.hidd=TRUE;
 }
 puts("29 refusal cases nooutputwrite exactrelease/no cursor mutation PASS");return 0;
}
'''

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path);args=ap.parse_args()
    # Sparse source tree may use the explicitly supplied full read-only source.
    full=Path(__import__('os').environ.get('AROS_READBACK_BASE',ROOT))
    def source(p):return (ROOT/p if (ROOT/p).exists() else full/p).read_text()
    raw=source('rom/graphics/readdisplaypixels.c')
    helpers=raw[raw.index('static BOOL display_channel'):raw.index('/****************************************************************************')]
    actual=raw[raw.index('    AROS_LIBFUNC_INIT',raw.index('AROS_LH8')):]
    actual='static BOOL ReadDisplayPixels(struct BitMap *bitmap,LONG x,LONG y,ULONG width,ULONG height,UBYTE *rgb,ULONG stride,ULONG size,struct GfxBase *GfxBase)\n{\n'+actual
    maptext=source('rom/hidds/gfx/include/preparedpixels.h');mapstruct=maptext[maptext.index('struct HIDD_PixelMap\n'):maptext.index('\n#endif')]
    cfb=source('rom/hidds/gfx/gfx_cursorfbclass.c');efi=source('rom/hidds/efifbgfx/efifbgfx_bitmapclass.c')
    macros=cfb[cfb.index('#define CFB_INIT'):cfb.index('/******************************************************************************/')]
    text=PRELUDE+mapstruct+ADAPTER+macros+'\n'+function(efi,'efifb_refuse')+'\n'+function(efi,'efifb_scanout')+'\n'+function(efi,'MNAME_BM(PreparePixels)')+'\n'+function(efi,'MNAME_BM(ReleasePixels)')+'\n'+function(cfb,'cursorfb_isdisplay')+'\n'+'#define CURSORFB_NAME "CursorFB"\n'+function(cfb,'cursorfb_refuse')+'\n'+function(cfb,'cursorfb_preparepixels')+'\n'+function(cfb,'cursorfb_releasepixels')+'\n'+function(cfb,'cursorfb_getimage')+'\n'+DISPATCH+helpers+actual+DRIVER
    with tempfile.TemporaryDirectory(prefix='displayread-') as temporary:
        out=args.out or Path(temporary);out.mkdir(parents=True,exist_ok=True)
        c=out/'actual.c';c.write_text(text);cmd=['clang','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(out/'actual')]
        r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr)
        if r.returncode:raise SystemExit(r.stderr)
        r=subprocess.run([str(out/'actual')],capture_output=True,text=True);(out/'run.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='')
        if r.returncode:raise SystemExit(r.returncode)
        mutations={'backing_instead_scanout':('const UBYTE *source = map.presentation +','const UBYTE *source = map.pixels +'),'ordinary_getimage_hides':('CFB_REMOVE(msg->x, msg->y, msg->x + msg->width - 1, msg->y + msg->height - 1)', '(void)msg;'),'missing_release':('HIDD_BM_ReleasePixels(object, &map);','(void)object;'),'cursor_removed':('object = HIDD_BM_OBJ(bitmap);','GfxDisplay_CursorRemove(&csd_fixture,&display); object = HIDD_BM_OBJ(bitmap);'),'pixel8_assumption':(' * 255) + rm / 2) / rm',' * 255) + 127) / 255'),'pitch_ignored':('row) * map.presentationStride','row) * (map.width * 4)'),'exact_display_class':('!cursorfb_isdisplay(csd, data->display)','OOP_OCLASS(data->display) != csd->displayclass'),'invisible_backing':('displayed = (IPTR)map.presentation;', 'if (!map.presentation) {map.presentation=map.pixels;map.presentationStride=map.stride;map.presentationSize=map.size;} displayed = (IPTR)map.presentation;')}
        rejected=[]
        for name,(old,new) in mutations.items():
            if old not in text:raise RuntimeError(name+' missing')
            c=out/(name+'.c');c.write_text(text.replace(old,new,1));m=cmd.copy();m[-3]=str(c);m[-1]=str(out/name)
            r=subprocess.run(m,capture_output=True,text=True)
            if r.returncode:raise SystemExit(r.stderr)
            r=subprocess.run([m[-1]],capture_output=True,text=True);(out/(name+'.log')).write_text(r.stdout+r.stderr)
            if not r.returncode:raise RuntimeError('survived '+name)
            rejected.append(name)
        (out/'result.json').write_text(json.dumps({'status':'PASS','scope':'actual graphics readback + CursorFB prepare/get/release + EFI mapping, OOP/Exec adapter; no native acceptance','rejected_mutations':rejected},indent=2)+'\n')
if __name__=='__main__':main()
