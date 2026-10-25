#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual MUI admission/setup/extent and live decision fragments.
Class/resource policy, native publication and font raster services are adapters;
scalecontext/font/open fixtures cover their respective production boundaries.
"""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[4]
s=(root/'workbench/libs/muimaster/classes/window.c').read_text()
def function(start):
 a=s.index(start);b=s.index('{',a);i=b+1;n=1
 while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
aware=function('static BOOL WindowScaleAware(')
extent=function('static LONG WindowExtent(')
a=s.index('    struct Screen *scr = mri->mri_Screen;');b=s.index('    if (!mri->mri_DrawInfo)',a)
setup=s[a:b]
a=s.index('            ULONG oldscale = data->wd_DisplayScale;',s.index('    case IDCMP_CHANGESCALE:'));b=s.index('            WindowRebasePosition',a)
event=s[a:b]
a=s.index('    const struct ScreenRenderContext *context =',s.index('IPTR Window__MUIM_RecalcDisplay('));b=s.index('        WindowRebasePosition',a)
recalc=s[a:b]
a=s.index('    const struct ScreenRenderContext *current =',s.index('static ULONG RetryWindowContext('));b=s.index('    if (displayed)',a)
retry=s[a:b]
a=s.index('        WA_ScaleAware,');b=s.index('\n',a)
tags=s[a:b]
head=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;typedef int32_t LONG;typedef uint32_t ULONG;typedef uintptr_t IPTR;
#define TRUE 1
#define FALSE 0
#define MUIMasterBase NULL
#define MUIM_ScaleContextSupported 1
#define MUII_WindowBack 2
#define WA_ScaleAware 3
struct Class{BOOL qualified;};
struct DrawInfo{ULONG height;};
struct ScreenRenderContext{ULONG src_Scale,src_LegacyScaleFactor;struct DrawInfo *src_PhysicalDrawInfo,*src_LogicalDrawInfo;BOOL src_PreparedScaleWindow;};
struct Screen{LONG Width,Height;const struct ScreenRenderContext*current;};
struct MUI_RenderInfo{struct Screen*mri_Screen;const struct ScreenRenderContext*mri_RenderContext;BOOL mri_ScaleAware;ULONG mri_RenderScale;struct DrawInfo*mri_DrawInfo;};
typedef struct Object{struct Class*cl;BOOL resources,images,qualified;}Object;
struct MUI_WindowData{Object*wd_RootObject,*wd_Menustrip;struct MUI_RenderInfo wd_RenderInfo;ULONG wd_DisplayScale;};
#define OCLASS(o) ((o)->cl)
static BOOL zune_class_scale_aware(struct Class*c,void*base){(void)base;return c->qualified;}
static BOOL DoMethod(Object*o,ULONG method){assert(method==MUIM_ScaleContextSupported);return o->resources;}
static BOOL zune_imspec_scale_supported(Object*o,ULONG image){assert(image==MUII_WindowBack);return o->images;}
static BOOL zune_object_scale_aware(Object*o,void*base){(void)base;return o->qualified;}
static unsigned captures;
static const struct ScreenRenderContext*publish_during_get;
static const struct ScreenRenderContext*zune_screen_context(struct Screen*screen){const struct ScreenRenderContext*ctx=screen->current;captures++;if(publish_during_get)screen->current=publish_during_get;return ctx;}
'''
wrappers='''
static void setup(Object *obj,struct MUI_WindowData *data){struct MUI_RenderInfo *mri=&data->wd_RenderInfo;
'''+setup+''' }
struct Decision{ULONG scale,factor;BOOL changed;};
static struct Decision event(Object*oWin,struct MUI_WindowData*data){
'''+event+''' (void)oldscale;return (struct Decision){scale,factor,TRUE};}
static struct Decision recalc(Object*obj,struct MUI_WindowData*data){
'''+recalc+''' (void)oldscale;return (struct Decision){newscale,newfactor,TRUE};}return (struct Decision){data->wd_DisplayScale,1,FALSE};}
static ULONG retry(Object*obj,struct MUI_WindowData*data){
'''+retry+''' return newscale;}
static BOOL native_publish(struct MUI_WindowData*data){IPTR native_tags[]={
'''+tags+''' 0};assert(native_tags[0]==WA_ScaleAware);assert(native_tags[1]==(IPTR)data->wd_RenderInfo.mri_ScaleAware);
return data->wd_RenderInfo.mri_RenderContext==data->wd_RenderInfo.mri_Screen->current&&(!native_tags[1]||data->wd_RenderInfo.mri_Screen->current->src_PreparedScaleWindow);}
'''
tests=r'''
int main(void){struct Class cls={TRUE};Object obj={&cls,TRUE,TRUE,TRUE},root={&cls,TRUE,TRUE,TRUE},menu={&cls,TRUE,TRUE,TRUE};
struct DrawInfo logical={13},physical={26};struct ScreenRenderContext stale={200,2,&physical,&logical,TRUE};
struct Screen second={2560,1600,&stale};struct MUI_WindowData control={&root,&menu,{&second,&stale,TRUE,200,&physical},200};struct MUI_WindowData saved=control;
/* Provider kinds model the public immutable result: builtin/theme opt in,
   inherited custom and unknown do not. Getter production is tested separately. */
for(unsigned kind=0;kind<4;kind++)for(ULONG phase=0;phase<14;phase++)for(unsigned custom=0;custom<2;custom++){
 ULONG scale=phase==13?100:100+25*phase;ULONG factor=(scale+50)/100;BOOL cap=kind<2,expected=cap&&!custom;
 struct DrawInfo currentFont={(13*scale+50)/100};struct ScreenRenderContext context={scale,factor,&currentFont,&logical,cap};
 struct Screen target={2560,1600,&context};struct MUI_WindowData data={&root,&menu,{.mri_Screen=&target},100};root.qualified=!custom;
 /* Neither NULL nor a different screen's old cached context may govern new
    target admission before Setup has installed its captured context. */
 for(unsigned cached=0;cached<2;cached++){
  data.wd_RenderInfo.mri_RenderContext=cached?&stale:NULL;
  captures=0;assert(WindowScaleAware(&obj,&data,&context)==expected);assert(!captures);
  setup(&obj,&data);assert(captures==1&&data.wd_RenderInfo.mri_RenderContext==&context);
  assert(data.wd_RenderInfo.mri_ScaleAware==expected&&data.wd_DisplayScale==(expected?scale:100));
  assert(data.wd_RenderInfo.mri_DrawInfo==(expected?&currentFont:&logical));
  LONG width=WindowExtent(&data.wd_RenderInfo,target.Width);assert(width==2560/(expected?1:factor));assert(native_publish(&data));
  data.wd_RenderInfo.mri_RenderContext=&stale;
  struct Decision d=event(&obj,&data);assert(d.scale==(expected?scale:100)&&d.factor==(expected?1:factor));
  assert(retry(&obj,&data)==(expected?scale:100));
  data.wd_RenderInfo.mri_ScaleAware=!expected;d=recalc(&obj,&data);assert(d.changed&&d.scale==(expected?scale:100)&&d.factor==(expected?1:factor));
  assert(!memcmp(&saved,&control,sizeof(saved)));
 }
 /* A publication between snapshot capture and qualification cannot replace
    its capability/fonts/extents. The bound native candidate must refuse. */
 struct ScreenRenderContext newer={400,4,&physical,&logical,!cap};
 data.wd_RenderInfo.mri_RenderContext=NULL;target.current=&context;publish_during_get=&newer;captures=0;
 setup(&obj,&data);publish_during_get=NULL;assert(captures==1&&data.wd_RenderInfo.mri_RenderContext==&context&&data.wd_RenderInfo.mri_ScaleAware==expected);
 assert(data.wd_RenderInfo.mri_DrawInfo==(expected?&currentFont:&logical)&&WindowExtent(&data.wd_RenderInfo,2560)==2560/(expected?1:factor));assert(!native_publish(&data));
}
assert(!WindowScaleAware(&obj,&control,NULL));puts("PASS actual MUI decorator admission/setup/extents/event/recalc/retry: builtin/theme/custom providers, custom-class veto,13scales+reset, logical13/physicalfont, NULL/other cached screen, immutable capture race refusal, second-screen unchanged");}
'''
variants=[('production',aware,setup,extent,True),
 ('ignored-provider',aware.replace('context && context->src_PreparedScaleWindow &&','context &&'),setup,extent,False),
 ('cached-context-admission',aware.replace('context && context->src_PreparedScaleWindow &&','(void)context, data->wd_RenderInfo.mri_RenderContext && data->wd_RenderInfo.mri_RenderContext->src_PreparedScaleWindow &&'),setup,extent,False),
 ('setup-old-context',aware,setup.replace('WindowScaleAware(obj, data, mri->mri_RenderContext)','WindowScaleAware(obj, data, NULL)'),extent,False),
 ('legacy-physical-font',aware,setup.replace('mri->mri_RenderContext->src_LogicalDrawInfo','mri->mri_RenderContext->src_PhysicalDrawInfo'),extent,False),
 ('extent-live-context',aware,setup,extent.replace('mri->mri_RenderContext->src_LegacyScaleFactor','mri->mri_Screen->current->src_LegacyScaleFactor'),False),
 ('live-cached-context',aware,setup,extent,False)]
with tempfile.TemporaryDirectory(prefix='scaledecor-') as directory:
 out=Path(directory)
 for name,a,p,e,passes in variants:
  unit=out/(name+'.c');binary=out/name
  selected=wrappers.replace(setup,p)
  if name=='live-cached-context':selected=selected.replace('zune_screen_context(data->wd_RenderInfo.mri_Screen);','data->wd_RenderInfo.mri_RenderContext;')
  unit.write_text(head+a+'\n'+e+'\n'+selected+tests)
  subprocess.run(['clang','-std=c11','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
  result=subprocess.run([str(binary)],capture_output=True,text=True)
  assert (result.returncode==0)==passes,(name,result.stdout,result.stderr)
  print(result.stdout if passes else 'REJECTED '+name,end='' if passes else '\n')
