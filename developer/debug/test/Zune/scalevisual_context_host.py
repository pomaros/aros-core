#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Compile real GadTools VI ownership/selection against immutable generations."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[4]
def body(name):
    s=(root/'workbench/libs/gadtools'/name).read_text(errors='surrogateescape')
    return s.split('    AROS_LIBFUNC_INIT',1)[1].split('    AROS_LIBFUNC_EXIT',1)[0]
get=body('getvisualinfoa.c');free=body('freevisualinfo.c')
support=(root/'workbench/libs/muimaster/support.c').read_text()
visual=support[support.index('APTR zune_visualinfo('):]
visual=visual[:visual.index('\n}')+2]
head=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
typedef uintptr_t IPTR;typedef void *APTR;
#define FALSE 0
#define MEMF_ANY 0
#define GTVI_RenderContext 1
#define GTVI_Logical 2
#define TAG_DONE 0
struct Screen {int id;};
struct DrawInfo {struct Screen *dri_Screen;int font;};
struct ScreenRenderContext {struct DrawInfo *src_LogicalDrawInfo,*src_PhysicalDrawInfo;};
struct TagItem {IPTR ti_Tag,ti_Data;};
struct MUI_RenderInfo {struct Screen *mri_Screen;const struct ScreenRenderContext *mri_RenderContext;int mri_ScaleAware;};
struct VisualInfo {struct Screen *vi_screen;struct DrawInfo *vi_dri;const struct ScreenRenderContext *vi_context;int vi_logical;};
static unsigned allocs,releases,logical_acquires,logical_releases;static int fail_alloc;
static struct DrawInfo default_info;
static IPTR GetTagData(IPTR tag,IPTR fallback,struct TagItem *list){if(list)for(;list->ti_Tag;list++)if(list->ti_Tag==tag)return list->ti_Data;return fallback;}
static void *AllocVec(size_t n,int flags){(void)flags;if(fail_alloc)return NULL;++allocs;return calloc(1,n);}
static void FreeVec(void *p){if(p){++releases;free(p);}}
static struct DrawInfo *GetScreenDrawInfo(struct Screen *s){++logical_acquires;default_info.dri_Screen=s;return &default_info;}
static void FreeScreenDrawInfo(struct Screen *s,struct DrawInfo *d){assert(d==&default_info&&d->dri_Screen==s);++logical_releases;}
'''
main=r'''
int main(void){
 struct Screen first={1},other={2};
 struct DrawInfo logicalA={&first,13},physicalA={&first,26},logicalB={&first,17},physicalB={&first,34},wrong={&other,1};
 struct ScreenRenderContext a={&logicalA,&physicalA},b={&logicalB,&physicalB};
 struct TagItem tags[]={{GTVI_RenderContext,(IPTR)&a},{0,0}};
 struct MUI_RenderInfo mri={&first,&a,1};
 assert(!zune_visualinfo(NULL));mri.mri_RenderContext=NULL;assert(!zune_visualinfo(&mri));mri.mri_RenderContext=&a;
 struct VisualInfo *bound=zune_visualinfo(&mri);assert(bound&&bound->vi_dri==&physicalA);freevi(bound);
 mri.mri_ScaleAware=0;bound=zune_visualinfo(&mri);assert(bound&&bound->vi_dri==&logicalA);freevi(bound);
 struct VisualInfo *old=getvi(&first,tags);assert(old&&old->vi_context==&a&&old->vi_dri==&physicalA);
 tags[0].ti_Data=(IPTR)&b;
 struct VisualInfo *fresh=getvi(&first,tags);assert(fresh&&fresh->vi_dri==&physicalB&&old->vi_dri->font==26);
 freevi(old);freevi(fresh);assert(!logical_acquires&&!logical_releases);
 struct TagItem logical[]={{GTVI_RenderContext,(IPTR)&a},{GTVI_Logical,1},{0,0}};
 old=getvi(&first,logical);assert(old&&old->vi_dri==&logicalA);freevi(old);
 old=getvi(&first,NULL);assert(old&&old->vi_dri==&default_info);freevi(old);assert(logical_acquires==1&&logical_releases==1);
 assert(!getvi(NULL,tags));fail_alloc=1;assert(!getvi(&first,tags));fail_alloc=0;
 assert(!getvi(&other,tags));b.src_PhysicalDrawInfo=NULL;assert(!getvi(&first,tags));b.src_PhysicalDrawInfo=&wrong;assert(!getvi(&first,tags));
 freevi(NULL);assert(allocs==releases);puts("PASS immutable VI generation, logical default, wrong-screen/failure ownership");
}
'''
source=head+'static APTR getvi(struct Screen *screen,struct TagItem *tagList){'+get+'}\nstatic void freevi(APTR vi){'+free+'}\n#define GetVisualInfoA getvi\n'+visual+'\n'+main
mutants=[('ignore-caller-context', 'vi->vi_context = (const struct ScreenRenderContext *)GetTagData(GTVI_RenderContext, 0, tagList);','vi->vi_context = NULL;'),('wrong-physical-view','vi->vi_context->src_PhysicalDrawInfo','vi->vi_context->src_LogicalDrawInfo'),('foreign-screen','vi->vi_dri->dri_Screen != screen','0'),('free-borrowed','if (!((struct VisualInfo *)vi)->vi_context)','if (1)')]
with tempfile.TemporaryDirectory(prefix='scalevisual-') as d:
    d=Path(d)
    for name,text in [('production',source)]+[(n,source.replace(a,b)) for n,a,b in mutants]:
        c=d/(name+'.c');exe=d/name;c.write_text(text)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
        p=subprocess.run([str(exe)],capture_output=True,text=True)
        if name=='production':
            assert p.returncode==0,p.stderr;print(p.stdout,end='')
        else: assert p.returncode!=0,f'surviving mutant {name}'
print('PASS 4 production mutants rejected')
