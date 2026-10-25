#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual FontDiag capture helper with locked metadata adapters, no pixels."""
from pathlib import Path
import argparse,re,subprocess
root=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
context=re.search(r'struct ScreenRenderContext\s*\{.*?\};',(root/'compiler/include/intuition/screens.h').read_text(encoding='latin1'),re.S).group(0)
source=(root/'arch/aarch64-apple/c/fontdiag_render.h').read_text()
prefix=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG;typedef uint16_t UWORD;typedef uintptr_t IPTR;typedef int BOOL;typedef void Object;
#define TRUE 1
#define FALSE 0
#define SA_RenderContext 164
struct TextAttr{char*ta_Name;UWORD ta_YSize;};
struct TTextAttr{char*tta_Name;UWORD tta_YSize;};
struct TextFont{UWORD tf_YSize;};struct Screen;
struct DrawInfo{struct Screen*dri_Screen;struct TextFont*dri_Font;};
struct Screen{struct{struct TextFont*Font;}RastPort;struct TextAttr*Font;};
'''+context+r'''
static unsigned depth,queries;static BOOL queryok=TRUE;
static const struct ScreenRenderContext*current;
static ULONG LockIBase(ULONG n){assert(!n&&!depth);return ++depth;}
static void UnlockIBase(ULONG n){assert(depth==1&&n==depth);depth--;}
static BOOL GetAttr(ULONG tag,Object*screen,IPTR*value){assert(depth==1&&tag==SA_RenderContext&&screen);queries++;*value=(IPTR)current;return queryok;}
'''
tests=r'''
int main(void){
 struct Screen screen={0},other={0};struct TextFont base={13},physical={13},wrong={17};
 struct TextAttr attr={"base.font",13};struct DrawInfo logical={&screen,&base},render={&screen,&physical};
 struct ScreenRenderContext context={.src_Scale=100,.src_LegacyScaleFactor=1,.src_BaseFont={"base.font",13},.src_LogicalDrawInfo=&logical,.src_PhysicalDrawInfo=&render};
 current=&context;screen.RastPort.Font=&physical;screen.Font=&attr;struct FontDiagRender sample;
 for(unsigned scale=100;scale<=400;scale+=25){context.src_Scale=scale;context.src_LegacyScaleFactor=(scale+50)/100;physical.tf_YSize=(13*scale+50)/100;assert(FontDiag_CaptureRender(&screen,&sample));assert(!depth&&sample.context==current&&sample.raster==&physical&&sample.logical.ta_YSize==13);}
 attr.ta_YSize=99;assert(sample.logical.ta_YSize==13);assert(!FontDiag_CaptureRender(&screen,&sample)&&!sample.context);attr.ta_YSize=13;
 screen.RastPort.Font=&wrong;assert(!FontDiag_CaptureRender(&screen,&sample));screen.RastPort.Font=&physical;
 logical.dri_Screen=&other;assert(!FontDiag_CaptureRender(&screen,&sample));logical.dri_Screen=&screen;
 render.dri_Screen=&other;assert(!FontDiag_CaptureRender(&screen,&sample));render.dri_Screen=&screen;
 current=NULL;assert(!FontDiag_CaptureRender(&screen,&sample));current=&context;
 queryok=FALSE;assert(!FontDiag_CaptureRender(&screen,&sample));queryok=TRUE;
 screen.Font=NULL;assert(!FontDiag_CaptureRender(&screen,&sample));screen.Font=&attr;
 context.src_LogicalDrawInfo=NULL;assert(!FontDiag_CaptureRender(&screen,&sample));context.src_LogicalDrawInfo=&logical;
 assert(!depth&&queries==21);
 puts("PASS actual FontDiag capture:13scales; raw raster/context agreement, logical attribute copy, foreign-screen/null/refused-query checks, IBase lifetime");return 0;
}
'''
mutations={
 'raw-raster-mismatch-accepted':source.replace('screen->RastPort.Font == context->src_PhysicalDrawInfo->dri_Font &&',''),
 'public-font-mismatch-accepted':source.replace('screen->Font->ta_YSize == context->src_BaseFont.tta_YSize','TRUE'),
 'foreign-owner-accepted':source.replace('context->src_LogicalDrawInfo->dri_Screen == screen &&','').replace('context->src_PhysicalDrawInfo->dri_Screen == screen &&',''),
 'query-outside-IBase':source.replace('    if (GetAttr','    UnlockIBase(lock);\n    if (GetAttr').replace('    UnlockIBase(lock);\n    return valid;','    return valid;'),
}
for label,body in [('production',source),*mutations.items()]:
 assert label=='production' or body!=source,label
 c=args.output/(label+'.c');exe=args.output/label;c.write_text(prefix+body+tests)
 r=subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],capture_output=True,text=True)
 (args.output/(label+'.compile.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
 r=subprocess.run([str(exe)],capture_output=True,text=True);(args.output/(label+'.log')).write_text(r.stdout+r.stderr)
 assert (r.returncode==0)==(label=='production'),r.stdout+r.stderr
 print(r.stdout.strip() if label=='production' else 'REJECT '+label)
