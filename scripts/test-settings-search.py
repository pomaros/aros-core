#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Run the production text highlight renderer with a recording graphics backend."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
engine = (root / 'workbench/libs/muimaster/textengine.c').read_text()
text = (root / 'workbench/libs/muimaster/classes/text.c').read_text()
draw = engine[engine.index('STRPTR zune_text_plain('):]
copy = text[text.index('static BOOL copy_highlight('):text.index('static void setup_text(')]
fixture = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef int16_t WORD;
typedef int BOOL; typedef char *STRPTR; typedef void *APTR;
#define TRUE 1
#define FALSE 0
#define MEMF_ANY 0
#define FS_NORMAL 0
#define JAM1 0
#define MPEN_FILL 3
#define FILLTEXTPEN 4
#define ZTL_CENTER 1
#define ZTL_RIGHT 2
#define MADF_DRAWOBJECT 0
#define D(x)
#define bug(...) 0
struct MinNode { struct MinNode *mln_Succ, *mln_Pred; };
struct MinList { struct MinNode *mlh_Head, *mlh_Tail, *mlh_TailPred; };
struct MUI_TextHighlightRange { ULONG start, length; };
struct MUI_TextHighlight { ULONG count; const struct MUI_TextHighlightRange *ranges; };
struct MUI_TextData { struct MUI_TextHighlight highlight; };
struct MUI_AlphaData { void *data; int width, height; };
struct RastPort { int pen; };
struct Font { int tf_Baseline; };
struct Dri { int dri_Pens[10]; };
typedef struct Object { int left, top, width, height; struct Font font; struct RastPort rp; int pens[10]; struct Dri dri; } Object;
#define _rp(o) (&(o)->rp)
#define _font(o) (&(o)->font)
#define _pens(o) ((o)->pens)
#define _dri(o) (&(o)->dri)
#define _left(o) ((o)->left)
#define _top(o) ((o)->top)
#define _width(o) ((o)->width)
#define _height(o) ((o)->height)
#define muiRenderInfo(o) (o)
typedef struct ZTextChunk { struct MinNode node; char *str; void *image; Object *obj; struct MUI_AlphaData *alpha_data; LONG dripen, pen; unsigned char style; WORD cwidth, cheight; } ZTextChunk;
typedef struct ZTextLine { struct MinNode node; unsigned char align; struct MinList chunklist; WORD lwidth,lheight; } ZTextLine;
typedef struct ZText { WORD width,height; struct MinList lines; } ZText;
static int boxes, rects[16][4];
static void *AllocVec(size_t n,int f) { return malloc(n); }
#define FreeVec free
#define CopyMem(a,b,n) memcpy(b,a,n)
static void SetFont(struct RastPort *r,struct Font *f) {}
static void SetSoftStyle(struct RastPort *r,int a,int b) {}
static int AskSoftStyle(struct RastPort *r) { return 255; }
static int GetAPen(struct RastPort *r) { return r->pen; }
static void SetAPen(struct RastPort *r,int p) { r->pen=p; }
static void SetDrMd(struct RastPort *r,int m) {}
static void SetABPenDrMd(struct RastPort *r,int p,int b,int m) { r->pen=p; }
static int TextLength(struct RastPort *r,char *s,int n) { return n*8; }
static void RectFill(struct RastPort *r,int x,int y,int xx,int yy) { assert(boxes<16); int v[]={x,y,xx,yy}; memcpy(rects[boxes++],v,sizeof v); }
static void Move(struct RastPort *r,int x,int y) {}
static void Text(struct RastPort *r,char *s,int n) {}
#define zune_imspec_show(a,b) ((void)0)
#define zune_imspec_hide(a) ((void)0)
#define zune_imspec_draw(...) ((void)0)
#define DoShowMethod(a) ((void)0)
#define DoHideMethod(a) ((void)0)
#define MUI_Redraw(a,b) ((void)0)
#define WritePixelArrayAlpha(...) ((void)0)
void zune_text_draw_highlight(ZText *,Object *,WORD,WORD,WORD,const struct MUI_TextHighlight *);
'''
checks = r'''
int main(void) {
 struct MinNode end={0}, chunkend={0};
 ZTextChunk b={.node={&chunkend,0},.str="cd",.cwidth=16,.cheight=10,.dripen=-1,.pen=-1};
 ZTextChunk a={.node={(struct MinNode*)&b,0},.str="ab",.cwidth=16,.cheight=10,.dripen=-1,.pen=-1};
 ZTextLine line={.node={&end,0},.chunklist={(struct MinNode*)&a,0,0},.lwidth=32,.lheight=10};
 ZText t={.lines={(struct MinNode*)&line,0,0}};
 Object obj={.font={8}};
 struct MUI_TextHighlightRange ranges[]={{1,2},{UINT32_MAX,UINT32_MAX}};
 struct MUI_TextHighlight h={2,ranges};
 struct MUI_TextData data={0};
 assert(copy_highlight(&data,&h)); ranges[0].start=0;
 assert(data.highlight.ranges[0].start==1);
 zune_text_draw_highlight(&t,&obj,10,99,20,&data.highlight);
 assert(boxes==2 && rects[0][0]==18 && rects[0][2]==25 && rects[1][0]==26 && rects[1][2]==33);
 assert(rects[0][1]==20 && rects[0][3]==29);
 char *plain=zune_text_plain(&t); assert(!strcmp(plain,"abcd")); free(plain);
 struct MUI_TextHighlight invalid={1,NULL};
 assert(!copy_highlight(&data,&invalid) && data.highlight.count==2);
 assert(copy_highlight(&data,NULL)); boxes=0;
 zune_text_draw_highlight(&t,&obj,10,99,20,&data.highlight); assert(boxes==0);
 ranges[0].start=0; ranges[0].length=UINT32_MAX; h.count=1;
 boxes=0; line.align=ZTL_RIGHT;
 zune_text_draw_highlight(&t,&obj,10,99,20,&h);
 assert(boxes==2 && rects[0][0]==68 && rects[1][2]==99);
 boxes=0; line.align=0;
 zune_text_draw_highlight(&t,&obj,10,21,20,&h);
 assert(boxes==1 && rects[0][0]==10 && rects[0][2]==21);
 puts("PASS range chunks, overflow, alignment, plain offsets, copy ownership and clear");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='settings-search-') as directory:
    source = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    source.write_text(fixture + copy + draw + checks)
    subprocess.run(['cc', '-std=c99', '-fsanitize=address,undefined', '-g', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

window = (root / 'workbench/prefs/settings/settingswindow.c').read_text()
record = window[window.index('struct index_record\n'):window.index('/* A page is')]
header = window[window.index('struct index_header\n'):window.index('static ULONG index_hash')]
reader = window[window.index('static BOOL cache_read('):window.index('static void cache_write(')]
cache_fixture = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint16_t UWORD; typedef uint32_t ULONG; typedef int32_t LONG;
typedef int BOOL; typedef int BPTR; typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define MODE_OLDFILE 0
#define INDEX_MAX 512
static unsigned char filedata[16384]; static size_t filesize, position;
static BPTR Open(const char *p,int mode) { position=0; return 1; }
static LONG Read(BPTR f,void *p,ULONG n) { if(n>filesize-position)n=filesize-position; memcpy(p,filedata+position,n); position+=n; return n; }
static void Close(BPTR f) {}
'''
cache_checks = r'''
int main(void) {
 struct index_record records[INDEX_MAX]={0};
 struct SettingsWindow_DATA data={records,0};
 struct index_header key={123, sizeof(records[0]), 41, 42, 43, 1}, disk=key;
 struct index_record record={.id=7,.name="Font",.help="Text size"};
 memcpy(filedata,&disk,sizeof disk); memcpy(filedata+sizeof disk,&record,sizeof record); filesize=sizeof disk+sizeof record;
 assert(cache_read("x",&key,&data,5) && data.swd_IndexCount==1 && records[0].row==5);
 ULONG *fields[]={&key.format,&key.record_size,&key.class_hash,&key.catalogue_hash,&key.language_hash};
 for(int i=0;i<5;i++) { (*fields[i])++; data.swd_IndexCount=0; assert(!cache_read("x",&key,&data,5) && !data.swd_IndexCount); (*fields[i])--; }
 filesize--; assert(!cache_read("x",&key,&data,5)); filesize++;
 memset(filedata+sizeof disk, 'a', sizeof record); assert(!cache_read("x",&key,&data,5));
 disk.count=INDEX_MAX+1; memcpy(filedata,&disk,sizeof disk); assert(!cache_read("x",&key,&data,5));
 puts("PASS cache identity invalidation, truncated/corrupt records, count bounds and row rebinding");
}
'''
with tempfile.TemporaryDirectory(prefix='settings-cache-') as directory:
    source = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    source.write_text(cache_fixture + record + header +
        'struct SettingsWindow_DATA { struct index_record *swd_Index; ULONG swd_IndexCount; };\n' + reader + cache_checks)
    subprocess.run(['cc', '-std=c99', '-fsanitize=address,undefined', '-g', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
