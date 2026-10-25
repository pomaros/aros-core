#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual GadTools text measurement selects its captured VI domain.
Font raster/TextLength is modeled; the real body must not mutate screen RP.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[4];s=(root/'workbench/libs/gadtools/basicfuncs.c').read_text();start=s.index('ULONG GT_TextLength(');left=s.index('{',start);end=left+1;depth=1
while depth:
 depth+=(s[end]=='{')-(s[end]=='}');end+=1
body=s[start:end]
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t ULONG;typedef const char*CONST_STRPTR;
struct TextFont{ULONG height;};struct RastPort{struct TextFont*Font;};struct Screen{struct RastPort RastPort;};struct DrawInfo{struct TextFont*dri_Font;};struct VisualInfo{struct Screen*vi_screen;struct DrawInfo*vi_dri;};struct Library{int unused;};
#define SetFont(rp,f) ((void)GadToolsBase,(rp)->Font=(f))
static ULONG TextLength(struct RastPort*rp,CONST_STRPTR text,ULONG count){(void)text;return count*rp->Font->height;}
'''
main=r'''
int main(void){struct TextFont logical={13},physical={26},changed={34};struct Screen screen={{&physical}};struct DrawInfo ld={&logical},pd={&physical};struct VisualInfo lv={&screen,&ld},pv={&screen,&pd};for(unsigned n=0;n<=5;n++){assert(GT_TextLength(&lv,"abcde",n,NULL)==13*n);assert(GT_TextLength(&pv,"abcde",n,NULL)==26*n);assert(screen.RastPort.Font==&physical);}screen.RastPort.Font=&changed;assert(GT_TextLength(&lv,"ab",2,NULL)==26&&GT_TextLength(&pv,"ab",2,NULL)==52);assert(screen.RastPort.Font==&changed);puts("PASS actual GadTools VI measurement: logical13/physical26 captured domains despite live34, zero lengths, screen RP unchanged");}
'''
with tempfile.TemporaryDirectory(prefix='gadtools-measure-') as tmp:
 tmp=Path(tmp)
 for name,code in [('actual',body),('live-raster-font',body.replace('SetFont(&rp, vi->vi_dri->dri_Font);','SetFont(&rp, vi->vi_screen->RastPort.Font);'))]:
  src=tmp/(name+'.c');exe=tmp/name;src.write_text(pre+code+main);subprocess.run(['clang','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True);p=subprocess.run([str(exe)],capture_output=True,text=True)
  if name=='actual':
   assert not p.returncode,p.stderr;print(p.stdout,end='')
  else:assert p.returncode,'escaped '+name;print('REJECTED '+name)
