#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual menu reveal through108 and constructor refusal/capability boundaries.

Use --binding-root for the exact108 consumer source tree; adapters come from its
production-body sanitizer fixture. No native scheduler/pixels are simulated.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, tempfile
from menuscale_host import function
ROOT=Path(__file__).resolve().parents[4]

def builds(binding_root):
    fixture=binding_root/'developer/debug/test/Zune/scalebinding_host.py'
    ns={'__file__':str(fixture)}
    exec(fixture.read_text().split("with tempfile.TemporaryDirectory(prefix='scalebinding-')",1)[0],ns)
    menu=(ROOT/'rom/intuition/menutask.c').read_text()
    helper=function(menu,'ShowMenuScaleWindow')
    model='''\nstruct MenuHandlerData {BOOL presentationFailed;};
#define WINDOW_FRONTMOST ((struct Window *)1)
#define ShowWindow(w,o) public_show(w,o,IntuitionBase)
'''
    main=ns['main'].replace('assert(public_show(&win.window,NULL,&base));',
       'struct MenuHandlerData mhd={0};assert(ShowMenuScaleWindow(&mhd,&win.window,&base));',1)
    reveal=ns['head']+ns['binding']+ns['wrapper']+ns['show_action']+ns['bridge']+model+helper+main
    # Actual under-mouse constructor prefix through the typed spaces refusal.
    ctor=function(menu,'MakeMenuBarWin')
    start=ctor.index('        if (!(temprp = CloneRastPort(')
    end=ctor.index('        mhd->innerleft = msg.mdp_InnerLeft;',start)
    prefix=ctor[start:end]
    head=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int BOOL;typedef int16_t WORD;typedef int32_t LONG;typedef unsigned long IPTR;
#define TRUE 1
#define FALSE 0
#define MDM_GETMENUSPACES 3
#define TEXT_AMIGAKEY_SPACING 4
#define ITEXT_EXTRA_LEFT 2
#define ITEXT_EXTRA_RIGHT 2
#define ITEXT_EXTRA_TOP 1
#define ITEXT_EXTRA_BOTTOM 1
struct TextFont {int size;};struct RastPort {struct TextFont *Font;int TxHeight;};
struct Menu {struct Menu *NextMenu;char *MenuName;};
struct Screen {struct RastPort RastPort;};struct DrawInfo{struct TextFont *dri_Font;};
struct Image {int Width,Height;};struct View{int menuhborder,menuvborder;};
struct MenuHandlerData {struct Screen *scr;struct DrawInfo *dri;struct Menu *menu;
struct Image *submenuimage;struct View *view;int TrueColor,nummenubaritems,menubaritemwidth,menubaritemheight;};
struct IntuitionBase {int unused;};
struct mdpGetMenuSpaces{int MethodID,mdp_TrueColor,mdp_InnerLeft,mdp_InnerTop,mdp_InnerRight,mdp_InnerBottom,
mdp_ItemInnerLeft,mdp_ItemInnerTop,mdp_ItemInnerRight,mdp_ItemInnerBottom,mdp_MinWidth,mdp_MinHeight;};
static int live,failed,refused;
static struct RastPort *CloneRastPort(struct RastPort *rp){(void)rp;if(failed)return NULL;live++;return calloc(1,sizeof(*rp));}
static void FreeRastPort(struct RastPort *rp){assert(rp);live--;free(rp);}
static void SetFont(struct RastPort *rp,struct TextFont *f){rp->Font=f;rp->TxHeight=f->size;}
static WORD TextLength(struct RastPort *rp,const char *s,unsigned n){(void)rp;(void)s;return n*7;}
static LONG MenuScalePoint(struct View *v,LONG x){(void)v;return x;}
static BOOL GetMenuScaleSpaces(struct MenuHandlerData*m,struct mdpGetMenuSpaces*s,struct IntuitionBase*b){(void)m;(void)s;(void)b;refused++;return FALSE;}
'''
    refuse=head+'static void constructor_prefix(struct MenuHandlerData *mhd,struct IntuitionBase *IntuitionBase){struct RastPort *temprp;WORD w,maxw=0;struct Menu *menu;\n'+prefix+'}\n'+r'''
int main(void){struct TextFont f={13};struct Screen s={0};struct DrawInfo d={&f};
struct Menu menu={NULL,"File"};struct Image image={7,13};struct View v={2,1};struct IntuitionBase b={0};
struct MenuHandlerData m={.scr=&s,.dri=&d,.menu=&menu,.submenuimage=&image,.view=&v};
for(int i=0;i<20;i++){constructor_prefix(&m,&b);assert(live==0);}assert(refused==20);
failed=1;constructor_prefix(&m,&b);assert(live==0&&refused==20);puts("PASS constructor typed refusal and clone failure release");}
'''
    builtin=(ROOT/'rom/intuition/menudecorclass.c').read_text()
    shipped=(ROOT/'workbench/c/Decoration/menudecorclass.c').read_text()
    case=shipped.split('        case MDM_QUERYCAPABILITIES:',1)[1].split('        case MDM_INITSCALEMENU:',1)[0]
    caps=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uintptr_t IPTR;typedef unsigned long ULONG;
#define MDM_CAP_SCALE 1
#define MDM_QUERYCAPABILITIES 512
#define OCLASS(o) ((o)->cl)
typedef struct Class{int id;}Class;typedef struct Object{Class*cl;}Object;
struct mdpQueryCapabilities{ULONG MethodID;};
'''+function(builtin,'MenuDecorClass__MDM_QUERYCAPABILITIES')+'\nstatic IPTR shipped(Class*cl,Object*obj,struct mdpQueryCapabilities*msg){IPTR retval=0;switch(msg->MethodID){case MDM_QUERYCAPABILITIES:'+case+'}return retval;}\n'+r'''
int main(void){Class a={1},sub={2};Object exact={&a},foreign={&sub};struct mdpQueryCapabilities q={MDM_QUERYCAPABILITIES};
assert(MenuDecorClass__MDM_QUERYCAPABILITIES(&a,&exact,&q)==MDM_CAP_SCALE);
assert(MenuDecorClass__MDM_QUERYCAPABILITIES(&a,&foreign,&q)==0);
assert(shipped(&a,&exact,&q)==MDM_CAP_SCALE);assert(shipped(&a,&foreign,&q)==0);
puts("PASS exact-provider context-free capability, unknown subclass refused");}
'''
    return {'reveal':reveal,'refusal':refuse,'capability':caps}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--binding-root',type=Path,required=True);ap.add_argument('--output',type=Path);args=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='menuscale-boundary-') as temp:
        out=args.output or Path(temp);out.mkdir(parents=True,exist_ok=True);records=[]
        units=builds(args.binding_root.resolve())
        variants=[(name,body,True) for name,body in units.items()]
        variants += [('old-show',units['reveal'].replace('!ShowWindow(window, NULL)','!ShowWindow(window, WINDOW_FRONTMOST)'),False),
          ('old-rp-leak',units['refusal'].replace('            FreeRastPort(temprp);\n',''),False),
          ('inherited-builtin',units['capability'].replace('OCLASS(obj) == cl ? MDM_CAP_SCALE : 0','MDM_CAP_SCALE',1),False),
          ('inherited-shipped',units['capability'].replace('retval = OCLASS(obj) == cl ? MDM_CAP_SCALE : 0','retval = MDM_CAP_SCALE'),False)]
        for name,body,passes in variants:
            c=out/(name+'.c');exe=out/name;c.write_text(body)
            cmd=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)]
            subprocess.run(cmd,check=True);run=subprocess.run([str(exe)],capture_output=True,text=True);(out/(name+'.log')).write_text(run.stdout+run.stderr)
            assert (run.returncode==0)==passes,(name,run.stdout,run.stderr)
            records.append(dict(name=name,returncode=run.returncode,source_sha256=hashlib.sha256(body.encode()).hexdigest()))
        (out/'results.json').write_text(json.dumps(records,indent=2)+'\n')
        print('PASS actual108 reveal, constructor refusal cleanup, exact-provider capabilities; 4 old-body mutants rejected')
if __name__=='__main__':main()
