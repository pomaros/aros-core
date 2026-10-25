#!/usr/bin/env python3
# SPDX-License-Identifier: APL-1.1
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
"""Exercise actual console font preparation with external OS boundaries stubbed.

This is a resource/metric contract test. It does not render a real glyph, run
Intuition or prove a Shell screenshot. Use the native/VM application runs for
those observations.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess


def function(source, name):
    match = re.search(r"(?:static\s+)?(?:struct TextFont \*|BOOL |LONG |VOID |void )" + name + r"\([^;]*?\)\s*\{", source, re.S)
    if not match:
        raise ValueError(name)
    start = match.start()
    pos = source.index("{", match.start())
    depth = 1
    end = pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREAMBLE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG; typedef uint16_t UWORD; typedef int16_t WORD;
typedef void VOID; typedef void *Msg;
typedef intptr_t IPTR; typedef int32_t LONG; typedef int BOOL;
typedef void *PLANEPTR; typedef void Object; typedef void Class;
#define bug(...) fprintf(stderr,__VA_ARGS__)
#define TRUE 1
#define FALSE 0
#define FSF_TAGGED 128
#define FPF_DESIGNED 64
#define FPF_ROMFONT 1
#define FPF_DISKFONT 2
#define FPF_REMOVED 128
#define NT_PROCESS 13
#define SA_Scale 1234
#define WFLG_GIMMEZEROZERO 64
struct Library { int dummy; };
struct TextFont {
 struct { struct { char *ln_Name; } mn_Node; } tf_Message;
 UWORD tf_XSize, tf_YSize, tf_Baseline, tf_Style, tf_Flags;
 void *tf_Extension;
};
struct TextFontExtension { void *tfe_Tags; };
struct TextAttr { char *ta_Name; UWORD ta_YSize; unsigned char ta_Style, ta_Flags; };
struct TTextAttr { char *tta_Name; UWORD tta_YSize; unsigned char tta_Style, tta_Flags; void *tta_Tags; };
struct DiskFontHeader { struct TextFont dfh_TF; };
struct GfxBase { struct Library library; struct TextFont *DefaultFont; };
struct RastPort { struct TextFont *Font; };
struct Window { LONG Flags, BorderLeft, BorderTop, BorderRight, BorderBottom, Width, Height; void *WScreen; struct RastPort *RPort; };
struct ConUnit { struct Window *window; struct TextFont *cu_Font;
 LONG cu_XRSize, cu_YRSize, cu_TxWidth, cu_TxHeight, cu_TxBaseline, cu_XROrigin, cu_YROrigin;
 WORD xmax,ymax,xcp,ycp; };
#define CU(o) ((struct ConUnit *)(o))
#define WINDOW(o) CU(o)->window
#define RASTPORT(o) WINDOW(o)->RPort
struct stdcondata { struct Library *scd_GfxBase; BOOL scaleaware; ULONG scale;
 struct TextFont *basefont, *scaledfont; PLANEPTR charScratch; UWORD charScratchHeight,charScratchWidth; WORD rendercursorcount; };
static struct DiskFontHeader fonts[2][512];
static unsigned refs[2][512], forbid_depth, opens, raster_live, diskopens, bitmapopens;
static int memory_mode, disk_mode, library_available=1, fail_raster;
static unsigned geometry_updates, cursor_nesting;
static struct stdcondata *active_data;
#define INST_DATA(cl,o) active_data
static void Console_UnRenderCursor(Object *o) { cursor_nesting++; }
static void Console_RenderCursor(Object *o) { assert(cursor_nesting); cursor_nesting--; }
struct P_Console_NewWindowSize { ULONG MethodID; };
static void stdcon_newwindowsize(Class *,Object *,struct P_Console_NewWindowSize *);
static void Console_NewWindowSize(Object *o) {
 struct P_Console_NewWindowSize msg={0};stdcon_newwindowsize(NULL,o,&msg);
}
#define CHAR_XMAX(o) CU(o)->xmax
#define CHAR_YMAX(o) CU(o)->ymax
#define XCP CU(o)->xcp
#define YCP CU(o)->ycp
#define GFX_XMIN(o) CU(o)->cu_XROrigin
#define GFX_YMIN(o) CU(o)->cu_YROrigin
#define GFX_XMAX(o) (GFX_XMIN(o)+(CU(o)->xmax+1)*CU(o)->cu_XRSize-1)
#define GFX_YMAX(o) (GFX_YMIN(o)+(CU(o)->ymax+1)*CU(o)->cu_YRSize-1)
#define JAM2 2
#define SetAPen(rp,pen) ((void)GfxBase,set_pen(rp,pen))
static void set_pen(struct RastPort *rp,int pen) { (void)rp;(void)pen; }
static void SetDrMd(struct RastPort *rp,int mode) { (void)rp;(void)mode; }
static int record_rects, rectcount, rectangles[4][4];
static void RectFill(struct RastPort *rp,int x1,int y1,int x2,int y2) {
 (void)rp;assert(x1<=x2&&y1<=y2);
 if(record_rects) {assert(rectcount<4);int v[4]={x1,y1,x2,y2};memcpy(rectangles[rectcount++],v,sizeof v);}
}
static void DoSuperMethodA(Class *cl,Object *o,Msg msg) { geometry_updates++; }
static unsigned fail_open_ordinal;
struct Task { struct { unsigned ln_Type; } tc_Node; };
static struct Task current_task={.tc_Node={NT_PROCESS}};
static struct Task *FindTask(void *unused) { return &current_task; }
static IPTR requested_scale=100;
static struct GfxBase gfx;
static struct Library disk;
static struct TextFontExtension ext;
static void *tags=(void *)(uintptr_t)0x1234;
static struct TextFont *face(unsigned family, unsigned height) {
 struct TextFont *f=&fonts[family][height].dfh_TF;
 if (!f->tf_YSize) { f->tf_YSize=height; f->tf_XSize=height/2; f->tf_Baseline=height-3;
  f->tf_Message.mn_Node.ln_Name=family?"B":"A"; f->tf_Style=FSF_TAGGED; f->tf_Extension=&ext; }
 return f;
}
static unsigned family(struct TextFont *f) { return f->tf_Message.mn_Node.ln_Name[0]=='B'; }
static struct TextFont *hold(unsigned fam,unsigned height) { refs[fam][height]++;return face(fam,height); }
static struct TextFont *OpenFont(struct TextAttr *plain) {
 struct TTextAttr *a=(void *)plain; unsigned fam=a->tta_Name[0]=='B';
 assert(!forbid_depth);
 assert(!(a->tta_Flags & (FPF_DESIGNED|FPF_ROMFONT|FPF_DISKFONT|FPF_REMOVED)));
 assert(!!(a->tta_Style&FSF_TAGGED)==!!a->tta_Tags);
 if(a->tta_Tags) assert(a->tta_Tags==tags);
 if (++opens==fail_open_ordinal) return NULL;
 return hold(fam, memory_mode?14:a->tta_YSize);
}
static void CloseFont(struct TextFont *f) { unsigned fam=family(f);assert(refs[fam][f->tf_YSize]);refs[fam][f->tf_YSize]--; }
static struct Library *OpenLibrary(const char *name, unsigned version) {
 (void)version;assert(!forbid_depth);assert(!strcmp(name,"diskfont.library"));diskopens++;
 return library_available?&disk:NULL;
}
static void CloseLibrary(struct Library *base) { assert(base==&disk); }
static struct TextFont *OpenDiskFont(struct TextAttr *plain) {
 struct TTextAttr *a=(void *)plain;
 return disk_mode==2?NULL:hold(a->tta_Name[0]=='B',disk_mode==1?14:a->tta_YSize);
}
static struct DiskFontHeader *NewScaledDiskFont(struct TextFont *base,struct TextAttr *plain) {
 struct TTextAttr *a=(void *)plain; bitmapopens++;
 struct TextFont *f=hold(family(base),a->tta_YSize);
 return (void *)f;
}
struct Raster { unsigned width,height; unsigned char pixels[]; };
static PLANEPTR AllocRaster(unsigned width,unsigned height) {
 if(fail_raster) return NULL;
 struct Raster *r=calloc(1,sizeof(*r)+height*2);assert(r);r->width=width;r->height=height;raster_live++;return r;
}
static void FreeRaster(PLANEPTR p,unsigned width,unsigned height) {
 struct Raster *r=p;assert(r->width==width&&r->height==height);assert(raster_live);raster_live--;free(r);
}
static BOOL GetAttr(unsigned attr,Object *screen,IPTR *value) {
 assert(attr==SA_Scale);assert(screen);*value=requested_scale;return TRUE;
}
static void SetFont(struct RastPort *rp,struct TextFont *font) { rp->Font=font; }
'''

TEST = r'''
static void cleanup(struct stdcondata *d) {
 if(d->basefont) CloseFont(d->basefont);
 if(d->scaledfont) CloseFont(d->scaledfont);
 if(d->charScratch) FreeRaster(d->charScratch,d->charScratchWidth,d->charScratchHeight);
 memset(d,0,sizeof(*d));
 assert(!raster_live&&!forbid_depth);
 for(unsigned f=0;f<2;f++)for(unsigned h=0;h<512;h++)assert(!refs[f][h]);
}
static void test_font(void) {
 struct RastPort rp={0};struct Window w={.BorderLeft=4,.BorderTop=19,.BorderRight=4,.BorderBottom=4,.Width=640,.Height=480,.RPort=&rp,.WScreen=&w};
 struct ConUnit unit={.window=&w,.cu_XRSize=7,.cu_YRSize=14};Class *cl=NULL;
 struct stdcondata d={.scd_GfxBase=(void *)&gfx,.scaleaware=TRUE};
 gfx.DefaultFont=face(0,14);ext.tfe_Tags=tags;
 struct stdcondata task_before=d;
 unsigned oldopens=opens,olddiskopens=diskopens;
 current_task.tc_Node.ln_Type=1;
 assert(!stdcon_scalefont(cl,&d,&unit));
 assert(opens==oldopens&&diskopens==olddiskopens&&!raster_live);
 assert(!memcmp(&task_before,&d,sizeof(d)));
 active_data=&d;
 assert(!stdcon_updatescale(cl,&unit));
 assert(geometry_updates==1&&!cursor_nesting&&opens==oldopens);
 struct P_Console_NewWindowSize task_size={0};
 stdcon_newwindowsize(cl,&unit,&task_size);
 assert(geometry_updates==2&&!cursor_nesting&&opens==oldopens);
 current_task.tc_Node.ln_Type=NT_PROCESS;
 gfx.DefaultFont->tf_Flags=FPF_DESIGNED|FPF_ROMFONT|FPF_DISKFONT|FPF_REMOVED;
 unsigned scales[]={100,150,200,300,400,100};
 for(unsigned i=0;i<sizeof(scales)/sizeof(scales[0]);i++) {
  requested_scale=scales[i];assert(stdcon_scalefont(cl,&d,&unit));
  assert(d.basefont==face(0,14));assert(d.scaledfont->tf_YSize==14*scales[i]/100);
  assert(rp.Font==d.scaledfont&&unit.cu_Font==rp.Font);
  assert(unit.cu_TxHeight==rp.Font->tf_YSize&&unit.cu_YRSize==rp.Font->tf_YSize);
  assert(unit.cu_TxBaseline==rp.Font->tf_Baseline&&unit.cu_XROrigin==4&&unit.cu_YROrigin==19);
  assert(raster_live==1);
 }
 /* Same scale, same face is a no-op with balanced temporary ownership. */
 struct stdcondata before=d;struct ConUnit previous=unit;
 assert(stdcon_scalefont(cl,&d,&unit));assert(!memcmp(&before,&d,sizeof(d)));
 w.BorderTop=31;assert(stdcon_scalefont(cl,&d,&unit));assert(unit.cu_YROrigin==31);
 assert(!memcmp(&before,&d,sizeof(d)));w.BorderTop=19;
 assert(stdcon_scalefont(cl,&d,&unit));assert(unit.cu_YROrigin==19);
 gfx.DefaultFont=face(1,16);assert(stdcon_scalefont(cl,&d,&unit));assert(d.basefont==face(1,16));
 assert(unit.cu_YRSize==16);cleanup(&d);
 /* Exact disk face, then bitmap fallback. Both discard nearest memory faces. */
 gfx.DefaultFont=face(0,14);requested_scale=200;memory_mode=1;disk_mode=0;
 d=(struct stdcondata){.scd_GfxBase=(void *)&gfx,.scaleaware=TRUE};
 assert(stdcon_scalefont(cl,&d,&unit));assert(d.scaledfont->tf_YSize==28);assert(diskopens);cleanup(&d);
 disk_mode=1;assert(stdcon_scalefont(cl,&d,&unit)); /* cleared instance is legacy */
 d=(struct stdcondata){.scd_GfxBase=(void *)&gfx,.scaleaware=TRUE};
 assert(stdcon_scalefont(cl,&d,&unit));assert(bitmapopens&&d.scaledfont->tf_YSize==28);
 /* Resource failures must retain the complete previously installed state. */
 before=d;previous=unit;struct TextFont *oldrp=rp.Font;requested_scale=300;fail_raster=1;
 assert(!stdcon_scalefont(cl,&d,&unit));assert(!memcmp(&before,&d,sizeof(d)));
 assert(!memcmp(&previous,&unit,sizeof(unit))&&rp.Font==oldrp&&raster_live==1);
 active_data=&d;
 unsigned oldgeometry=geometry_updates;
 assert(!stdcon_updatescale(cl,&unit));
 assert(geometry_updates==oldgeometry+1&&!cursor_nesting);
 assert(!memcmp(&before,&d,sizeof(d)));
 fail_raster=0;library_available=0;
 assert(!stdcon_scalefont(cl,&d,&unit));assert(!memcmp(&before,&d,sizeof(d)));
 library_available=1;memory_mode=0;disk_mode=0;
 fail_open_ordinal=opens+1;assert(!stdcon_scalefont(cl,&d,&unit));assert(!memcmp(&before,&d,sizeof(d)));
 fail_open_ordinal=0;requested_scale=125;assert(stdcon_scalefont(cl,&d,&unit));assert(unit.cu_YRSize==18);
 w.Flags=WFLG_GIMMEZEROZERO;requested_scale=150;assert(stdcon_scalefont(cl,&d,&unit));assert(!unit.cu_XROrigin&&!unit.cu_YROrigin);
 before=d;previous=unit;oldrp=rp.Font;w.Width=1;requested_scale=200;
 assert(!stdcon_scalefont(cl,&d,&unit));assert(!memcmp(&before,&d,sizeof(d)));
 assert(!memcmp(&previous,&unit,sizeof(unit))&&rp.Font==oldrp);w.Width=640;
 cleanup(&d);
 /* Untagged default fonts carry no extension pointer. */
 gfx.DefaultFont=face(0,14);gfx.DefaultFont->tf_Style=0;
 requested_scale=200;
 d=(struct stdcondata){.scd_GfxBase=(void *)&gfx,.scaleaware=TRUE};
 assert(stdcon_scalefont(cl,&d,&unit));cleanup(&d);
 gfx.DefaultFont->tf_Style=FSF_TAGGED;
 /* Initial open survives a failed scaled-face request. The window owns
  * the existing font; the unit retries with independently owned resources. */
 rp.Font=face(0,14);requested_scale=200;fail_open_ordinal=opens+1;
 d=(struct stdcondata){.scd_GfxBase=(void *)&gfx,.scaleaware=TRUE};
 stdcon_initialfont(cl,&d,&unit);
 assert(d.scaleaware&&!d.scaledfont&&!d.basefont&&rp.Font==face(0,14));
 assert(d.charScratchWidth==7&&d.charScratchHeight==14&&raster_live==1);
 fail_open_ordinal=0;active_data=&d;
 assert(stdcon_updatescale(cl,&unit)&&d.scaledfont->tf_YSize==28);
 cleanup(&d);
 /* A missing optional scratch plane also leaves a usable existing face. */
 rp.Font=face(0,14);fail_raster=1;
 d=(struct stdcondata){.scd_GfxBase=(void *)&gfx,.scaleaware=TRUE};
 stdcon_initialfont(cl,&d,&unit);
 assert(!d.charScratch&&!d.scaledfont&&rp.Font==face(0,14));
 fail_raster=0;cleanup(&d);
 /* Clear only the unused partial-cell strips in both RP coordinate modes. */
 w=(struct Window){.BorderLeft=4,.BorderTop=10,.BorderRight=4,.BorderBottom=4,.Width=40,.Height=50,.RPort=&rp,.WScreen=&w};
 unit=(struct ConUnit){.window=&w,.cu_XRSize=7,.cu_YRSize=14,.xmax=3,.ymax=1};
 d=(struct stdcondata){.scd_GfxBase=(void *)&gfx,.scaleaware=TRUE};active_data=&d;
 current_task.tc_Node.ln_Type=1;record_rects=1;rectcount=0;
 stdcon_newwindowsize(cl,&unit,&task_size);
 int expected[2][4]={{32,10,35,45},{4,38,35,45}};
 assert(rectcount==2&&!memcmp(rectangles,expected,sizeof expected));
 w.Flags=WFLG_GIMMEZEROZERO;rectcount=0;stdcon_newwindowsize(cl,&unit,&task_size);
 int gzzexpected[2][4]={{28,0,31,35},{0,28,31,35}};
 assert(rectcount==2&&!memcmp(rectangles,gzzexpected,sizeof gzzexpected));
 record_rects=0;current_task.tc_Node.ln_Type=NT_PROCESS;
 /* Malformed percentages cannot allocate or alter the caller's live state. */
 unsigned invalid[]={0,99,101,175+1,401,0xffffffffu};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)assert(!stdcon_openfont((void *)&gfx,face(0,14),invalid[i]));
 puts("PASS actual console font prepare: six scales/reset, independent14pxbase, tagged attrs, same-scale font replacement, disk/bitmap fallback, unchanged-on-failure, refs/scratch balanced");
}
int main(void) { test_font();return 0; }
'''

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[4])
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    path=args.source/'rom/devs/console/stdconclass.c'
    source=path.read_text(encoding='latin1')
    actual='\n\n'.join(function(source,n) for n in ['stdcon_openfont','stdcon_scalefont','stdcon_initialfont','stdcon_newwindowsize','stdcon_updatescale'])
    unit=args.output/'font-fixture.c'
    unit.write_text(PREAMBLE+'\n'+actual+'\n'+TEST)
    command=['clang','-O1','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(unit),'-o',str(args.output/'font-fixture')]
    subprocess.run(command,check=True)
    subprocess.run([str((args.output/'font-fixture').resolve())],check=True)
    mutations={
        'unbalanced-base-reference': ('if (oldbasefont) CloseFont(oldbasefont);','(void)oldbasefont;'),
        'stale-same-scale-font': ('data->scale == scale && data->basefont == basefont','data->scale == scale'),
        'wrong-rounding': ('* scale + 50','* scale'),
        'missing-conunit-metrics': ('CU(o)->cu_YRSize = font->tf_YSize;',''),
        'failed-font-skips-geometry': ('    Console_NewWindowSize(o);', '    if (prepared) Console_NewWindowSize(o);'),
        'initial-failure-skips-fallback': ('    if (data->scaleaware)\n        bug(', '    if (data->scaleaware) return;\n    if (data->scaleaware)\n        bug('),
        'partial-column-not-cleared': ('x2 = CU(o)->cu_XROrigin + WINDOW(o)->Width -\n            WINDOW(o)->BorderLeft - WINDOW(o)->BorderRight - 1;', 'x2 = GFX_XMAX(o);'),
        'task-opens-font': ('if (FindTask(NULL)->tc_Node.ln_Type != NT_PROCESS) return FALSE;', '(void)FindTask(NULL);'),
        'designed-font-request': ('attr.tta_Flags = base->tf_Flags &\n        ~(FPF_DESIGNED | FPF_ROMFONT | FPF_DISKFONT | FPF_REMOVED);', 'attr.tta_Flags = base->tf_Flags;'),
        'scratch-failure-leaks-font': ('CloseFont(font);\n        CloseFont(basefont);','CloseFont(basefont);'),
    }
    for name,(old,new) in mutations.items():
        assert old in actual,name
        mutated=args.output/(name+'.c')
        mutated.write_text(PREAMBLE+'\n'+actual.replace(old,new,1)+'\n'+TEST)
        mutant_command=command[:-3]+[str(mutated),'-o',str(args.output/name)]
        subprocess.run(mutant_command,check=True)
        with (args.output/(name+'.log')).open('w') as log:
            run=subprocess.run([str((args.output/name).resolve())],stdout=log,stderr=subprocess.STDOUT)
        if run.returncode==0: raise AssertionError('survived '+name)
        print('REJECT',name)
    (args.output/'font-source.json').write_text(json.dumps({'source':str(path.resolve()),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'functions':['stdcon_openfont','stdcon_scalefont','stdcon_initialfont','stdcon_newwindowsize','stdcon_updatescale'],'command':command},indent=2)+'\n')

if __name__=='__main__':
    main()
