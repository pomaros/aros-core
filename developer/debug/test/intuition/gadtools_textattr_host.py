#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Actual GadTools text allocation/render/free bodies, ASan/UBSan regressions."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[4]
PRE=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef int BOOL;typedef uint32_t ULONG;typedef int32_t LONG;typedef uint16_t UWORD;typedef int16_t WORD;typedef uint8_t UBYTE;typedef char *STRPTR;typedef uintptr_t IPTR;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 0
#define MEMF_CLEAR 0
#define GT_Underscore 77
#define FSF_TAGGED 128
#define FSF_UNDERLINED 1
#define NG_HIGHLABEL 1
#define HIGHLIGHTTEXTPEN 0
#define TEXTPEN 1
#define BACKGROUNDPEN 2
#define JAM1 1
#define HIGH_COLOR 0
struct TagItem {IPTR ti_Tag,ti_Data;};
struct TextAttr {STRPTR ta_Name;UWORD ta_YSize;UBYTE ta_Style,ta_Flags;};
struct TTextAttr {STRPTR tta_Name;UWORD tta_YSize;UBYTE tta_Style,tta_Flags;struct TagItem *tta_Tags;};
struct TextFont {struct {struct {STRPTR ln_Name;} mn_Node;} tf_Message;UWORD tf_YSize;UBYTE tf_Style,tf_Flags;void *tf_Extension;};
struct TextFontExtension {struct TagItem *tfe_Tags;};
struct IntuiText {UBYTE FrontPen,BackPen,DrawMode;WORD LeftEdge,TopEdge;struct TextAttr *ITextFont;UBYTE *IText;struct IntuiText *NextText;};
struct DrawInfo {struct TextFont *dri_Font;UWORD *dri_Pens;};struct VisualInfo {struct DrawInfo *vi_dri;};
struct NewGadget {struct TextAttr *ng_TextAttr;void *ng_VisualInfo;STRPTR ng_GadgetText;int ng_Flags;};
struct RastPort {struct TextFont *Font;};struct GadToolsBase_intern {int unused;};
static int refs,allocs,tagallocs,failopen,failvec,failclone,rendering;
static struct TextFont opened;
static struct TagItem *nexttag(struct TagItem **state){if(!*state)return NULL;for(;;){struct TagItem *p=*state;switch(p->ti_Tag){case 0:*state=NULL;return NULL;case 1:break;case 2:*state=(struct TagItem*)p->ti_Data;if(!*state)return NULL;continue;case 3:*state+=p->ti_Data+1;continue;default:return (*state)++;}(*state)++;}}
static IPTR GetTagData(IPTR tag,IPTR def,struct TagItem *p){struct TagItem *q;while((q=nexttag(&p)))if(q->ti_Tag==tag)return q->ti_Data;return def;}
static struct TagItem *CloneTagItems(struct TagItem *p){if(failclone)return NULL;struct TagItem *state=p,*q;size_t n=0;while(nexttag(&state))n++;struct TagItem *out=calloc(n+1,sizeof(*out));assert(out);state=p;n=0;while((q=nexttag(&state)))out[n++]=*q;tagallocs++;return out;}
static void FreeTagItems(struct TagItem *p){assert(p&&tagallocs>0);tagallocs--;free(p);}
static void *AllocVec(size_t n,int flags){(void)flags;if(failvec)return NULL;void*p=calloc(1,n);if(p)allocs++;return p;}
static void FreeVec(void *p){assert(p&&allocs>0);allocs--;free(p);}
static struct TextFont *OpenFont(struct TextAttr *a){if(failopen)return NULL;if(a->ta_Style&FSF_TAGGED){struct TagItem *tags=((struct TTextAttr*)a)->tta_Tags;if(tags)assert(GetTagData(0x80000001,0,tags)==0x960096);}refs++;opened.tf_YSize=a->ta_YSize;return &opened;}
static void CloseFont(struct TextFont *f){(void)f;assert(refs>0);refs--;}
static void InitRastPort(struct RastPort *r){memset(r,0,sizeof(*r));}
static void SetFont(struct RastPort *r,struct TextFont *f){r->Font=f;}
static LONG TextLength(struct RastPort*r,const char*t,LONG n){(void)t;return r->Font->tf_YSize*n;}
static void SetSoftStyle(struct RastPort*r,int s,ULONG m){(void)r;(void)s;(void)m;}
static void SetABPenDrMd(struct RastPort*r,int a,int b,int d){(void)r;(void)a;(void)b;(void)d;}
#define CopyMem(a,b,n) memcpy(b,a,n)
'''
TEST=r'''
static void one(int explicit,int tagged,int underline){
struct TagItem *source=calloc(2,sizeof(*source));source[0].ti_Tag=0x80000001;source[0].ti_Data=0x960096;
struct TextFontExtension ext={source};struct TextFont base={.tf_Message={{"Arial.font"}},.tf_YSize=13,.tf_Style=tagged?FSF_TAGGED:0,.tf_Extension=&ext};
UWORD pens[]={1,2,3};struct DrawInfo dri={&base,pens};struct VisualInfo vi={&dri};struct TTextAttr attr={.tta_Name="Arial.font",.tta_YSize=13,.tta_Style=tagged?FSF_TAGGED:0,.tta_Tags=source};
struct TagItem under[]={{GT_Underscore,'_'},{0,0}};struct NewGadget ng={.ng_TextAttr=explicit?(struct TextAttr*)&attr:NULL,.ng_VisualInfo=&vi,.ng_GadgetText=underline?"_Go":"Go"};
struct IntuiText *it=makeitext(NULL,&ng,under);assert(it&&!refs&&allocs==1);assert(!!(it->ITextFont->ta_Style&FSF_TAGGED)==tagged);if(tagged)assert(((struct TTextAttr*)it->ITextFont)->tta_Tags!=source);free(source);rendering=1;
for(struct IntuiText *p=it;p;p=p->NextText){struct RastPort rp={&base};struct TextFont *old,*font=preparefont(NULL,&rp,p,&old);assert(font&&font->tf_YSize==13);closefont(NULL,&rp,font,old);}
rendering=0;assert(!refs);freeitext(NULL,it);assert(!allocs&&!tagallocs);}
int main(int argc,char **argv){(void)argv;if(argc>1){one(1,1,1);return 0;}for(int ex=0;ex<2;ex++)for(int tag=0;tag<2;tag++)for(int under=0;under<2;under++)one(ex,tag,under);
struct TagItem tags[]={{0x80000001,0x960096},{0,0}};struct TextFontExtension ext={tags};struct TextFont base={.tf_Message={{"Arial.font"}},.tf_YSize=13,.tf_Style=FSF_TAGGED,.tf_Extension=&ext};UWORD pens[]={1,2,3};struct DrawInfo dri={&base,pens};struct VisualInfo vi={&dri};struct TTextAttr attr={.tta_Name="Arial.font",.tta_YSize=13,.tta_Style=FSF_TAGGED,.tta_Tags=tags};struct NewGadget ng={.ng_TextAttr=(struct TextAttr*)&attr,.ng_VisualInfo=&vi,.ng_GadgetText="Go"};
failopen=1;assert(!makeitext(NULL,&ng,NULL)&&!refs&&!allocs&&!tagallocs);failopen=0;
failvec=1;assert(!makeitext(NULL,&ng,NULL)&&!refs&&!allocs&&!tagallocs);failvec=0;
failclone=1;assert(!makeitext(NULL,&ng,NULL)&&!refs&&!allocs&&!tagallocs);ng.ng_TextAttr=NULL;assert(!makeitext(NULL,&ng,NULL)&&!refs&&!allocs&&!tagallocs);failclone=0;
freeitext(NULL,NULL);puts("PASS: explicit/default tagged/untagged normal+underline text; source tags freed before actual preparefont; clone/open/allocation failures and free balance");return 0;}
'''
def extract(text):return text[text.index('struct GTIText\n'):text.index('BOOL renderlabel(')]
def run(text,directory,label,old=False):
 source=directory/(label+'.c');exe=directory/label;source.write_text(PRE+extract(text)+TEST)
 subprocess.run(['cc','-std=c99','-g','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-pointer-sign','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(source),'-o',str(exe)],check=True)
 env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0:abort_on_error=0'
 return subprocess.run([str(exe)]+(['old'] if old else []),capture_output=True,text=True,env=env)
def main():
 current=(ROOT/'workbench/libs/gadtools/basicfuncs.c').read_text(encoding='latin1')
 old=subprocess.check_output(['git','show','440724964a:workbench/libs/gadtools/basicfuncs.c'],cwd=ROOT).decode('latin1')
 with tempfile.TemporaryDirectory(prefix='aros-gadtools-textattr-') as tmp:
  directory=Path(tmp);result=run(current,directory,'fixed');print(result.stdout,end='');assert result.returncode==0,result.stderr
  for label,changed in {'borrowed_tags':current.replace('CloneTagItems(fonttags)', 'fonttags'), 'discard_valid_tag_style':current.replace('gtit->ta.tta_Style = fontstyle;', 'gtit->ta.tta_Style = fontstyle & ~FSF_TAGGED;')}.items():
   assert changed!=current;result=run(changed,directory,label);assert result.returncode!=0,label;print('PASS: negative control rejected:',label)
  result=run(old,directory,'old',True);assert result.returncode!=0 and 'AddressSanitizer' in result.stderr,result.stderr;print('PASS: old production bodies rejected by ASan while consuming tagged IntuiText attributes')
if __name__=='__main__':main()
