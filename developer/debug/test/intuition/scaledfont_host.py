#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Run the production bitmap scaler/constructor/CleanMem under ASan and UBSan.
The harness supplies OS allocation/list/graphics services, not a second scaler.
"""
from pathlib import Path
import os, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[4]
lib = ROOT / "workbench/libs/diskfont"
constructor = (lib / "newscaleddiskfont.c").read_text().split("AROS_LIBFUNC_INIT", 1)[1].split("AROS_LIBFUNC_EXIT", 1)[0]
cleanup = (lib / "diskfont_init.c").read_text().split("struct DiskFontHeader *dfh, *dfh2;", 1)[1].split("AROS_USERFUNC_EXIT", 1)[0]
prefix = r'''#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef uint16_t UWORD;
typedef int16_t WORD; typedef uint8_t UBYTE; typedef int BOOL; typedef void *BPTR;
typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define FSF_COLORFONT 64
#define FSF_TAGGED 128
#define FSF_UNDERLINED 1
#define FPF_ROMFONT 1
#define FPF_DISKFONT 2
#define FPF_DESIGNED 64
#define FPF_REMOVED 128
#define TE0F_NOREMFONT 1
#define MEMF_ANY 0
#define MEMF_CLEAR 0
#define DFH_ID 0x0f80
#define MEM_DID_NOTHING 0
#define MEM_ALL_DONE 1
#define D(x)
struct Node {struct Node *next, *prev; char *ln_Name;};
struct Message {struct Node mn_Node; void *mn_ReplyPort;};
struct TextFont {struct Message tf_Message; UWORD tf_YSize; UBYTE tf_Style,tf_Flags;
UWORD tf_XSize,tf_Baseline,tf_BoldSmear,tf_Accessors; UBYTE tf_LoChar,tf_HiChar;
void *tf_CharData; UWORD tf_Modulo; void *tf_CharLoc,*tf_CharSpace,*tf_CharKern;};
#define tf_Extension tf_Message.mn_ReplyPort
struct TextFontExtension {UBYTE tfe_Flags0;};
struct TextAttr {char *ta_Name; UWORD ta_YSize; UBYTE ta_Style,ta_Flags;};
struct DiskFontHeader {struct Node dfh_DF; UWORD dfh_FileID,dfh_Revision;
BPTR dfh_Segment; char dfh_Name[32]; struct TextFont dfh_TF;};
struct DiskfontBase {struct Node diskfontlist; void *gfxfontsem;};
struct Library {int unused;};
static struct DiskfontBase base_storage, *LIBBASE=&base_storage;
static int allocations,fail_alloc,fail_extend,extensions,font_count;
static void *AllocSegment(void *prev, ULONG n, ULONG flags, struct DiskfontBase *base) {
(void)prev;(void)flags;(void)base; if(fail_alloc)return NULL;
void *p=calloc(1,n); if(p)allocations++; return p;}
#define MAKE_REAL_SEGMENT(p) ((void *)(p))
static void UnLoadSeg(void *p) {assert(p); allocations--;free(p);}
static char *FilePart(const char *p) {const char *s=p,*it;for(it=p;*it;it++)if(*it==':'||*it=='/')s=it+1;return (char *)s;}
static void *ExtendFont(struct TextFont *f,void *tags) {(void)tags;if(fail_extend)return NULL;
f->tf_Extension=calloc(1,sizeof(struct TextFontExtension));extensions++;return f->tf_Extension;}
static void StripFont(struct TextFont *f) {assert(f->tf_Extension);free(f->tf_Extension);f->tf_Extension=NULL;extensions--;}
static void ObtainSemaphore(void *s) {(void)s;}
static void ReleaseSemaphore(void *s) {(void)s;}
static int AttemptSemaphore(void *s) {(void)s;return 1;}
static void AddFont(struct TextFont *f) {f->tf_Accessors=0;font_count++;}
static void append(struct Node *head,struct Node *n) {n->next=head;n->prev=head->prev;head->prev->next=n;head->prev=n;}
static void remove_node(struct Node *n) {n->prev->next=n->next;n->next->prev=n->prev;}
#define ADDTAIL(l,n) append((struct Node *)(l),(struct Node *)(n))
#define REMOVE(n) remove_node((struct Node *)(n))
/* The public font node is not part of the managed header list. */
static void unlink_font(struct Node *n) {(void)n;font_count--;}
#define ForeachNodeSafe(l,p,q) for(p=(void *)(l)->next,q=(void *)p->dfh_DF.next; &p->dfh_DF!=(l); p=q,q=(void *)q->dfh_DF.next)
static void CloseFont(struct TextFont *f) {assert(f->tf_Accessors);f->tf_Accessors--;
assert(((struct TextFontExtension *)f->tf_Extension)->tfe_Flags0&TE0F_NOREMFONT);}
'''
# Keep the actual cleanup branching/segment release, with the graphics-list
# unlink represented separately from the managed diskfont list in this harness.
cleanup=cleanup.replace("REMOVE(&dfh->dfh_TF.tf_Message.mn_Node);", "unlink_font(&dfh->dfh_TF.tf_Message.mn_Node);")
main=r'''
static int bit(struct TextFont *f,int x,int y) {return !!(((UBYTE *)f->tf_CharData)[y*f->tf_Modulo+x/8]&(128>>(x%8)));}
int main(void) {
base_storage.diskfontlist.next=base_storage.diskfontlist.prev=&base_storage.diskfontlist;
ULONG loc[]={2, (2UL<<16)|1}; WORD space[]={3,2},kern[]={-1,0};
UBYTE data[]={0xa0,0x40};
struct TextFont source={0},plan={0}; source.tf_YSize=2;source.tf_XSize=3;source.tf_Baseline=1;
source.tf_Flags=FPF_ROMFONT|FPF_DISKFONT|FPF_DESIGNED;
source.tf_BoldSmear=1;source.tf_LoChar=source.tf_HiChar=65;source.tf_Modulo=1;
source.tf_Message.mn_Node.ln_Name="FONTS:arial.font";
source.tf_CharData=data;source.tf_CharLoc=loc;source.tf_CharSpace=space;source.tf_CharKern=kern;
struct TextAttr attr={"arial.font",4,0,0};
for(int round=0;round<100;round++) {
struct DiskFontHeader *d=construct(&source,&attr,(struct Library *)LIBBASE);assert(d);
struct TextFont *f=&d->dfh_TF; assert(f->tf_YSize==4&&f->tf_XSize==6&&f->tf_Baseline==2);
assert(f->tf_Accessors==1&&!strcmp(d->dfh_Name,"arial.font"));
assert(!(f->tf_Flags&(FPF_ROMFONT|FPF_DISKFONT|FPF_DESIGNED|FPF_REMOVED)));
assert(((WORD *)f->tf_CharSpace)[0]==6&&((WORD *)f->tf_CharKern)[0]==-2);
for(int y=0;y<4;y++)for(int x=0;x<4;x++)assert(bit(f,x,y)==bit(&source,x/2,y/2));
assert(((ULONG *)f->tf_CharLoc)[1]==((4UL<<16)|2));
assert(clean()==MEM_ALL_DONE&&allocations==1); /* open font survives pressure */
CloseFont(f);assert(clean()==MEM_ALL_DONE);assert(!allocations&&!extensions&&!font_count);
}
attr.ta_YSize=3; struct DiskFontHeader *d=construct(&source,&attr,(struct Library *)LIBBASE);assert(d);
assert(d->dfh_TF.tf_YSize==3&&((WORD *)d->dfh_TF.tf_CharKern)[0]==-2);
for(int y=0;y<3;y++)for(int x=0;x<3;x++)assert(bit(&d->dfh_TF,x,y)==bit(&source,x*2/3,y*2/3));
CloseFont(&d->dfh_TF);clean();
source.tf_CharKern=source.tf_CharSpace=NULL;attr.ta_YSize=1;
d=construct(&source,&attr,(struct Library *)LIBBASE);assert(d&&d->dfh_TF.tf_Baseline==0);
CloseFont(&d->dfh_TF);clean();
attr.ta_YSize=0;assert(!construct(&source,&attr,(struct Library *)LIBBASE));
attr.ta_YSize=4;source.tf_Style=FSF_COLORFONT;assert(!construct(&source,&attr,(struct Library *)LIBBASE));source.tf_Style=0;
loc[0]=(8UL<<16)|1;assert(!ScaleBitmapFont(&source,4,&plan,NULL));loc[0]=2;
source.tf_YSize=0;assert(!ScaleBitmapFont(&source,4,&plan,NULL));source.tf_YSize=2;
source.tf_HiChar=64;assert(!ScaleBitmapFont(&source,4,&plan,NULL));source.tf_HiChar=65;
source.tf_CharSpace=space;space[0]=32767;assert(!ScaleBitmapFont(&source,4,&plan,NULL));space[0]=3;
source.tf_YSize=1;source.tf_Baseline=0;source.tf_XSize=65535;
assert(!ScaleBitmapFont(&source,65535,&plan,NULL));source.tf_YSize=2;source.tf_Baseline=1;source.tf_XSize=3;
source.tf_Message.mn_Node.ln_Name="this-font-name-is-too-long-for-the-diskfont-header.font";
assert(!construct(&source,&attr,(struct Library *)LIBBASE));source.tf_Message.mn_Node.ln_Name="arial.font";
source.tf_CharKern=kern;kern[0]=-32768;assert(!ScaleBitmapFont(&source,4,&plan,NULL));kern[0]=-1;
source.tf_Baseline=2;assert(!ScaleBitmapFont(&source,4,&plan,NULL));source.tf_Baseline=1;
fail_alloc=1;assert(!construct(&source,&attr,(struct Library *)LIBBASE));fail_alloc=0;
fail_extend=1;assert(!construct(&source,&attr,(struct Library *)LIBBASE));fail_extend=0;
attr.ta_Style=2;assert(!construct(&source,&attr,(struct Library *)LIBBASE));
assert(!allocations&&!extensions&&!font_count);puts("PASS: production glyphs 2x/1.5x/downscale, signed metrics, bounds, allocation failure, 100 open/close/CleanMem cycles");
}
'''
with tempfile.TemporaryDirectory(prefix="aros-scaledfont-") as temp:
    path=Path(temp)
    code=prefix+(lib/"scaledfont_bitmap.h").read_text()+"\nstatic struct DiskFontHeader *construct(struct TextFont *sourceFont, struct TextAttr *destTextAttr, struct Library *DiskfontBase) {"+constructor+"}\nstatic int clean(void) {struct DiskFontHeader *dfh,*dfh2;"+cleanup+"}\n"+main
    (path/"test.c").write_text(code)
    subprocess.run([os.environ.get("CC","clang"),"-std=c11","-g","-O1","-fsanitize=address,undefined","-fno-sanitize-recover=all",str(path/"test.c"),"-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test")],check=True)
