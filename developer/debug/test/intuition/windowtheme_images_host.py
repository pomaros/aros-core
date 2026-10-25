#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License (APL) Version 1.1.
"""Actual checked state-sheet scaler + actual decorator container ownership."""
import argparse,pathlib,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[4]);a=p.parse_args()
s=(a.source/'workbench/c/Decoration/scaledimage.h').read_text();source=(a.source/'workbench/libs/decorator/decorimagefuncs.c').read_text()
def function(name):
 start=source.index(name+'(');left=source.index('{',start);depth=1;right=left+1
 while depth:
  depth+=(source[right]=='{')-(source[right]=='}');right+=1
 return source[left+1:right-1]
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;typedef uint16_t UWORD;typedef uint64_t UQUAD;typedef int BOOL;typedef void Object;typedef void*APTR;typedef char*STRPTR;
#define TRUE 1
#define FALSE 0
#define MEMF_ANY 1
#define MEMF_CLEAR 2
struct DecorImage{ULONG*data;UWORD w,h;BOOL ok;ULONG subimagescols,subimagesrows;BOOL*subimageinbm;Object*o;APTR mask;void*bitmap;STRPTR filename;void*bitmap2;};
static unsigned ordinal,failAt,allocated,freed;
static void*AllocVec(size_t bytes,unsigned flags){(void)flags;if(++ordinal==failAt)return NULL;void*p=calloc(1,bytes);assert(p);allocated++;return p;}
static void FreeVec(void*p){if(p){free(p);freed++;}}
static void DisposeDTObject(void*p){assert(!p);}
static void FreeBitMap(void*p){assert(!p);}
'''
containers='static struct DecorImage*DNewDecorImageContainer(UWORD w,UWORD h){'+function('NewDecorImageContainer')+'}\nstatic void DDisposeImageContainer(struct DecorImage*di){'+function('DisposeImageContainer')+'}\n'
main=r'''
int main(void){unsigned cases=0;const unsigned widths[]={1,3,7,13},heights[]={1,4,19};for(unsigned cols=1;cols<=4;cols++)for(unsigned rows=1;rows<=2;rows++)for(unsigned wi=0;wi<4;wi++)for(unsigned hi=0;hi<3;hi++)for(unsigned scale=100;scale<=400;scale+=25){ULONG sw=widths[wi],sh=heights[hi],dw=(sw*scale+50)/100,dh=(sh*scale+50)/100;struct DecorImage*source=DNewDecorImageContainer(sw*cols,sh*rows);assert(source);source->ok=1;source->subimagescols=cols;source->subimagesrows=rows;for(unsigned y=0;y<source->h;y++)for(unsigned x=0;x<source->w;x++)source->data[y*source->w+x]=(x/sw+1)*0x1000000u+(y/sh+1)*0x100000u+x*513u+y*7u;size_t bytes=source->w*source->h*4;ULONG*copy=malloc(bytes);assert(copy);memcpy(copy,source->data,bytes);unsigned before=allocated-freed;for(unsigned fail=0;fail<=2;fail++){ordinal=0;failAt=fail;struct DecorImage*image=checked_scale_image_cells(source,dw,dh);failAt=0;assert(!!image==!fail);assert(!memcmp(copy,source->data,bytes));if(image){assert(image->w==dw*cols&&image->h==dh*rows&&image->subimagescols==cols&&image->subimagesrows==rows);for(unsigned y=0;y<image->h;y++)for(unsigned x=0;x<image->w;x++){unsigned col=x/dw,row=y/dh;unsigned sx=col*sw+(uint64_t)(x%dw)*sw/dw,sy=row*sh+(uint64_t)(y%dh)*sh/dh;assert(image->data[y*image->w+x]==copy[sy*source->w+sx]);}DDisposeImageContainer(image);}assert(allocated-freed==before);}free(copy);DDisposeImageContainer(source);assert(allocated==freed);cases++;}
ULONG pixel=0;struct DecorImage invalid={.ok=1,.data=&pixel,.w=1,.h=1,.subimagescols=1,.subimagesrows=1};unsigned before=allocated;assert(!checked_scale_image_cells(NULL,1,1));assert(!checked_scale_image_cells(&invalid,0,1));assert(!checked_scale_image_cells(&invalid,65536,1));invalid.subimagescols=0;assert(!checked_scale_image_cells(&invalid,1,1));invalid.subimagescols=2;assert(!checked_scale_image_cells(&invalid,1,1));invalid.w=invalid.h=65535;invalid.subimagescols=1;assert(!checked_scale_image_cells(&invalid,1,1));assert(before==allocated);printf("PASS actual state-sheet scaler/container: %u cases, 2 actual allocation failures/case, independent ARGB/state pixel oracle, unchanged source, no leaks, malformed/overflow refusal\n",cases);return 0;}
'''
variants=[('actual',containers+s),('lose-state-column',(containers+s).replace('col * sourceWidth + (UQUAD)x','(UQUAD)x')),('wrong-pixel-row',(containers+s).replace('(UQUAD)y * sourceHeight / cellHeight','0')),('lose-failed-container',(containers+s).replace('FreeVec(di);\n            di = NULL;','di = NULL;')),('missing-size-guard',(containers+s).replace('width > 65535 || height > 65535 ||','FALSE ||'))]
with tempfile.TemporaryDirectory(prefix='window-theme-image-') as tmp:
 tmp=pathlib.Path(tmp)
 for name,body in variants:
  c=tmp/(name+'.c');exe=tmp/name;c.write_text(pre+body+main);subprocess.run(['clang','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True);q=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  if name=='actual':
   if q.returncode:raise SystemExit(q.stderr)
   print(q.stdout,end='')
  elif q.returncode==0:raise SystemExit('escaped mutant '+name)
  else:print('REJECTED '+name)
