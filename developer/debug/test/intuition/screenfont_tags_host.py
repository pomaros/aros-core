#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Run actual screen font open bodies under ASan/UBSan; old code must fail."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[4]
PRE=r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG;typedef uint16_t UWORD;typedef uint8_t UBYTE;
#define NULL ((void*)0)
#define FSF_TAGGED 128
#define SA_SCALE_MIN 100
struct TagItem {uintptr_t ti_Tag,ti_Data;};
struct TextAttr {char *ta_Name;UWORD ta_YSize;UBYTE ta_Style,ta_Flags;};
struct TTextAttr {char *tta_Name;UWORD tta_YSize;UBYTE tta_Style,tta_Flags;struct TagItem *tta_Tags;};
struct TextFont {struct {struct {char *ln_Name;} mn_Node;} tf_Message;UWORD tf_YSize;UBYTE tf_Style,tf_Flags;void *tf_Extension;int tf_Accessors;};
struct TextFontExtension {struct TagItem *tfe_Tags;};
struct DiskFontHeader {struct TextFont dfh_TF;};
struct Library {int unused;};struct GfxBase {int unused;};
struct IntIntuitionBase {struct GfxBase *GfxBase;};struct IntuitionBase {struct IntIntuitionBase priv;};
#define GetPrivIBase(b) (&(b)->priv)
static struct TextFont source,opened;static struct DiskFontHeader scaled;
static struct TagItem tags[]={{0x80000001,0x960096},{0,0}};
static int expect_tags,diskfail,fontcalls,diskcalls,scaledcalls,forbidden;
static void Forbid(void){forbidden++;}static void Permit(void){assert(forbidden>0);forbidden--;}
static void check(struct TextAttr *a){assert(source.tf_Accessors>0);assert(a->ta_Name==source.tf_Message.mn_Node.ln_Name);
 if(expect_tags){assert(a->ta_Style&FSF_TAGGED);struct TagItem *p=((struct TTextAttr*)a)->tta_Tags;assert(p==tags&&p[0].ti_Data==0x960096&&p[1].ti_Tag==0);}
 else {assert(!(a->ta_Style&FSF_TAGGED));}}
static struct TextFont *openfont(struct TextAttr *a){check(a);fontcalls++;if(a->ta_YSize!=source.tf_YSize)return NULL;source.tf_Accessors++;return &source;}
static struct TextFont *opendisk(struct TextAttr *a){check(a);diskcalls++;if(diskfail)return NULL;opened.tf_YSize=a->ta_YSize;return &opened;}
static struct DiskFontHeader *newscaled(struct TextFont *f,struct TextAttr *a){assert(f==&source);check(a);scaledcalls++;scaled.dfh_TF.tf_YSize=a->ta_YSize;return &scaled;}
static struct Library *openlib(const char *s,int n){static struct Library lib;(void)s;(void)n;return &lib;}
#define OpenFont(a) ((void)GfxBase,openfont(a))
#define OpenDiskFont(a) ((void)DiskfontBase,opendisk(a))
#define NewScaledDiskFont(f,a) ((void)DiskfontBase,newscaled(f,a))
#define OpenLibrary(s,n) openlib(s,n)
#define CloseLibrary(b) ((void)(b))
#define CloseFont(f) ((void)(f))
'''
TEST=r'''
int main(void){struct IntuitionBase base={0};struct TextFontExtension ext={tags};source.tf_Message.mn_Node.ln_Name="Test.font";source.tf_YSize=13;source.tf_Style=FSF_TAGGED;source.tf_Extension=&ext;source.tf_Accessors=1;expect_tags=1;
struct TextFont *p=&source;assert(SafeReopenFont(&base,&p)==&source&&source.tf_Accessors==2&&!forbidden);
for(ULONG scale=200;scale<=400;scale+=100){struct TextFont *f=OpenScaledScreenFont(&base,&source,scale);assert(f&&f->tf_YSize==13*scale/100);}
diskfail=1;assert(OpenScaledScreenFont(&base,&source,200)==&scaled.dfh_TF&&scaledcalls==1);
expect_tags=0;ext.tfe_Tags=NULL;assert(SafeReopenFont(&base,&p)==&source);source.tf_Extension=NULL;assert(OpenScaledScreenFont(&base,&source,200));source.tf_Style=0;source.tf_Extension=&ext;assert(SafeReopenFont(&base,&p)==&source);
assert(source.tf_Accessors==4&&!forbidden&&fontcalls==8&&diskcalls==5);puts("PASS: tagged DPI preserved, null extension/tags untagged, synchronous accessor lifetime, memory/disk/bitmap paths");return 0;}
'''
def extract(text):
    # Select complete font bodies, independent of unrelated pointer helpers
    # inserted between the last font function and MakePointerFromData.
    bodies=[]
    for signature in ('void ScreenFontTextAttr(',
                      'struct TextFont *OpenScaledScreenFont(',
                      'struct TextFont *SafeReopenFont('):
        start=text.find(signature)
        if start < 0:
            if signature.startswith('void ScreenFontTextAttr('):
                continue  # Historical negative control predates this helper.
            raise ValueError('missing production font function: '+signature)
        end=text.index('{',start)+1;depth=1
        while depth:
            depth+=(text[end]=='{')-(text[end]=='}');end+=1
        bodies.append(text[start:end])
    return '\n'.join(bodies)+'\n'
def run(text,where,label):
    path=where/(label+'.c');exe=where/label;path.write_text(PRE+extract(text)+TEST)
    subprocess.run(['cc','-std=c99','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(path),'-o',str(exe)],check=True)
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0:abort_on_error=0'
    return subprocess.run([str(exe)],capture_output=True,text=True,env=env)
def main():
    current=(ROOT/'rom/intuition/misc.c').read_text(encoding='latin1')
    previous=subprocess.check_output(['git','show','0b69d1f517:rom/intuition/misc.c'],cwd=ROOT).decode('latin1')
    with tempfile.TemporaryDirectory(prefix='aros-screenfont-tags-') as temp:
        where=Path(temp);positive=run(current,where,'fixed');print(positive.stdout,end='');assert positive.returncode==0,positive.stderr
        negative=run(previous,where,'before');assert negative.returncode!=0 and 'stack-buffer-overflow' in negative.stderr,negative.stderr
        print('PASS: previous production bodies rejected by ASan stack-buffer-overflow reading TTextAttr tags')
if __name__=='__main__':main()
