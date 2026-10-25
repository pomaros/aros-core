#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Compile the actual exact-class/context policy and real arrow renderer."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[4]
source=(root/'workbench/libs/muimaster/scalecontext.c').read_text()
source=re.sub(r'^#include[^\n]*\n','',source,flags=re.M)
vector=(root/'workbench/libs/muimaster/imspec_vector.c').read_text()
vector=vector[vector.index('#define HSPACING'):vector.index('void checkbox_draw')]
support=(root/'workbench/libs/muimaster/support.c').read_text()
metric=support[support.index('LONG zune_scale_value'):support.index('LONG zune_scale(Object')]
head=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t ULONG;typedef int32_t LONG;typedef uint16_t UWORD;typedef int BOOL;
typedef uintptr_t IPTR;typedef void *APTR;typedef const unsigned char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define MUI_CLASS_SCALE_AWARE 1
#define CLF_INLIST 1
#define CLF_SCALE_AWARE 2
#define CLF_SCALE_DECLARED 4
#define MUIM_ScaleContextSupported 1
#define MUIM_ScaleContextChildren 4
#define MUIA_Group_ChildList 2
#define MUIA_Family_List 3
#define MUI_MAXMAX 10000
#define MPEN_TEXT 0
struct Library {int dummy;};struct Semaphore {int count;};
struct MUIMasterBase_intern {struct Semaphore ZuneSemaphore;};
#define MUIMB(b) ((struct MUIMasterBase_intern *)(b))
typedef struct Class {struct Class *cl_Super;ULONG cl_ObjectCount,cl_SubclassCount,cl_Flags;} Class;
typedef struct Object Object;
struct MinList {Object *mlh_Head;};
struct Object {Class *cl;BOOL resources;struct MinList children;Object *next;BOOL family;};
#define OCLASS(o) ((o)->cl)
static void ObtainSemaphore(struct Semaphore *s){++s->count;}
static void ReleaseSemaphore(struct Semaphore *s){assert(s->count);--s->count;}
static IPTR DoMethod(Object *o,ULONG method){if(method==1)return o->resources;assert(method==4);return o->family<0?0:(IPTR)&o->children;}

static Object *NextObject(Object **p){Object *o=*p;if(o)*p=o->next;return o;}
struct RastPort {LONG x,y;UBYTE *pixels;ULONG w,h;};
struct MUI_RenderInfo {const struct ScreenRenderContext *mri_RenderContext;ULONG mri_RenderScale;struct RastPort *mri_RastPort;UWORD *mri_Pens;};
'''.replace('typedef uint32_t ULONG;','typedef uint8_t UBYTE;typedef uint32_t ULONG;')
area=(root/'workbench/libs/muimaster/classes/area.c').read_text()
getter=area[area.index('    case MUIA_RenderContext:'):area.index('    case MUIA_Area_SearchMark:',area.index('    case MUIA_RenderContext:'))]
accessor='\nstatic BOOL area_get(struct MUI_RenderInfo *mri,ULONG attr,IPTR *storage){ struct { BOOL mad_ScaleAwareHooks; } area={TRUE},*data=&area;\n#define STORE (*storage)\n#define muiRenderInfo(obj) mri\n switch(attr){'+getter+'default:return FALSE;}\n#undef STORE\n#undef muiRenderInfo\n}\n'
head+='#define MUIA_RenderContext 9\n#define MUIA_RenderScale 7\n#define MUIA_ScaleAwareHooks 8\n'
renderer=r'''
static void SetAPen(struct RastPort *rp,UWORD pen){(void)rp;(void)pen;}
static void Move(struct RastPort *rp,LONG x,LONG y){rp->x=x;rp->y=y;}
static void Draw(struct RastPort *rp,LONG x1,LONG y1){
 LONG x0=rp->x,y0=rp->y,dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
 for(;;){assert(x0>=0&&y0>=0&&(ULONG)x0<rp->w&&(ULONG)y0<rp->h);rp->pixels[y0*rp->w+x0]=1;
 if(x0==x1&&y0==y1)break;LONG e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
 rp->x=x1;rp->y=y1;
}
'''
tests=r'''
int main(void){
 struct MUIMasterBase_intern base={0};Class rootclass={0};Class notify={&rootclass,0,0,0};
 Class group={&notify,0,0,0},text={&notify,0,0,0},custom={&text,0,0,0},subclass={&custom,0,0,0};
 struct Library *lib=(void *)&base;
 assert(!zune_class_scale_aware(&group,lib));
 assert(zune_set_class_capabilities(&notify,1,lib));assert(zune_set_class_capabilities(&group,1,lib));assert(zune_set_class_capabilities(&text,1,lib));
 Object child={&text,TRUE,{0},NULL,FALSE};Object obj={&group,TRUE,{&child},NULL,FALSE};
 assert(zune_object_scale_aware(&obj,lib));
 child.cl=&custom;assert(!zune_object_scale_aware(&obj,lib));assert(zune_set_class_capabilities(&custom,1,lib));assert(zune_object_scale_aware(&obj,lib));
 child.cl=&subclass;assert(!zune_object_scale_aware(&obj,lib));
 subclass.cl_ObjectCount=1;assert(!zune_set_class_capabilities(&subclass,1,lib));subclass.cl_ObjectCount=0;
 subclass.cl_SubclassCount=1;assert(!zune_set_class_capabilities(&subclass,1,lib));subclass.cl_SubclassCount=0;
 assert(zune_set_class_capabilities(&subclass,1,lib));subclass.cl_ObjectCount=5;assert(zune_set_class_capabilities(&subclass,1,lib));assert(!zune_set_class_capabilities(&subclass,0,lib));
 assert(!zune_set_class_capabilities(&subclass,2,lib));
 /* IconList-like Area exposes a non-BOOPSI Family_List; typed child method
  * returnsNULL and must never traverse this poisoned data address. */
 Object pseudo={&text,TRUE,{(Object *)(uintptr_t)1},NULL,-1};
 assert(zune_object_scale_aware(&pseudo,lib));
 assert(zune_object_scale_aware(&obj,lib));child.resources=FALSE;assert(!zune_object_scale_aware(&obj,lib));child.resources=TRUE;
 child.cl=&text;obj.family=TRUE;assert(zune_object_scale_aware(&obj,lib));
 assert(zune_text_scale_supported((CONST_STRPTR)"plain\33bBold"));
 assert(!zune_text_scale_supported((CONST_STRPTR)"\33I[1:0]"));assert(!zune_text_scale_supported((CONST_STRPTR)"\33O[deadbeef]"));assert(!zune_text_scale_supported((CONST_STRPTR)"\33A[deadbeef]"));
 struct MUI_RenderInfo mri={0};assert(zune_render_scale(NULL)==100);assert(zune_render_scale(&mri)==100);mri.mri_RenderScale=200;assert(zune_render_scale(&mri)==200);
 IPTR returned=0;int context=42;mri.mri_RenderContext=(void *)&context;
 assert(area_get(&mri,MUIA_RenderContext,&returned)&&returned==(IPTR)&context);
 assert(area_get(NULL,MUIA_RenderContext,&returned)&&!returned);
 assert(area_get(&mri,MUIA_RenderScale,&returned)&&returned==200);
 mri.mri_RenderScale=100;assert(area_get(&mri,MUIA_RenderScale,&returned)&&returned==100);
 assert(area_get(&mri,MUIA_ScaleAwareHooks,&returned)&&returned);
 assert(zune_scale_value(6,100)==6&&zune_scale_value(6,200)==12&&zune_scale_value(8,125)==10&&zune_scale_value(MUI_MAXMAX,400)==MUI_MAXMAX);
 /* Unknown exact class address reuse starts zeroed; superclass qualification is not inherited. */
 memset(&custom,0,sizeof(custom));custom.cl_Super=&text;assert(!zune_class_scale_aware(&custom,lib));
 assert(!base.ZuneSemaphore.count);
 UBYTE pixels[80*80],baseline[20*20];UWORD pens[]={0};struct RastPort rp={0,0,pixels,80,80};mri.mri_RastPort=&rp;mri.mri_Pens=pens;
 typedef void(*Render)(struct MUI_RenderInfo*,LONG,LONG,LONG,LONG,LONG);
 Render funcs[]={arrowup_draw,arrowdown_draw,arrowleft_draw,arrowright_draw};
 for(unsigned f=0;f<4;++f){
  memset(pixels,0,sizeof(pixels));mri.mri_RenderScale=100;funcs[f](&mri,0,0,20,20,0);
  unsigned count=0;for(unsigned y=0;y<20;++y)for(unsigned x=0;x<20;++x){baseline[y*20+x]=pixels[y*80+x];count+=pixels[y*80+x];}assert(count>0);
  for(unsigned scale=125;scale<=400;scale+=25){memset(pixels,0,sizeof(pixels));mri.mri_RenderScale=scale;unsigned extent=20*scale/100;
   funcs[f](&mri,0,0,extent,extent,0);unsigned enlarged=0;
   for(unsigned y=0;y<80;++y)for(unsigned x=0;x<80;++x){if(pixels[y*80+x]){assert(x<extent&&y<extent);++enlarged;}}
   assert(enlarged>=count);}
 }
 puts("scalecontext production ASan/UBSan PASS: exact class/no inheritance, immutable declaration, resource veto, context100/200, all4 real arrows125..400 bounded+larger");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='aros-scalecontext-') as d:
 c=Path(d)/'test.c';exe=Path(d)/'test';c.write_text(head+source+metric+accessor+renderer+vector+tests)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
