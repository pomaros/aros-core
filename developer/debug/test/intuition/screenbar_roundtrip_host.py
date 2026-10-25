#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""OpenScreen's decorated bar against the live scale transaction, bit-exact.

Executes the actual int_InitDecorator and PrepareScreenScaleDecoration
(decoration.c), the actual render-context logical metrics
(screenrendercontext.c), the built-in decorator and the shipped Decoration
screen class (InitScreen metrics, PrepareScale) over every shipped theme's
Config. A screen opened at scale S must carry exactly the metrics a live
transaction 100 -> S -> 100 produces, and the reset must equal the boot
screen. The stack is filled with 8 before each OpenScreen, as on the M1 where
an uninitialised InitScreen padding gave a 29-row Ice bar at boot and 18
after a reset. Not covered: BOOPSI dispatch, theme images, layers, drawing.
"""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, tempfile
from scaleresult_fixture import result_definitions
ROOT = Path(__file__).resolve().parents[4]

def read(rel): return (ROOT / rel).read_text(encoding='latin1')
def function(text, marker):
    start = text.index(marker); opening = text.index('{', start); end = opening + 1; depth = 1
    while depth: depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end] + '\n'

public = read('compiler/include/intuition/scrdecorclass.h')
public = public[public.index('struct ScreenScaleMetrics'):public.index('struct sdpGetDefSizeSysImage')] + \
    public[public.index('struct sdpInitScreen'):public.index('struct sdpExitScreen')]
decoration = read('rom/intuition/decoration.c')
intuition = decoration[decoration.index('static LONG scale_bar_height('):decoration.index('void int_CalcSkinInfo(')] + \
    decoration[decoration.index('struct PreparedDepthGadget'):]
context = function(read('rom/intuition/screenrendercontext.c'), 'static BOOL context_logicalmetrics(')
builtin = read('rom/intuition/scrdecorclass.c')
builtin = function(builtin, 'IPTR ScrDecorClass__SDM_INITSCREEN(') + builtin[builtin.index('IPTR ScrDecorClass__SDM_PREPARESCALE('):]
shipped = read('workbench/c/Decoration/screendecorclass.c')
shipped = shipped[shipped.index('static BOOL scrdecor_scale_metrics('):shipped.index('static IPTR scrdecor_initscreen(')]

def themes():
    out = []
    for config in sorted((ROOT / 'images/Themes').glob('*/system/Config')):
        values = {'BarHeight': 0, 'SBarHeight': 0, 'LeftBorder': 0, 'RightBorder': 0, 'BottomBorder': 0, 'SBarGadPost': 0}
        for line in config.read_text(encoding='latin1').splitlines():
            for key in values:
                if line.startswith(key + ' ') or line.startswith(key + '\t'):
                    numbers = re.findall(r'-?\d+', line.split('/*')[0].split('=', 1)[1])
                    values[key] = int(numbers[-1 if key == 'SBarGadPost' else 0])
        out.append((config.parts[-3], values))
    return out

PREFIX = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef int8_t BYTE;typedef uint8_t UBYTE;typedef int32_t LONG;typedef uint32_t ULONG;typedef int16_t WORD;typedef uint16_t UWORD;
typedef uintptr_t IPTR;typedef void*APTR;typedef int BOOL;typedef void*Msg;
#define TRUE 1
#define FALSE 0
#define STACKED
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define SCREENQUIET 1
#define GFLG_RELWIDTH 4
#define GFLG_RELRIGHT 8
#define DRIF_DIRECTCOLOR 1
#define D(x)
#define SDM_INITSCREEN 97
#define SDM_QUERYCAPABILITIES 98
#define SDM_PREPARESCALE 99
#define SDM_CAP_PREPARESCALE 1
struct TextFont{UWORD tf_YSize;};
typedef struct Class{int id;}Class;
typedef struct Object{WORD Width,Height;Class*class;void*data;}Object;
#define Image Object
#define OCLASS(obj) ((obj)->class)
#define INST_DATA(cl,obj) ((void)(cl),(obj)->data)
struct Gadget{WORD LeftEdge,TopEdge,Width,Height;UWORD Flags;APTR GadgetRender;};
struct Screen{BYTE BarHeight,BarVBorder,BarHBorder,MenuVBorder,MenuHBorder,WBorTop,WBorLeft,WBorRight,WBorBottom;UWORD Flags;};
struct DrawInfo{struct Screen*dri_Screen;};
struct IntDrawInfo{ULONG dri_Flags;struct TextFont*dri_Font;};
struct ScreenRenderContext;
struct IntScreen{struct Screen Screen;struct IntDrawInfo DInfo;Object*depthgadget;IPTR DecorUserBuffer;Object*ScrDecorObj;struct ScreenRenderContext*RenderContext;};
#define GetPrivScreen(s) ((struct IntScreen*)(s))
struct IntuitionBase{int unused;};
struct DecorConfig{LONG BarHeight,SBarHeight,LeftBorder,RightBorder,BottomBorder,SBarGadPost_s;};
struct ScreenData{void*dts;};
struct scrdecor_data{struct DecorConfig*dc;};
static unsigned live;
static void*AllocVec(IPTR n,ULONG f){(void)f;void*p=calloc(1,n);assert(p);live++;return p;}
static void FreeVec(void*p){if(p){live--;free(p);}}
static IPTR DoMethodA(Object*o,Msg message);
'''
BOUNDARY = r'''
struct ScreenRenderContext{struct DrawInfo*src_PhysicalDrawInfo;ULONG src_PhysicalBarHeight;struct ScreenScaleMetrics logical;};
static const struct ScreenRenderContext*CurrentScreenRenderContext(struct Screen*s){return GetPrivScreen(s)->RenderContext;}
static const struct ScreenScaleMetrics*ScreenRenderContextLogicalMetrics(const struct ScreenRenderContext*c){return c?&c->logical:NULL;}
static Object*ScreenRenderContextDepthImage(const struct ScreenRenderContext*c){(void)c;return NULL;}
'''
DISPATCH = r'''
static Class builtinclass={1},shippedclass={2};
static IPTR DoMethodA(Object*o,Msg message){
 ULONG id=*(ULONG*)message;
 if(id==SDM_QUERYCAPABILITIES)return SDM_CAP_PREPARESCALE;
 if(o->class==&shippedclass){struct scrdecor_data*data=INST_DATA(o->class,o);
  if(id==SDM_INITSCREEN)return scrdecor_initscreen_metrics(data->dc,message);
  return scrdecor_preparescale(o->class,o,message);}
 if(id==SDM_INITSCREEN)return ScrDecorClass__SDM_INITSCREEN(&builtinclass,o,message);
 return ScrDecorClass__SDM_PREPARESCALE(&builtinclass,o,message);
}
'''
MAIN = r'''
struct Model{struct IntScreen is;struct ScreenRenderContext context;struct DrawInfo dri;struct TextFont base,font;struct ScreenData sd;};
/* The M1 stack at OpenScreen: whatever an earlier call left there. */
static __attribute__((noinline)) void poison(void){volatile LONG junk[16384];for(unsigned i=0;i<16384;i++)junk[i]=8;(void)junk[7];}
static void publish_context(struct Model*m,const struct ScreenScaleMetrics*physical,ULONG scale){
 struct ScreenScaleMetrics logical;
 assert(context_logicalmetrics(&m->is,&m->base,scale,physical,&logical,NULL));
 m->context=(struct ScreenRenderContext){&m->dri,(ULONG)physical->BarHeight+1,logical};m->is.RenderContext=&m->context;}
static void capture(struct Screen*s,struct ScreenScaleMetrics*m){*m=(struct ScreenScaleMetrics){s->BarHeight,s->BarVBorder,s->BarHBorder,s->MenuVBorder,s->MenuHBorder,s->WBorTop,s->WBorLeft,s->WBorRight,s->WBorBottom,-1,0};}
static ULONG physical_font(ULONG base,ULONG scale){return (base*scale+50)/100;}
/* OpenScreen: its defaults, its first bar estimate, then the decorator. */
static __attribute__((noinline)) BOOL open_screen(struct Model*m,Object*decor,ULONG base,ULONG scale){
 memset(m,0,sizeof*m);m->sd.dts=(void*)0x1234;m->base.tf_YSize=base;m->font.tf_YSize=physical_font(base,scale);
 m->is.Screen=(struct Screen){0,1,5,2,4,6,4,4,4,0};m->is.DInfo=(struct IntDrawInfo){DRIF_DIRECTCOLOR,&m->font};
 m->is.ScrDecorObj=decor;m->is.DecorUserBuffer=(IPTR)&m->sd;m->dri.dri_Screen=&m->is.Screen;
 m->is.Screen.BarHeight=m->font.tf_YSize+m->is.Screen.WBorTop-2+m->is.Screen.BarVBorder*2;
 poison();
 if(!int_InitDecorator(&m->is.Screen,&m->base,scale))return FALSE;
 struct ScreenScaleMetrics physical;capture(&m->is.Screen,&physical);publish_context(m,&physical,scale);return TRUE;}
static void transact(struct Model*m,ULONG scale){
 struct TextFont*font=&m->font;font->tf_YSize=physical_font(m->base.tf_YSize,scale);
 struct PreparedScreenScaleDecoration*p=PrepareScreenScaleDecoration(&m->is.Screen,&m->base,font,scale,NULL,NULL);assert(p);
 struct ScreenScaleMetrics next=*PreparedScreenScaleDecorationMetrics(p);
 InstallScreenScaleDecoration(p,FALSE);ReleaseScreenScaleDecoration(p,NULL);
 struct ScreenScaleMetrics physical;capture(&m->is.Screen,&physical);assert(physical.BarHeight==next.BarHeight);publish_context(m,&physical,scale);}
static void same(const char*what,const struct Model*a,const struct Model*b,ULONG scale){
 struct ScreenScaleMetrics x,y;capture((struct Screen*)&a->is.Screen,&x);capture((struct Screen*)&b->is.Screen,&y);
 if(memcmp(&x,&y,sizeof x)||memcmp(&a->context.logical,&b->context.logical,sizeof x)||a->context.src_PhysicalBarHeight!=b->context.src_PhysicalBarHeight){
  printf("FAIL %s at %lu%%: OpenScreen bar %ld vborder %ld wbortop %ld, transaction bar %ld vborder %ld wbortop %ld\n",what,(unsigned long)scale,(long)b->is.Screen.BarHeight+1,(long)b->is.Screen.BarVBorder,(long)b->is.Screen.WBorTop,(long)a->is.Screen.BarHeight+1,(long)a->is.Screen.BarVBorder,(long)a->is.Screen.WBorTop);abort();}}
int main(void){
 unsigned cases=0;
 /* The M1 boot screen first: Ice, arial 13, 100 % and 200 %. */
 {struct scrdecor_data data={&themes[ICE]};Object decor={0,0,&shippedclass,&data};struct Model a,b;
  assert(open_screen(&a,&decor,13,100)&&open_screen(&b,&decor,13,200));
  if(a.context.src_PhysicalBarHeight!=18||b.context.src_PhysicalBarHeight!=36){printf("FAIL Ice arial 13: OpenScreen bar %lu at 100%%, %lu at 200%%; the theme's own bar is 18 and 36\n",(unsigned long)a.context.src_PhysicalBarHeight,(unsigned long)b.context.src_PhysicalBarHeight);abort();}}
 for(unsigned t=0;t<=THEMES;t++){
  struct scrdecor_data data={t<THEMES?&themes[t]:NULL};Object decor={0,0,t<THEMES?&shippedclass:&builtinclass,&data};
  for(unsigned face=0;face<3;face++){ULONG base=face==0?8:face==1?13:26;
   struct Model open[13];for(unsigned i=0;i<13;i++){BOOL ok=open_screen(&open[i],&decor,base,100+25*i);if(!ok){assert(base==26&&100+25*i>200);open[i].is.ScrDecorObj=NULL;}}
   struct Model screen;assert(open_screen(&screen,&decor,base,100));
   if(t==ICE&&base==13){
    if(open[0].context.src_PhysicalBarHeight!=18||open[4].context.src_PhysicalBarHeight!=36){printf("FAIL Ice arial 13: OpenScreen bar %lu at 100%%, %lu at 200%%; the theme's own bar is 18 and 36\n",(unsigned long)open[0].context.src_PhysicalBarHeight,(unsigned long)open[4].context.src_PhysicalBarHeight);abort();}}
   /* 100 -> S -> 100 for every accepted S, then a walk through all of them. */
   for(unsigned i=1;i<13;i++){if(!open[i].is.ScrDecorObj)continue;
    transact(&screen,100+25*i);same("forward",&screen,&open[i],100+25*i);
    transact(&screen,100);same("reset",&screen,&open[0],100);cases+=2;}
   for(unsigned i=0;i<13;i++){if(!open[i].is.ScrDecorObj)continue;transact(&screen,100+25*i);same("walk",&screen,&open[i],100+25*i);cases++;}
   transact(&screen,100);same("final reset",&screen,&open[0],100);cases++;
  }
 }
 assert(!live);
 printf("PASS OpenScreen equals the live transaction: %u themes plus built-in, faces 8/13/26, %u forward/reset/walk cases, Ice arial 13 bar 18/36\n",(unsigned)THEMES,cases);return 0;}
'''

def unit(themelist, intuition_code, context_code, shipped_code):
    names = [n for n, _ in themelist]
    table = 'static struct DecorConfig themes[]={' + ','.join(
        '{%d,%d,%d,%d,%d,%d}' % (v['BarHeight'], v['SBarHeight'], v['LeftBorder'], v['RightBorder'], v['BottomBorder'], v['SBarGadPost'])
        for _, v in themelist) + '};\n#define THEMES %d\n#define ICE %d\n' % (len(names), names.index('Ice'))
    return PREFIX + result_definitions(ROOT) + public + BOUNDARY + \
        '\n'.join(l for l in read('rom/intuition/prepareddecor.h').splitlines() if not l.startswith('#include')) + '\n' + \
        context_code + builtin + shipped_code + DISPATCH + intuition_code + table + MAIN

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--output', type=Path); args = ap.parse_args()
    themelist = themes(); assert any(n == 'Ice' for n, _ in themelist)
    seed = 'struct ScreenScaleMetrics metrics =\n    {\n        (LONG)msg->sdp_BarHeight, (LONG)msg->sdp_BarVBorder,'
    assert seed in shipped
    mutants = {
        # The defect seen on the M1: InitScreen's padding read from the stack.
        'uninitialised-init-padding': (intuition, context, shipped.replace(seed, 'struct ScreenScaleMetrics metrics; (void)\n    (\n        (LONG)msg->sdp_BarHeight + (LONG)msg->sdp_BarVBorder +', 1)
            .replace('msg->sdp_WBorRight, msg->sdp_WBorBottom, -1, 0\n    };', 'msg->sdp_WBorRight + msg->sdp_WBorBottom\n    );')),
        'init-unscaled-padding': (intuition, context, shipped.replace('(LONG)msg->sdp_BarVBorder,', '1,', 1)),
        'transaction-unscaled-vborder': (intuition.replace('p->next.BarVBorder = (logical->BarVBorder * scale + 50) / 100;', 'p->next.BarVBorder = logical->BarVBorder;'), context, shipped),
        'transaction-from-live-metrics': (intuition.replace('const struct ScreenScaleMetrics *logical = ScreenRenderContextLogicalMetrics(context);',
            'struct ScreenScaleMetrics live; capture_scale_metrics(screen, &live); (void)context; const struct ScreenScaleMetrics *logical = &live;'), context, shipped),
        'logical-vborder-kept-physical': (intuition, context.replace('logical->BarVBorder = 1;', ''), shipped),
        'theme-bar-not-scaled': (intuition, context, shipped.replace('themedbar = (config->SBarHeight * (LONG)scale + 50) / 100 - 1;', 'themedbar = config->SBarHeight - 1;')),
    }
    with tempfile.TemporaryDirectory(prefix='screenbar-') as tmp:
        out = args.output or Path(tmp); out.mkdir(parents=True, exist_ok=True); records = []
        for name, parts in [('production', (intuition, context, shipped))] + list(mutants.items()):
            assert name == 'production' or parts != (intuition, context, shipped), name
            c = out / (name + '.c'); exe = out / name; c.write_text(unit(themelist, *parts))
            built = subprocess.run(['clang', '-std=gnu11', '-O0', '-g', '-Wno-uninitialized', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', str(c), '-o', str(exe)], capture_output=True, text=True)
            if built.returncode: raise RuntimeError(name + ':\n' + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            (out / (name + '.log')).write_text(run.stdout + run.stderr)
            if name == 'production':
                if run.returncode: raise RuntimeError(run.stdout + run.stderr)
                print(run.stdout.strip())
            elif not run.returncode: raise RuntimeError('mutant escaped: ' + name)
            else: print('REJECT %s: %s' % (name, (run.stdout.strip().splitlines() or ['abort'])[-1]))
            records.append({'name': name, 'returncode': run.returncode, 'unit_sha256': hashlib.sha256(c.read_bytes()).hexdigest()})
        (out / 'results.json').write_text(json.dumps({'themes': [n for n, _ in themelist], 'cases': records}, indent=2) + '\n')

if __name__ == '__main__': main()
