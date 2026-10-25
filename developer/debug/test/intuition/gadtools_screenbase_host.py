#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Actual complete GadTools test flow: content SysFont != logical screen font.

OS/window construction is modeled; actual physical compare and label functions
are extracted, so bad composition must fail actual byte readback comparison.
"""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[4]
text=(ROOT/'developer/debug/test/intuition/scalelegacy_gadtools.h').read_text()
def block(marker):
    start=text.index(marker); brace=text.index('{',start); end=brace+1; depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef uint32_t ULONG; typedef uint16_t UWORD; typedef uint8_t UBYTE; typedef intptr_t IPTR;
typedef int BOOL; typedef void *APTR; typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define TAG_DONE 0
#define SA_Scale 1
#define SA_RenderContext 2
#define MEMF_PUBLIC 0
#define RECTFMT_RGB 0
#define WA_CustomScreen 1
#define WA_Left 2
#define WA_Top 3
#define WA_Width 4
#define WA_Height 5
#define WA_Borderless 6
#define WA_ScaleAware 7
#define WA_NoCareRefresh 8
#define GT_ORACLE_WIDTH 112
#define GT_ORACLE_HEIGHT 88
struct TextFont {UWORD tf_YSize;}; struct TextAttr {UWORD ta_YSize;};
struct RastPort {struct TextFont *Font;}; struct DrawInfo {struct TextFont *dri_Font;};
struct Screen {int Width,Height;struct TextAttr *Font;struct RastPort RastPort;};
struct ScreenRenderContext{ULONG src_Scale;struct DrawInfo*src_LogicalDrawInfo,*src_PhysicalDrawInfo;};
static struct ScreenRenderContext context;
typedef struct Screen Object;
struct Window {int LeftEdge,TopEdge;struct RastPort *RPort;};struct Gadget {int unused;};
struct GTOracleClient {APTR visual;struct Gadget *list;struct Window *window;};
struct Library {int unused;};static struct Library library,*GadToolsBase;
static struct TextFont logicalfont,physicalfont,contentfont;
static struct DrawInfo logical,physical; static struct Screen *current;
static ULONG scale;static int double_public,double_composition,livewindows;
static char report_line[512];
static void report(const char *fmt,...) {va_list ap;va_start(ap,fmt);vsnprintf(report_line,sizeof report_line,fmt,ap);va_end(ap);}
static struct Library *OpenLibrary(CONST_STRPTR n,int version){(void)n;(void)version;return &library;}
static void CloseLibrary(struct Library *p){assert(p==&library);}
static void *AllocVec(size_t n,int flags){(void)flags;return calloc(1,n);}
static void FreeVec(void *p){free(p);}
static void Delay(int ticks){(void)ticks;}
static void SetAttrs(Object *s,int tag,int percent,int end){assert(tag==SA_Scale&&end==TAG_DONE);scale=percent;physicalfont.tf_YSize=logicalfont.tf_YSize*percent/100;s->Font->ta_YSize=logicalfont.tf_YSize*(double_public&&percent==200?2:1);}
static void GetAttr(int tag,Object*s,IPTR*out){(void)s;assert(tag==SA_RenderContext);context=(struct ScreenRenderContext){scale,&logical,&physical};*out=(IPTR)&context;}
static struct DrawInfo *GetScreenDrawInfo(struct Screen*s){assert(s==current);return &logical;}
static void FreeScreenDrawInfo(struct Screen*s,struct DrawInfo*d){assert(s==current&&d==&logical);}
static struct Window *newwindow(int left,struct TextFont *font){struct Window *w=calloc(1,sizeof *w);w->RPort=calloc(1,sizeof *w->RPort);w->LeftEdge=left;w->TopEdge=112;w->RPort->Font=font;livewindows++;return w;}
static void CloseWindow(struct Window*w){if(w){free(w->RPort);free(w);livewindows--;}}
static struct Window *OpenWindowTags(void *none,...){(void)none;return newwindow(320,&physicalfont);}
static BOOL gt_oracle_open(struct GTOracleClient*c,struct Screen*s,int left,UWORD base){assert(s==current&&base==logicalfont.tf_YSize);c->window=newwindow(left,&contentfont);return TRUE;}
static void gt_oracle_close(struct GTOracleClient*c){CloseWindow(c->window);}
static void SetRast(struct RastPort *rp,int pen){(void)rp;assert(pen==0);}
static void RefreshGList(struct Gadget *g,struct Window*w,void*req,int count){(void)g;(void)w;(void)req;assert(count==-1);}
static void GT_RefreshWindow(struct Window*w,void*req){(void)w;(void)req;}
static ULONG ReadPixelArray(UBYTE*out,int a,int b,int stride,struct RastPort*rp,int x,int y,int width,int height,int fmt){
 (void)a;(void)b;(void)fmt;assert(rp==&current->RastPort);
 int factor=scale/100,left=x>=320?160:16;
 if(double_composition&&factor==2)factor=4;
 for(int yy=0;yy<height;yy++)for(int xx=0;xx<width;xx++){
  int rx=(x+xx)/factor-left-8,ry=(y+yy)/factor-112-8;
  UBYTE pen=(rx>=25&&rx<50&&(ry==18||ry==58))?255:0;
  memset(out+yy*stride+xx*3,pen,3);
 }
 return width*height;
}
'''
functions='\n'.join(block(marker) for marker in ['static ULONG gt_oracle_compare(', 'static BOOL gt_oracle_label(', 'static BOOL gadtools_fonts('])
test=r'''
static void run(unsigned content,unsigned screenbase){
 struct TextAttr attr={screenbase};struct Screen screen={800,600,&attr,{&physicalfont}};current=&screen;
 contentfont.tf_YSize=content;logicalfont.tf_YSize=screenbase;physicalfont.tf_YSize=screenbase;logical.dri_Font=&logicalfont;physical.dri_Font=&physicalfont;scale=100;
 BOOL accepted=gadtools_fonts(&screen);
 assert(!livewindows&&!GadToolsBase);
 assert(accepted==!(double_public||double_composition));
 if(accepted){char expected[32];snprintf(expected,sizeof expected,"base-font=%u ",screenbase);assert(strstr(report_line,expected));assert(strstr(report_line,"cycles=2"));}
}
int main(void){
 run(8,12);run(14,13);
 double_public=1;run(8,12);run(14,13);double_public=0;
 double_composition=1;run(8,12);run(14,13);double_composition=0;
 puts("PASS actual GadTools flow separates SysFont8/screen12 and SysFont14/screen13; doubled public metadata and doubled composition rejected; cleanup balanced");
}
'''
with tempfile.TemporaryDirectory(prefix='aros-gadtools-screenbase-') as temporary:
    root=Path(temporary)
    for name,body in [('production',functions),('old_content_font_assumption',functions.replace('    base = public100;','    base = contentfont.tf_YSize;'))]:
        source=root/(name+'.c'); exe=root/name; source.write_text(pre+body+test)
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(source),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        if name=='production':
            assert result.returncode==0,result.stderr
            print(result.stdout,end='')
        else:
            assert body!=functions and result.returncode!=0,'old content-font assumption escaped detection'
            print('PASS negative control: original content-font equals screen-font assumption rejected')
