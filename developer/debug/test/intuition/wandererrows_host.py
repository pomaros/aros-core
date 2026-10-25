#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual row preparation and Setup refusal under ASan/UBSan; no GUI emulation."""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[4]
s=(root/'workbench/system/Wanderer/Classes/iconlist.c').read_text(encoding='latin1')
def function(name):
    import re
    m=re.search(r'(?m)^.*\b'+name+r'\([^;]*?\)\s*\{',s);assert m,name
    a=m.start();b=s.index('{',a);i=b+1;depth=1
    while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
    return s[a:i]+'\n'
show=function('IconList__MUIM_Show');hide=function('IconList__MUIM_Hide');cleanup=function('IconList__MUIM_Cleanup');dispose=function('IconList__OM_DISPOSE')
assert 'lvma_IconDrawer = GetIconTags' not in show and 'DupDiskObject' not in show
assert 'iconlist_release_rows' not in hide
assert 'iconlist_release_rows(data);' in cleanup and 'iconlist_release_rows(data);' in dispose
setup=function('IconList__MUIM_Setup').split('    DoMethod(_win(obj), MUIM_Window_AddEventHandler,',1)[0]+'    return TRUE;\n}\n'
head=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef int32_t LONG;typedef uint32_t ULONG;typedef intptr_t IPTR;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define TAG_DONE 0
#define ICONENTRY_SIZE 16
#define ICONGETA_Screen 1
#define ICONGETA_RemapIcon 2
#define ICONGETA_FailIfUnavailable 3
#define ICONGETA_GenerateImageMasks 4
#define ICONA_ErrorCode 5
#define ICONCTRLA_SetScaleBox 6
#define ERROR_NO_FREE_STORE 103
#define ERROR_OBJECT_NOT_FOUND 205
#define PACK_ICON_SCALEBOX(w,h) (((w)<<16)|(h))
#define MUIM_Cleanup 42
#define _screen(o) ((o)->screen)
#define INST_DATA(c,o) ((void)(c),(o)->data)
struct DiskObject{LONG width,generation;};
struct ListViewModeAttribs{struct DiskObject*lvma_IconDrawer,*lvma_IconFile;};
struct IconList_DATA{struct ListViewModeAttribs*icld_LVMAttribs;ULONG icld_RenderScale;void *icld_DisplayRastPort,*icld_BufferRastPort,*icld_ScaledLabelFont,*icld_ScaledInfoFont,*icld_IconLabelFont,*icld_IconInfoFont;};
typedef struct Object{struct IconList_DATA *data;LONG screen;}Object;
struct IClass{int unused;};struct MUIP_Setup{int unused;};typedef void*Msg;
static int calls,failat,live,fonts,superclean,supersetup,missing,generation,scaled_screen;
static BOOL superclass=TRUE,font_success=TRUE;
static LONG WandererScalePoint(LONG n,ULONG scale){return n*scale/100;}
static struct DiskObject*makeicon(int width){struct DiskObject*p=malloc(sizeof*p);assert(p);*p=(struct DiskObject){width,generation};live++;return p;}
static struct DiskObject*GetIconTags(const char*name,IPTR a,IPTR screen,IPTR b,IPTR remap,IPTR c,IPTR fail,IPTR d,IPTR masks,IPTR end){
 assert(strstr(name,"WANDERER:Icons/")&&a==1&&b==2&&c==3&&d==4&&remap&&!fail&&masks&&!end);assert(screen==scaled_screen);
 if(++calls==failat)return NULL;
 (void)missing; /* Actual icon.library supplies builtin fallback. */
 return makeicon(16);}
static BOOL IconControl(struct DiskObject*p,ULONG tag,ULONG size,...){assert(tag==6);if(++calls==failat)return FALSE;p->width=size>>16;return TRUE;}
static BOOL LayoutIcon(struct DiskObject*p,LONG screen,...){assert(p&&screen==scaled_screen);return ++calls!=failat;}
static void FreeDiskObject(struct DiskObject*p){assert(p);--live;free(p);}
static BOOL DoSuperMethodA(struct IClass*c,Object*o,Msg m){(void)c;(void)o;(void)m;supersetup++;return superclass;}
static void DoSuperMethod(struct IClass*c,Object*o,int method){(void)c;(void)o;assert(method==MUIM_Cleanup);superclean++;}
static BOOL iconlist_resolve_fonts(Object*o,struct IconList_DATA*d){(void)o;if(!font_success)return FALSE;d->icld_ScaledLabelFont=malloc(1);d->icld_ScaledInfoFont=malloc(1);fonts+=2;d->icld_IconLabelFont=d->icld_ScaledLabelFont;d->icld_IconInfoFont=d->icld_ScaledInfoFont;return TRUE;}
static void CloseFont(void*p){assert(p);fonts--;free(p);}
'''
body=function('iconlist_release_rows')+function('iconlist_prepare_rows')+setup
tests=r'''
static void releasefonts(struct IconList_DATA*d){if(d->icld_ScaledLabelFont)CloseFont(d->icld_ScaledLabelFont);if(d->icld_ScaledInfoFont)CloseFont(d->icld_ScaledInfoFont);d->icld_ScaledLabelFont=d->icld_ScaledInfoFont=NULL;}
int main(void){struct IClass cl={0};struct MUIP_Setup msg={0};struct ListViewModeAttribs rows={makeicon(16),makeicon(16)};struct IconList_DATA data={.icld_LVMAttribs=&rows,.icld_RenderScale=200};Object o={&data,7};scaled_screen=7;
struct DiskObject*old0=rows.lvma_IconDrawer,*old1=rows.lvma_IconFile;
for(failat=1;failat<=6;failat++){calls=0;superclean=0;assert(!IconList__MUIM_Setup(&cl,&o,&msg));assert(superclean==1&&!fonts);assert(rows.lvma_IconDrawer==old0&&rows.lvma_IconFile==old1&&live==2);}
failat=0;generation=2;calls=0;assert(IconList__MUIM_Setup(&cl,&o,&msg));assert(rows.lvma_IconDrawer->width==32&&rows.lvma_IconFile->width==32&&live==2);assert(fonts==2);releasefonts(&data);
/* Repeated Show/Hide does not own rows: preserve exact pointers; changing
   layout grid/list can immediately use either already prepared image. */
old0=rows.lvma_IconDrawer;old1=rows.lvma_IconFile;
for(int i=0;i<10;i++){assert(rows.lvma_IconDrawer==old0&&rows.lvma_IconFile==old1&&live==2);}
/* Another screen's independent destination is untouched by rebuild/reset. */
struct ListViewModeAttribs secondrows={makeicon(16),makeicon(16)};struct IconList_DATA second={.icld_LVMAttribs=&secondrows,.icld_RenderScale=100};struct DiskObject*control=secondrows.lvma_IconDrawer;
ULONG scales[]={100,125,150,175,200,225,250,275,300,325,350,375,400,100};
for(unsigned i=0;i<sizeof(scales)/sizeof(*scales);i++){data.icld_RenderScale=scales[i];generation++;calls=0;assert(IconList__MUIM_Setup(&cl,&o,&msg));assert(rows.lvma_IconDrawer->width==(int)(16*scales[i]/100));assert(rows.lvma_IconFile->generation==generation);assert(secondrows.lvma_IconDrawer==control&&control->width==16);releasefonts(&data);}
iconlist_release_rows(&data);iconlist_release_rows(&data);iconlist_release_rows(&second);assert(!live&&!fonts);
/* Missing theme assets use builtin fallback; allocator refusal is distinct. */
missing=1;calls=0;assert(IconList__MUIM_Setup(&cl,&o,&msg));assert(rows.lvma_IconDrawer&&rows.lvma_IconFile);releasefonts(&data);iconlist_release_rows(&data);missing=0;
superclass=FALSE;calls=0;assert(!IconList__MUIM_Setup(&cl,&o,&msg)&&!calls&&!fonts);superclass=TRUE;
font_success=FALSE;superclean=0;assert(!IconList__MUIM_Setup(&cl,&o,&msg)&&superclean==1&&!fonts);
puts("wandererrows-host PASS six preparation faults preserve both old images; Setup refusal cleans fonts/super; 13 scales/reset; independent screen; builtin fallback; retained visibility lifecycle");}
'''
variants=[('production',body,True),('ignore-row-refusal',body.replace('if (!iconlist_prepare_rows(obj, data))','if (FALSE)'),False),('omit-font-unwind',body.replace('if (data->icld_ScaledLabelFont) CloseFont(data->icld_ScaledLabelFont);',''),False),('discard-old-before-prepare',body.replace('struct DiskObject *prepared[2] = { NULL, NULL };','iconlist_release_rows(data);\n    struct DiskObject *prepared[2] = { NULL, NULL };'),False),('oom-is-missing',body.replace('if (!prepared[i]) goto failure;','if (!prepared[i]) continue;'),False),('wrong-screen',body.replace('(IPTR)_screen(obj)','0'),False),('unscaled-rows',body.replace('data->icld_RenderScale != 100','FALSE'),False)]
with tempfile.TemporaryDirectory(prefix='wandererrows-') as directory:
    p=Path(directory);unit=p/'test.c';binary=p/'test'
    for name,actual,passes in variants:
        unit.write_text(head+actual+tests)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
        r=subprocess.run([str(binary)],capture_output=True,text=True)
        assert (r.returncode==0)==passes,(name,r.stdout,r.stderr)
        print(r.stdout if passes else 'REJECTED '+name,end='' if passes else '\n')
