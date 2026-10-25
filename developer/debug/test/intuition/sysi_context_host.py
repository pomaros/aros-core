#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License (APL) Version 1.1.
"""Execute actual immutable-context SYSICLASS preparation and disposal bodies."""
import argparse, pathlib, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[4]);a=p.parse_args()
s=(a.source/'rom/intuition/sysiclass.c').read_text()
def body(name):
    start=s.index(name+'(');left=s.index('{',start);depth=1;right=left+1
    while depth:
        depth+=(s[right]=='{')-(s[right]=='}');right+=1
    return s[left+1:right-1]
prepare=body('prepare_menu_image');dispose=body('SysIClass__OM_DISPOSE')
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;typedef uint32_t ULONG;typedef uintptr_t IPTR;typedef void *APTR;typedef void Object;typedef void *Msg;
#define TRUE 1
#define FALSE 0
#define MDM_GETSCALEDEFSIZE_SYSIMAGE 8
#define MDM_QUERYCAPABILITIES 12
#define MDM_CAP_SCALE 1
#define MDM_RELEASESCALE_SYSIMAGE 10
#define WDM_RELEASESCALE_SYSIMAGE 15
#define MDP_SCALE_FAILED 0
#define MDP_SCALE_READY 1
#define MDP_SCALE_UNAVAILABLE 2
struct DrawInfo {void *dri_Screen;};
struct ScreenRenderContext {struct DrawInfo *src_LogicalDrawInfo,*src_PhysicalDrawInfo;ULONG src_Scale;};
struct SysIData {struct DrawInfo *dri;void *frame;const struct ScreenRenderContext *context;Object *scaleProvider;APTR prepared;BOOL logical,vectorFallback,windowProvider,legacyMenuProvider;};
struct Image {long Width,Height;};
struct mdpGetDefSizeSysImage {ULONG MethodID;ULONG *mdp_Width,*mdp_Height;};
struct mdpGetScaleDefSizeSysImage {struct mdpGetDefSizeSysImage base;const struct ScreenRenderContext *context;BOOL logical;ULONG requestedWidth,requestedHeight;APTR *prepared;};
struct mdpQueryCapabilities {ULONG MethodID;};
struct mdpReleaseScaleSysImage {ULONG MethodID;APTR prepared;};
struct wdpReleaseScaleSysImage {ULONG MethodID;APTR prepared;};
struct IntScreen {Object *MenuDecorObj;};
#define IS(x) ((struct IntScreen *)(x))
struct IntuitionBase {int unused;};typedef struct {struct IntuitionBase *cl_UserData;} Class;
static struct SysIData *current;
#define INST_DATA(c,o) current
static unsigned capabilities=1,legacyCalls;
static unsigned providerStatus,allocations,disposals,calls,vectorCalls;static int badHandle,oversize,vectorFailure;
static IPTR DoMethodA(Object *o,Msg message) {assert(o==(void*)0x123);calls++;ULONG method=*(ULONG*)message;if(method==12)return capabilities;if(method==0){legacyCalls++;return 0;}if(method==10){struct mdpReleaseScaleSysImage *m=message;assert(m->prepared);free(m->prepared);disposals++;return 1;}assert(method==8);struct mdpGetScaleDefSizeSysImage *m=message;assert(m->requestedWidth==0||m->requestedWidth==40);assert(m->requestedHeight==0||m->requestedHeight==30);if(providerStatus==1||badHandle){*m->prepared=malloc(8);assert(*m->prepared);allocations++;}*m->base.mdp_Width=oversize?32768:20;*m->base.mdp_Height=24;return providerStatus;}
static BOOL MenuScaleVectorSize(struct mdpGetScaleDefSizeSysImage *m,struct IntuitionBase *b){(void)b;vectorCalls++;assert(!*m->prepared);*m->base.mdp_Width=18;*m->base.mdp_Height=22;return !vectorFailure;}
static void DisposeObject(void *p){assert(!p);}
static IPTR DoSuperMethodA(Class*c,Object*o,Msg m){(void)c;(void)o;(void)m;return 7;}
'''
functions='static BOOL prepare_menu_image(struct SysIData *data,struct Image *image,struct mdpGetDefSizeSysImage *message,BOOL set_width,BOOL set_height,struct IntuitionBase *IntuitionBase){'+prepare+'}\nstatic IPTR dispose(Class *cl,Object *obj,Msg msg){'+dispose+'}\n'
main=r'''
int main(void){struct IntScreen screen={(void*)0x123},other={0};struct DrawInfo logical={&screen},physical={&screen},own={&screen};struct ScreenRenderContext context={&logical,&physical,100};struct IntuitionBase base={0};Class cl={&base};struct Image image={40,30};ULONG width,height;struct mdpGetDefSizeSysImage message={0,&width,&height};
for(unsigned status=0;status<4;status++)for(unsigned wh=0;wh<4;wh++)for(unsigned logicalDomain=0;logicalDomain<2;logicalDomain++){struct SysIData data={0};data.dri=&own;data.context=&context;data.logical=logicalDomain;current=&data;providerStatus=status;calls=vectorCalls=0;BOOL result=prepare_menu_image(&data,&image,&message,wh&1,wh&2,&base);assert(result==(status==1||status==2));assert(vectorCalls==(status==2));assert(data.vectorFallback==(status==2));assert(dispose(&cl,NULL,NULL)==7);assert(allocations==disposals);}
for(unsigned which=0;which<2;which++){struct SysIData data={0};data.dri=&own;data.context=&context;current=&data;providerStatus=1;calls=0;if(which)physical.dri_Screen=&other;else context.src_LogicalDrawInfo=NULL;assert(!prepare_menu_image(&data,&image,&message,0,0,&base));assert(!calls);assert(dispose(&cl,NULL,NULL)==7);context.src_LogicalDrawInfo=&logical;physical.dri_Screen=&screen;}
for(unsigned failure=0;failure<4;failure++){struct SysIData data={0};data.dri=&own;data.context=&context;current=&data;providerStatus=failure==1?1:2;badHandle=failure==0;oversize=failure==1;vectorFailure=failure==2; if(failure==3)providerStatus=0;assert(!prepare_menu_image(&data,&image,&message,0,0,&base));assert(dispose(&cl,NULL,NULL)==7);assert(allocations==disposals);badHandle=oversize=vectorFailure=0;}
for(unsigned scale=100;scale<=200;scale+=100){struct SysIData data={0};data.dri=&own;data.context=&context;current=&data;context.src_Scale=scale;capabilities=0;legacyCalls=0;providerStatus=0;BOOL ready=prepare_menu_image(&data,&image,&message,0,0,&base);assert(ready==(scale==100));assert(legacyCalls==(scale==100));assert(data.legacyMenuProvider==(scale==100));assert(!data.prepared);assert(dispose(&cl,NULL,NULL)==7);}capabilities=1;context.src_Scale=100;
printf("PASS actual SYSI immutable image preparation: ready/missing/unknown/OOM, requested dimensions, wrong-screen rejection, failure-owned handles, balanced disposal\n");return 0;}
'''
variants=[('actual',functions),('accept-unknown',functions.replace('result != MDP_SCALE_READY','result == 999')),('fallback-on-oom',functions.replace('result == MDP_SCALE_UNAVAILABLE','result != MDP_SCALE_READY')),('lose-handle',functions.replace('data->prepared = NULL;','data->prepared = NULL;',1).replace('DoMethodA(data->scaleProvider, (Msg)&release);','(void)release;')),('typed-oom-legacy-fallback',functions.replace('DoMethodA(data->scaleProvider, (Msg)&query) & MDM_CAP_SCALE','FALSE')),('wrong-screen',functions.replace('data->context->src_PhysicalDrawInfo->dri_Screen != data->dri->dri_Screen','FALSE'))]
with tempfile.TemporaryDirectory(prefix='sysi-context-') as t:
    t=pathlib.Path(t)
    for name,code in variants:
        c=t/(name+'.c');exe=t/name;c.write_text(pre+code+main)
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-g',str(c),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if name=='actual':
            if result.returncode:raise SystemExit(result.stderr)
            print(result.stdout,end='')
        elif result.returncode==0:raise SystemExit('mutation escaped: '+name)
        else:print('REJECTED '+name)
