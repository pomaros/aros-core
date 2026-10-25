#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise production font selection, cleanup and same-scale event handling."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
font = (root / 'workbench/libs/muimaster/font.c').read_text()
font = re.sub(r'^#include[^\n]*\n', '', font, flags=re.M)
font = font[font.index('/* Prefer the exact'):]
window = (root / 'workbench/libs/muimaster/classes/window.c').read_text()
cleanup = window[window.index('    for (i = 0; i < -MUIV_Font_NegCount; i++)', window.index('static void CleanupRenderInfo(')):]
cleanup = cleanup[:cleanup.index('    CleanupRenderInfoPens')]
coordinates = window[window.index('static LONG WindowExtent'):window.index('static APTR WindowVisualInfo')]
event = window[window.index('    case IDCMP_CHANGESCALE:'):window.index('    case IDCMP_NEWSIZE:')]
event = event[event.index('\n') + 1:event.rindex('        break;')]
support = (root / 'workbench/libs/muimaster/support.c').read_text()
metric = support[support.index('ULONG zune_legacy_factor'):support.index('LONG zune_scale(Object')]
head = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef unsigned long ULONG;typedef long LONG;typedef uintptr_t IPTR;
typedef intptr_t SIPTR;typedef unsigned short UWORD;typedef unsigned char UBYTE;
typedef char *STRPTR;typedef const char *CONST_STRPTR;typedef void *APTR;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MUI_MAXMAX 10000
#define MUIV_Font_Inherit 0
#define MUIV_Font_Normal -1
#define MUIV_Font_Fixed -2
#define MUIV_Font_NegCount -3
#define FSF_TAGGED 128
#define MUIA_Window_Open 4
#define SA_LegacyScaleFactor 5
#define D(...)
struct TextAttr {char *ta_Name;UWORD ta_YSize;UBYTE ta_Style,ta_Flags;};
struct TTextAttr {char *tta_Name;UWORD tta_YSize;UBYTE tta_Style,tta_Flags;APTR tta_Tags;};
struct Node {char *ln_Name;};struct Message {struct Node mn_Node;};
struct TextFontExtension {APTR tfe_Tags;};
struct TextFont {struct Message tf_Message;UWORD tf_YSize;UBYTE tf_Style,tf_Flags;APTR tf_Extension;};
struct DiskFontHeader {struct TextFont dfh_TF;};
struct ScreenRenderContext {struct TTextAttr src_BaseFont;ULONG src_Scale,src_LegacyScaleFactor;BOOL src_PreparedScaleWindow;};
struct Screen {struct TextAttr *Font;ULONG scale,factor;const struct ScreenRenderContext *context;};
struct Window {LONG LeftEdge,TopEdge;};
struct MUI_RenderInfo {struct TextFont *mri_Fonts[3];struct Screen *mri_Screen;const struct ScreenRenderContext *mri_RenderContext;ULONG mri_RenderScale;BOOL mri_ScaleAware;struct Window *mri_Window;};
struct Prefs {char *fonts[3];};
struct MUI_GlobalInfo {struct Prefs *mgi_Prefs;struct Screen *mgi_CustomScreen;};
typedef struct Object {struct MUI_GlobalInfo global;struct MUI_RenderInfo mri;BOOL aware;} Object;
struct MUI_WindowData {struct MUI_RenderInfo wd_RenderInfo;ULONG wd_DisplayScale;
 LONG wd_Width,wd_Height,wd_ReqWidth,wd_ReqHeight,wd_ReqX,wd_ReqY;Object *wd_RefWindow;};
struct IntuiMessage {void *IAddress;};
static unsigned opened,closed,replied,windowopens,windowcloses;
static unsigned disk_calls,scaled_calls;
static BOOL bitmap_only,check_tagged;
static APTR expected_tags;
static void check_attributes(struct TextAttr *attr){
 if(check_tagged){struct TTextAttr *tagged=(void*)attr;
 assert((tagged->tta_Style&FSF_TAGGED)&&tagged->tta_Tags==expected_tags);}
}
static struct TextFont defaultFont;
static struct {struct TextFont *DefaultFont;} gfx={&defaultFont};
#define GfxBase (&gfx)
#define muiGlobalInfo(o) (&(o)->global)
#define muiRenderInfo(o) (&(o)->mri)
#define _screen(o) ((o)->mri.mri_Screen)
static ULONG zune_render_scale(struct MUI_RenderInfo *mri){return mri->mri_RenderScale?mri->mri_RenderScale:100;}
static LONG zune_scale(Object *obj,LONG v);
static struct TextFont *OpenDiskFont(struct TextAttr *attr){
 check_attributes(attr);if(bitmap_only&&disk_calls==1)assert(attr->ta_YSize==13);++disk_calls;
 struct TextFont *font=calloc(1,sizeof(*font));assert(font);++opened;
 font->tf_YSize=bitmap_only?13:attr->ta_YSize;font->tf_Message.mn_Node.ln_Name=attr->ta_Name;return font;
}
static void CloseFont(struct TextFont *font){assert(font);++closed;free(font);}
static struct DiskFontHeader *NewScaledDiskFont(struct TextFont *base,struct TextAttr *attr){
 assert(bitmap_only&&base->tf_YSize==13);check_attributes(attr);++scaled_calls;
 struct DiskFontHeader *scaled=calloc(1,sizeof(*scaled));assert(scaled);++opened;
 scaled->dfh_TF.tf_YSize=attr->ta_YSize;return scaled;
}
static void *AllocVec(size_t n,unsigned flags){(void)flags;return malloc(n);}
static void FreeVec(void *p){free(p);}
static void StrToLong(char *p,LONG *out){*out=strtol(p,NULL,10);}
static char *FilePart(char *p){return p;}
static char *PathPart(char *p){return p;}
static BOOL WindowScaleAware(Object *obj,struct MUI_WindowData *data,const struct ScreenRenderContext *context){(void)data;return obj->aware&&context&&context->src_PreparedScaleWindow;}
static const struct ScreenRenderContext *zune_screen_context(struct Screen *screen){return screen->context;}
static ULONG zune_screen_scale(struct Screen *screen){return screen->scale;}
static BOOL GetAttr(ULONG attr,Object *object,IPTR *value){assert(attr==SA_LegacyScaleFactor);*value=((struct Screen *)object)->factor;return TRUE;}
static void ReplyMsg(struct Message *message){assert(message);++replied;}
'''
bridge = coordinates+r'''
static LONG zune_scale(Object *obj,LONG v){return zune_scale_value(v,zune_render_scale(&obj->mri));}
static void cleanup_fonts(Object *obj){int i;struct MUI_RenderInfo *mri=&obj->mri;
'''+cleanup+r'''
}
static void set(Object *obj,ULONG attr,BOOL open){assert(attr==MUIA_Window_Open);
 if(!open){++windowcloses;cleanup_fonts(obj);}else{++windowopens;
 obj->mri.mri_RenderScale=obj->aware?obj->mri.mri_Screen->scale:100;
 obj->mri.mri_RenderContext=obj->mri.mri_Screen->context;}
}
static void change(Object *oWin,struct MUI_WindowData *data,struct IntuiMessage *imsg){BOOL replied=FALSE;
'''+event+r'''
 (void)replied;
}
'''
tests = r'''
int main(void){
 assert(WindowRebaseCoordinate(100,1,2)==50);assert(WindowRebaseCoordinate(50,2,1)==100);
 assert(WindowRebaseCoordinate(-3,1,2)==-2);
 struct TTextAttr attr={"screen.font",13,0,0,NULL};struct ScreenRenderContext first={attr,200,2,TRUE},second={attr,200,2,TRUE};
 struct Screen screen={(struct TextAttr *)&attr,200,2,&first},other={(struct TextAttr *)&attr,100,1,&first};
 struct MUI_RenderInfo extent={.mri_Screen=&screen,.mri_RenderContext=&first};assert(WindowExtent(&extent,2560)==1280);
 assert(WindowPointToScreen(&extent,10,5)==30&&ScreenPointToWindow(&extent,30,5)==10);
 screen.factor=4;assert(WindowExtent(&extent,2560)==1280);second.src_LegacyScaleFactor=4;extent.mri_RenderContext=&second;assert(WindowExtent(&extent,2560)==640);screen.factor=2;extent.mri_RenderContext=&first;second.src_LegacyScaleFactor=2;
 struct Window native={100,40};struct MUI_WindowData rebased={.wd_RenderInfo={.mri_Screen=&screen,.mri_Window=&native}};
 /* Native97 has already applied current factor2 before notifying oldlegacy100 context. */
 WindowRebasePosition(&rebased,2);assert(rebased.wd_ReqX==100&&rebased.wd_ReqY==40);
 WindowRebasePosition(&rebased,1);assert(rebased.wd_ReqX==200&&rebased.wd_ReqY==80);
 rebased.wd_RenderInfo.mri_ScaleAware=TRUE;WindowRebasePosition(&rebased,2);assert(rebased.wd_ReqX==50&&rebased.wd_ReqY==20);

 struct Prefs prefs={{"","",""}};Object aware={.global={&prefs,NULL},.mri={.mri_RenderContext=&first,.mri_Screen=&screen,.mri_RenderScale=200},.aware=TRUE};
 Object legacy={.global={&prefs,NULL},.mri={.mri_RenderContext=&first,.mri_Screen=&screen,.mri_RenderScale=100}};
 struct MUI_WindowData ad={.wd_RenderInfo={.mri_Screen=&screen,.mri_ScaleAware=TRUE},.wd_DisplayScale=200,.wd_Width=200,.wd_Height=100};
 struct MUI_WindowData ld={.wd_RenderInfo={.mri_Screen=&screen},.wd_DisplayScale=100,.wd_Width=100,.wd_Height=50};
 struct IntuiMessage msg={&screen};
 assert(zune_font_get(&aware,MUIV_Font_Normal)->tf_YSize==26);
 assert(zune_font_get(&legacy,MUIV_Font_Normal)->tf_YSize==13);
 assert(opened==2&&!closed);zune_font_get(&aware,MUIV_Font_Normal);assert(opened==2);
 attr.tta_YSize=17;second.src_BaseFont=attr;screen.context=&second;
 change(&aware,&ad,&msg);change(&legacy,&ld,&msg);
 assert(replied==2&&windowopens==2&&windowcloses==2&&closed==2);
 assert(ad.wd_ReqWidth==200&&ld.wd_ReqWidth==100);
 assert(zune_font_get(&aware,MUIV_Font_Normal)->tf_YSize==34);
 assert(zune_font_get(&legacy,MUIV_Font_Normal)->tf_YSize==17);
 msg.IAddress=&other;change(&aware,&ad,&msg);assert(replied==2&&closed==2);
 screen.scale=100;screen.factor=1;second.src_Scale=100;second.src_LegacyScaleFactor=1;msg.IAddress=&screen;change(&aware,&ad,&msg);change(&legacy,&ld,&msg);
 assert(zune_font_get(&aware,MUIV_Font_Normal)->tf_YSize==17);
 assert(zune_font_get(&legacy,MUIV_Font_Normal)->tf_YSize==17);
 cleanup_fonts(&aware);cleanup_fonts(&legacy);assert(opened==closed);
 /* Preserve tagged DPI through exact and bitmap fallback requests at all
    thirteen percentages; the fallback base is independently opened at13. */
 struct {ULONG tag;IPTR value;} dpi[]={{1,72},{0,0}};
 expected_tags=dpi;check_tagged=TRUE;
 for(unsigned bitmap=0;bitmap<2;bitmap++)for(ULONG scale=100;scale<=400;scale+=25){
  struct TTextAttr tagged={"bitmap.font",(UWORD)zune_scale_value(13,scale),FSF_TAGGED,0,dpi};
  bitmap_only=bitmap;disk_calls=scaled_calls=0;
  struct TextFont *render=zune_open_scaled_font((struct TextAttr*)&tagged,13,scale);
  assert(render&&render->tf_YSize==tagged.tta_YSize);
  assert(scaled_calls==(bitmap&&scale!=100?1u:0u));
  assert(disk_calls==(bitmap&&scale!=100?2u:1u));
  CloseFont(render);assert(opened==closed);
 }
 check_tagged=bitmap_only=FALSE;
 struct TextFontExtension extension={dpi};struct TextFont taggedfont={.tf_YSize=13,.tf_Style=FSF_TAGGED|2,.tf_Extension=&extension};
 struct TTextAttr converted;
 zune_font_textattr(&taggedfont,&converted);assert(converted.tta_Style==(FSF_TAGGED|2)&&converted.tta_Tags==dpi);
 taggedfont.tf_Extension=NULL;zune_font_textattr(&taggedfont,&converted);
 assert(converted.tta_Style==2&&!converted.tta_Tags);
 taggedfont.tf_Extension=&extension;extension.tfe_Tags=NULL;
 zune_font_textattr(&taggedfont,&converted);assert(converted.tta_Style==2&&!converted.tta_Tags);
 puts("scalefont context ASan/UBSan PASS: actual font selection aware26/legacy13, same-scale changedbase34/17 closes+reloads both, other-screen ignored, reset17/17, no leaks");return 0;
}
'''
variants=[('production',font,True),
 ('lose-fallback-dpi',font.replace('? ((struct TTextAttr *)attr)->tta_Tags : NULL;','? NULL : NULL;'),False),
 ('wrong-bitmap-base',font.replace('base_attr.tta_YSize = base_size;','base_attr.tta_YSize = attr->ta_YSize;'),False),
 ('skip-managed-bitmap',font.replace('NewScaledDiskFont(base, attr)','NULL'),False),
 ('invalid-tag-without-extension',font.replace('attr->tta_Style = font->tf_Style & ~FSF_TAGGED;','attr->tta_Style = font->tf_Style;'),False),
 ('invalid-tag-without-tags',font.replace('if (attr->tta_Tags) attr->tta_Style |= FSF_TAGGED;','attr->tta_Style |= FSF_TAGGED;'),False)]
with tempfile.TemporaryDirectory(prefix='aros-scalefont-context-') as directory:
    source = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    for name,actual,passes in variants:
        source.write_text(head+metric+actual+bridge+tests)
        subprocess.run(['clang','-std=c11','-g','-O1','-Wall','-Wextra','-Werror',
                        '-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                        str(source),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],capture_output=True,text=True)
        assert (result.returncode==0)==passes,(name,result.stdout,result.stderr)
        print(result.stdout if passes else 'REJECTED '+name,end='' if passes else '\n')
    print('scalefont helpers PASS exact+bitmap13 all13percentages tagged DPI preserved; absent extension/tags clearFSF_TAGGED; five mutants rejected')
