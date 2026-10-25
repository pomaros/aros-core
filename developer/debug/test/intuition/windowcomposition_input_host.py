#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
# Production-body extraction exercises coordinate routing, gadget hits and delta conservation.
from pathlib import Path
import subprocess, json, tempfile
root=Path(__file__).resolve().parents[4]/"rom/intuition"
temporary=tempfile.TemporaryDirectory(prefix="aros-composition-input-")
out=Path(temporary.name)
def body(file, signature):
 s=(root/file).read_text();a=s.index(signature);start=s.index('{',a);level=1;i=start+1
 while level:
  if s[i]=='{':level+=1
  if s[i]=='}':level-=1
  i+=1
 return s[a:i]
s='''#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
typedef int LONG;typedef short WORD;typedef unsigned short UWORD;typedef int BOOL;
#define TRUE 1
#define FALSE 0
struct Screen {WORD MouseX,MouseY;struct Screen *NextScreen;struct Window *FirstWindow;};
struct Window {struct Screen *WScreen;WORD LeftEdge,TopEdge,BorderLeft,BorderTop,MouseX,MouseY,GZZMouseX,GZZMouseY;struct Window *NextWindow;UWORD factor,CompositionDeltaFactor;WORD CompositionDeltaX,CompositionDeltaY,CompositionMouseDeltaX,CompositionMouseDeltaY;};
#define IW(w) (w)
#define IntWindow Window
struct IntuitionBase {struct Screen *FirstScreen;};
struct IIHData {WORD DeltaMouseX,DeltaMouseY;};
struct IBox {WORD Left,Top,Width,Height;};
struct Requester {int unused;};
struct Gadget {struct IBox box;};
UWORD WindowCompositionFactor(struct Window *w){return w->factor;}
LONG floor_div(LONG n,LONG f){return n>=0?n/f:-((-n+f-1)/f);}
BOOL WindowCompositionMouse(struct Window *w,LONG x,LONG y,LONG *a,LONG *b){*a=floor_div(x,w->factor)-w->LeftEdge;*b=floor_div(y,w->factor)-w->TopEdge;return TRUE;}
void GetWinGadgetIBox(struct Gadget*g,struct Screen*s,struct Window*w,struct Requester*r,struct IBox*b){*b=g->box;}
void GetScrGadgetIBox(struct Gadget*g,struct Screen*s,struct Window*w,struct Requester*r,struct IBox*b){*b=g->box;}
'''
s+=body('intuition_misc.c','void UpdateMouseCoords(')+'\n'
s+=body('inputhandler_support.c','BOOL InsideGadget(')+'\n'
s+=body('windowclasses.c','static LONG windowmouse(')+'\n'
s+=body('windowclasses.c','static LONG windowextent(')+'\n'
src=(root/'inputhandler.c').read_text();a=src.index('            struct Screen *deltaScreen;');b=src.index('\n#if !SINGLE_SETPOINTERPOS_PER_EVENTLOOP',a)
s+='void UpdateDeltas(struct IntuitionBase *IntuitionBase,struct IIHData *iihdata) {\n'+src[a:b]+'\n'
s+='''int main(void){
 struct Screen screen={0};struct Window wins[4]={0};struct IntuitionBase ib={&screen};struct IIHData input={0};struct Gadget gad={{20,15,30,12}};int checks=0;
 screen.FirstWindow=&wins[0];
 for(int f=1;f<=4;f++){struct Window*w=&wins[f-1];w->WScreen=&screen;w->LeftEdge=31;w->TopEdge=17;w->BorderLeft=4;w->BorderTop=12;w->factor=f;w->NextWindow=f<4?&wins[f]:0;
  for(int x=-9;x<90*f;x++)for(int y=-5;y<50*f;y++){screen.MouseX=x;screen.MouseY=y;UpdateMouseCoords(w);assert(w->MouseX==floor_div(x,f)-31);assert(w->MouseY==floor_div(y,f)-17);assert(w->GZZMouseX==w->MouseX-4);assert(w->GZZMouseY==w->MouseY-12);assert(windowmouse(w,x)==floor_div(x,f));checks++;}
  for(int x=(31+18)*f;x<(31+53)*f;x++)for(int y=(17+13)*f;y<(17+30)*f;y++){int expected=x>=(31+20)*f&&x<(31+50)*f&&y>=(17+15)*f&&y<(17+27)*f;assert(InsideGadget(&screen,w,0,&gad,x,y)==expected);checks++;}
  assert(windowextent(w,2560)==2560/f);
 }
 int totalx[4]={0},totaly[4]={0},px=0,py=0;
 for(int n=0;n<10000;n++){input.DeltaMouseX=n%17-8;input.DeltaMouseY=n%11-5;px+=input.DeltaMouseX;py+=input.DeltaMouseY;UpdateDeltas(&ib,&input);for(int i=0;i<4;i++){struct Window*w=&wins[i];totalx[i]+=w->CompositionMouseDeltaX;totaly[i]+=w->CompositionMouseDeltaY;assert(totalx[i]*w->factor+w->CompositionDeltaX==px);assert(totaly[i]*w->factor+w->CompositionDeltaY==py);assert(abs(w->CompositionDeltaX)<w->factor);assert(abs(w->CompositionDeltaY)<w->factor);checks++;}}
 /* A factor change clears old remainders before consuming the new event. */
 wins[1].factor=4;input.DeltaMouseX=1;input.DeltaMouseY=-1;UpdateDeltas(&ib,&input);assert(wins[1].CompositionMouseDeltaX==0&&wins[1].CompositionDeltaX==1);assert(wins[1].CompositionMouseDeltaY==0&&wins[1].CompositionDeltaY==-1);
 puts("PASS production input bodies: mouse mapping, GZZ, every gadget pixel at factors1/2/3/4, signed delta conservation, factor reset");printf("checks=%d\\n",checks);return 0;}
'''
(out/'input-host.c').write_text(s)
r=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',str(out/'input-host.c'),'-o',str(out/'input-host')],capture_output=True,text=True);(out/'input-host-build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
r=subprocess.run([str(out/'input-host')],capture_output=True,text=True);(out/'input-host.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);assert r.returncode==0
