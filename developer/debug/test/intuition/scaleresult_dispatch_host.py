#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual screen OM_SET/SetScreenScale: tag order, receipt reset and font errors.
The synchronous engine boundary is an explicit adapter; actual transaction and
planner receipts are exercised by preparedscale_host and scaleresult_host.
"""
from pathlib import Path
import subprocess,tempfile,argparse
from scaleresult_fixture import result_definitions,result_helpers
ROOT=Path(__file__).resolve().parents[4]
def block(source,marker):
 start=source.index(marker);end=source.index('{',start)+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
prefix=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef uint32_t ULONG;typedef int32_t LONG;typedef uintptr_t IPTR;typedef uint8_t UBYTE;typedef int BOOL;typedef void*APTR;
#define TRUE 1
#define FALSE 0
#define SA_Dummy 1000
#define SA_Left 1
#define SA_Top 2
#define SA_ShowPointer 3
#define SA_Scale 4
#define SA_GammaRed 5
#define SA_GammaBlue 6
#define SA_GammaGreen 7
#define SA_AlphaPreCompositingHook 8
#define SF_Draggable 1
#define SPOS_RELATIVE 1
#define MM_SetScreenGamma 1
struct Window {char titlebuffer[256];};
#define IW(w) (w)
struct Library{int unused;};struct GfxBase{int unused;};
struct TextFont {int refs;};struct Hook{int unused;};
struct Screen {LONG LeftEdge,TopEdge;};
struct IntScreen {struct Screen Screen;ULONG Scale;struct TextFont*ScaleBaseFont;BOOL ShowPointer;ULONG SpecialFlags;struct{BOOL UseGammaControl;UBYTE*GammaTableR,*GammaTableB,*GammaTableG;}GammaControl;struct Hook*preAlphaCompHook;void*IMonitorNode;};
struct IntuitionBase {struct GfxBase*GfxBase;struct Library*UtilityBase;struct Screen*WorkBench;struct TextFont*ScreenFont;};
#define GetPrivIBase(b) (b)
#define GetPrivScreen(s) (s)
typedef struct{void*cl_UserData;}Class;typedef void Object;
#define INST_DATA(c,o) ((struct IntScreen*)(o))
struct TagItem {ULONG ti_Tag;IPTR ti_Data;};struct opSet{struct TagItem*ops_AttrList;};
struct msSetScreenGamma {ULONG MethodID;void*gamma;BOOL force;};struct IntuiActionMsg{int unused;};
static struct TagItem*NextTagItem(struct TagItem**state){struct TagItem*t=*state;if(!t->ti_Tag)return NULL;*state=t+1;return t;}
static IPTR GetTagData(ULONG key,IPTR fallback,struct TagItem*t){for(;t->ti_Tag;t++)if(t->ti_Tag==key)return t->ti_Data;return fallback;}
static void ScreenPosition(struct Screen*s,int f,int a,int b,int c,int d){(void)s;(void)f;(void)a;(void)b;(void)c;(void)d;}
static void DoMethodA(Object*o,void*m){(void)o;(void)m;}
static ULONG ClampScreenScale(ULONG scale){if(scale<100)return 100;if(scale>400)return 400;return ((scale+12)/25)*25;}
static int fontfail,enginefail,enginecalls;
static struct TextFont physical={1};
static struct TextFont*SafeReopenFont(struct IntuitionBase*b,struct TextFont**f){(void)b;if(fontfail)return NULL;(*f)->refs++;return*f;}
static struct TextFont*OpenScaledScreenFont(struct IntuitionBase*b,struct TextFont*f,ULONG scale){(void)b;(void)f;(void)scale;physical.refs++;return &physical;}
static void CloseFont(struct TextFont*f){assert(f->refs>0);f->refs--;}
static void DoSyncAction(APTR callback,struct IntuiActionMsg*message,struct IntuitionBase*b){((void(*)(void*,struct IntuitionBase*))callback)(message,b);}
'''
engine=r'''
static BOOL ApplyPreparedScreenScale(struct IntScreen*s,struct TextFont*f,struct TextFont*base,ULONG scale,void*layout,struct ScreenScaleResult*r,struct IntuitionBase*b){
 (void)f;(void)base;(void)layout;(void)b;enginecalls++;ScaleResultReset(r,scale,s->Scale);
 if(enginefail){ScaleResultSetStage(r,SSR_STAGE_DECORATION,NULL);return FALSE;}
 s->Scale=scale;ScaleResultFinish(r,SSR_SUCCESS,scale);return TRUE;
}
'''
tests=r'''
int main(void){struct TextFont basefont={1};struct IntScreen screen={.Scale=100,.ScaleBaseFont=&basefont};struct IntuitionBase base={0};Class cl={&base};struct ScreenScaleResult result;
 for(int order=0;order<2;order++){
  struct TagItem tags[3]={{SA_Scale,200},{SA_ScaleResult,(IPTR)&result},{0,0}};
  if(order){struct TagItem t=tags[0];tags[0]=tags[1];tags[1]=t;}
  struct opSet msg={tags};screen.Scale=100;enginefail=1;memset(&result,0xa5,sizeof result);
  assert(!ScreenClass__OM_SET(&cl,(Object*)&screen,&msg));assert(result.ssr_Status==SSR_FAILED&&result.ssr_Stage==SSR_STAGE_DECORATION&&result.ssr_Reason==SSR_REASON_PREPARE_FAILED&&result.ssr_RequestedScale==200&&result.ssr_AppliedScale==100&&!result.ssr_Window);
  enginefail=0;assert(ScreenClass__OM_SET(&cl,(Object*)&screen,&msg));assert(result.ssr_Status==SSR_SUCCESS&&result.ssr_AppliedScale==200&&!result.ssr_MinHeight);
  assert(!ScreenClass__OM_SET(&cl,(Object*)&screen,&msg));assert(result.ssr_Status==SSR_UNCHANGED&&result.ssr_AppliedScale==200&&!result.ssr_Window);
  screen.Scale=100;fontfail=1;assert(!ScreenClass__OM_SET(&cl,(Object*)&screen,&msg));fontfail=0;
  assert(result.ssr_Status==SSR_FAILED&&result.ssr_Stage==SSR_STAGE_FONT&&result.ssr_Reason==SSR_REASON_PREPARE_FAILED&&result.ssr_AppliedScale==100);
 }
 struct TagItem only[]={{SA_ScaleResult,(IPTR)&result},{0,0}};struct opSet message={only};memset(&result,0xa5,sizeof result);assert(!ScreenClass__OM_SET(&cl,(Object*)&screen,&message));struct ScreenScaleResult zero={0};assert(!memcmp(&result,&zero,sizeof result));
 struct TagItem absent[]={{SA_Scale,150},{0,0}};message.ops_AttrList=absent;assert(ScreenClass__OM_SET(&cl,(Object*)&screen,&message)&&screen.Scale==150);
 puts("PASS actual OM_SET: both tag orders, failed/success/noop/reused result, font refusal, result-only and optional output");return 0;}
'''
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path);args=parser.parse_args()
 source=(ROOT/'rom/intuition/screenclass.c').read_text();start=source.index('struct ScaleScreenAction\n');end=source.index('IPTR ScreenClass__OM_SET(',start)
 body=source[start:end]+block(source,'IPTR ScreenClass__OM_SET(')
 import contextlib
 with (contextlib.nullcontext(str(args.output.resolve())) if args.output else tempfile.TemporaryDirectory()) as directory:
  out=Path(directory);out.mkdir(parents=True,exist_ok=True)
  variants={'production':body,'missing-prescan':body.replace('GetTagData(SA_ScaleResult, 0, msg->ops_AttrList)','0'),'stale-noop':body.replace('ScaleResultFinish(result, SSR_UNCHANGED, screen->Scale);',''),'stale-result-only':body.replace('if (scaleResult) *scaleResult = (struct ScreenScaleResult){0};','')}
  for name,changed in variants.items():
   c=out/(name+'.c');exe=out/name;c.write_text(prefix+result_definitions(ROOT)+result_helpers(ROOT)+engine+changed+tests)
   subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Werror','-Wno-unused-variable','-Wno-unused-function','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
   run=subprocess.run([str(exe)],capture_output=True,text=True);(out/(name+'.log')).write_text(run.stdout+run.stderr)
   assert (run.returncode==0)==(name=='production'),name
   print(run.stdout if name=='production' else 'REJECT '+name,end='\n')
if __name__=='__main__':main()
