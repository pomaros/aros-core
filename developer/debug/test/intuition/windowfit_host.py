#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Compile the production fit planner against deterministic geometry/allocator fakes."""
from pathlib import Path
from scaleresult_fixture import result_definitions, result_helpers
import subprocess
import tempfile
import re
import argparse
import contextlib
import hashlib
import json
ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / 'rom/intuition/windowfit.c'
PREAMBLE = r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef int16_t WORD; typedef int32_t LONG; typedef uint32_t ULONG; typedef uint16_t UWORD;
typedef uint8_t UBYTE; typedef int BOOL; typedef int64_t QUAD; typedef uint64_t UQUAD; typedef uintptr_t IPTR;
#define TRUE 1
#define FALSE 0
#define D(x)
#define MEMF_PUBLIC 0
#define MEMF_CLEAR 0
#define WINDOWFIT_X 1
#define WINDOWFIT_Y 2
#define WINDOWFIT_WIDTH 4
#define WINDOWFIT_HEIGHT 8
struct Layer_Info { int Flags; };
struct Layer { struct Layer_Info *LayerInfo; struct { LONG MinX, MinY; } bounds; LONG Width,Height; };
struct Screen { UBYTE *Title,*DefaultTitle; LONG Width,Height; ULONG Scale; UWORD FitBatch; struct { void *BitMap; } RastPort; struct Layer_Info LayerInfo; struct Window *FirstWindow; };
struct Window { char titlebuffer[256]; UBYTE *Title,*ScreenTitle; ULONG Flags; LONG LeftEdge,TopEdge,Width,Height,RelLeftEdge,RelTopEdge,GZZWidth,GZZHeight;
UWORD MinWidth,MinHeight,MaxWidth,MaxHeight,BorderLeft,BorderRight,BorderTop,BorderBottom;
BOOL ScaleAware,FitCanonicalValid,ScaleGeometryEnabled; struct AwareGeometry ScaleGeometry; const struct ScreenRenderContext *LayoutMinContext; LONG LayoutMinWidth,LayoutMinHeight; UWORD FitAutomatic; LONG FitLeft,FitTop,FitWidth,FitHeight;
struct Window *parent,*NextWindow; struct Screen *WScreen; struct Layer layer; };
struct NewWindow { LONG LeftEdge,TopEdge,Width,Height,MinWidth,MinHeight,MaxWidth,MaxHeight; };
struct LayersBase { int unused; }; struct IntuitionBase { struct LayersBase *LayersBase; struct { ULONG ic_Flags; } IControlPrefs; };
#define ICF_OFFSCREENLAYERS 1
#define LIFLG_SUPPORTS_OFFSCREEN_LAYERS 1
#define IW(w) (w)
#define IS(s) (s)
#define IS_HIDD_BM(b) ((b)!=NULL)
#define GetPrivIBase(b) (b)
#define LOCK_REFRESH(s) ((void)LayersBase,(void)(s),++refreshlocks)
#define UNLOCK_REFRESH(s) ((void)(s),assert(refreshlocks>0),--refreshlocks)
static int refreshlocks;
static int allocations, failalloc, moves, changes, sizes, failmove;
static ULONG limitslock;
#define LockIBase(value) ((void)IntuitionBase,lock_base(value))
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
#define DEBUG_WINDOWLIMITS(x)
#define SANITY_CHECKR(w,result) if(!(w))return result;
static ULONG lock_base(ULONG ignored) {(void)ignored;assert(!limitslock);return ++limitslock;}
static void UnlockIBase(ULONG lock) {assert(lock==limitslock&&lock==1);limitslock--; }
static struct Window *order[1024];
static void *AllocVec(size_t n,int flags) { (void)flags; if(failalloc)return NULL; void *p=calloc(1,n); if(p)allocations++;return p; }
static void FreeVec(void *p) {if(p)allocations--;free(p);}
static struct Layer *WindowCompositionLayer(struct Window *w) {return &w->layer;}
void WindowFitRecordChange(struct Window *,ULONG);
void FixWindowCoords(struct Window *,LONG *,LONG *,LONG *,LONG *,struct IntuitionBase *);
static BOOL DoMoveSizeWindow(struct Window *w,LONG x,LONG y,LONG width,LONG height,BOOL send,struct IntuitionBase *b) {
(void)send;assert(w->WScreen->FitBatch);FixWindowCoords(w,&x,&y,&width,&height,b);order[moves++]=w;if(failmove)return FALSE;w->LeftEdge=x;w->TopEdge=y;w->Width=width;w->Height=height;WindowFitRecordChange(w,15);return TRUE;}
static void NotifyWindowGeometry(struct Window *w,BOOL resized,struct IntuitionBase *b) {(void)w;(void)b;changes++;sizes+=resized;}
'''
TESTS = r'''
static struct Screen screen = {.Width=2560,.Height=1600,.Scale=100,.RastPort={ (void*)1 }};
static struct IntuitionBase base;
static struct Window makewin(LONG x,LONG y,LONG w,LONG h) {
struct Window a={0};a.WScreen=&screen;a.LeftEdge=x;a.TopEdge=y;a.Width=w;a.Height=h;
a.MinWidth=20;a.MinHeight=20;a.MaxWidth=32767;a.MaxHeight=32767;
a.layer.LayerInfo=&screen.LayerInfo;a.layer.bounds.MinX=x;a.layer.bounds.MinY=y;a.layer.Width=w;a.layer.Height=h;return a;}
static void scale(ULONG value) {struct WindowFitTransaction *t=NULL;assert(WindowFitPreflight(&screen,value,NULL,0,NULL,&t,NULL,&base));screen.Scale=value;assert(WindowFitApply(t,FALSE,&base));WindowFitFinish(t,TRUE,&base);assert(!allocations&&!screen.FitBatch);}
static void aware_install(ULONG percent) {
 struct WindowFitFrameMetrics frames[8];unsigned n=0;
 for(struct Window *w=screen.FirstWindow;w;w=w->NextWindow)
  frames[n++]=(struct WindowFitFrameMetrics){w,w->BorderLeft,w->BorderTop,w->BorderRight,w->BorderBottom};
 struct WindowFitTransaction *t=NULL;
 assert(WindowFitPreflight(&screen,percent,frames,n,NULL,&t,NULL,&base));
 WindowFitInstall(t,FALSE);screen.Scale=percent;WindowFitFinish(t,FALSE,&base);
 assert(!allocations);
}
static void apply_initial(struct Window *w,struct NewWindow *n) {
 w->RelLeftEdge=n->LeftEdge;w->RelTopEdge=n->TopEdge;
 w->LeftEdge=n->LeftEdge+(w->parent?w->parent->LeftEdge:0);
 w->TopEdge=n->TopEdge+(w->parent?w->parent->TopEdge:0);
 w->Width=n->Width;w->Height=n->Height;w->MinWidth=n->MinWidth;w->MinHeight=n->MinHeight;
 w->MaxWidth=n->MaxWidth;w->MaxHeight=n->MaxHeight;
 w->GZZWidth=w->Width-w->BorderLeft-w->BorderRight;w->GZZHeight=w->Height-w->BorderTop-w->BorderBottom;
}
static void aware_contract(void) {
 struct Screen saved_screen=screen;
 screen.Width=1440;screen.Height=900;screen.Scale=100;
 struct Window a=makewin(100,120,640,240),control=makewin(30,40,100,100);
 a.ScaleAware=TRUE;a.BorderLeft=4;a.BorderTop=20;a.BorderRight=4;a.BorderBottom=4;
 control.ScaleAware=TRUE;control.GZZWidth=control.Width;control.GZZHeight=control.Height;a.NextWindow=&control;screen.FirstWindow=&a;
 struct Window saved_control=control;
 struct ScaleWindowGeometry requested={100,120,640,240,80,80,-1,-1};struct NewWindow nw={0};
 assert(WindowFitInitializeGeometry(&a,&requested,1440,900,TRUE,&nw));apply_initial(&a,&nw);
 struct AwareGeometry original=a.ScaleGeometry;
 for(unsigned i=0;i<20;i++) {
  aware_install(400);assert(a.Width==1440&&a.Height==900&&a.LeftEdge==0&&a.TopEdge==0);
  assert(a.MinWidth==320&&a.MinHeight==320);
  aware_install(100);assert(a.Width==640&&a.Height==240&&a.LeftEdge==100&&a.TopEdge==120);
  assert(a.MinWidth==80&&a.MinHeight==80&&!memcmp(&original,&a.ScaleGeometry,sizeof original));
  assert(!memcmp(&control,&saved_control,sizeof control));
 }
 /* Explicit physical move/size at200 retains exact odd pixels independently. */
 aware_install(200);a.LeftEdge=a.RelLeftEdge=203;a.Width=503;
 WindowFitRecordChange(&a,WINDOWFIT_X|WINDOWFIT_WIDTH);
 for(unsigned i=0;i<20;i++) {
  aware_install(100);assert(a.LeftEdge==102&&a.Width==252&&a.TopEdge==120&&a.Height==240);
  aware_install(200);assert(a.LeftEdge==203&&a.Width==503&&a.TopEdge==240&&a.Height==480);
 }
 /* All limits and geometry reverse together, including implicit GZZ extents. */
 struct Window before=a;struct WindowFitTransaction *t=NULL;
 struct WindowFitFrameMetrics frame={&a,4,20,4,4};
 assert(WindowFitPreflight(&screen,100,&frame,1,NULL,&t,NULL,&base));
 WindowFitInstall(t,FALSE);assert(a.MinWidth==80);WindowFitInstall(t,TRUE);
 assert(!memcmp(&a,&before,sizeof a));WindowFitFinish(t,FALSE,&base);
 /* Public limit API stays physical and records only accepted limits. */
 assert(WindowLimits(&a,100,90,503,0,&base)&&!limitslock);
 assert(a.ScaleGeometry.axis[AG_MINWIDTH].value==100&&a.ScaleGeometry.axis[AG_MINWIDTH].scale==200);
 assert(a.ScaleGeometry.axis[AG_MAXWIDTH].value==503&&a.ScaleGeometry.axis[AG_MAXWIDTH].scale==200);
 aware_install(100);assert(a.MinWidth==50&&a.MinHeight==45&&a.MaxWidth==252);
 aware_install(200);assert(a.MinWidth==100&&a.MinHeight==90&&a.MaxWidth==503&&a.Width==503);
 assert(!WindowLimits(&a,600,0,0,0,&base)&&!limitslock);
 assert(a.ScaleGeometry.axis[AG_MINWIDTH].value==100);
 assert(WindowLimits(&a,0,0,(UWORD)-1,0,&base));
 assert(a.ScaleGeometry.axis[AG_MAXWIDTH].value==-1);
 before=a;
 /* Missing proposed frame and impossible minimum fail without live writes. */
 assert(!WindowFitPreflight(&screen,100,NULL,0,NULL,&t,NULL,&base)&&!t);
 assert(!memcmp(&a,&before,sizeof a)&&!allocations);
 screen.Width=150;before=a;
 assert(!WindowFitPreflight(&screen,400,&frame,1,NULL,&t,NULL,&base)&&!t);
 assert(!memcmp(&a,&before,sizeof a)&&!allocations);screen.Width=1440;
 /* Constructor persisted200 saves requested size before viewport fitting. */
 screen.Scale=200;a=makewin(0,0,1,1);a.ScaleAware=TRUE;screen.FirstWindow=&a;
 assert(WindowFitInitializeGeometry(&a,&requested,1440,900,TRUE,&nw));apply_initial(&a,&nw);
 assert(a.LeftEdge==160&&a.Width==1280&&a.Height==480);
 assert(WindowFitPrepareOpen(&a,&base));aware_install(100);
 assert(a.LeftEdge==100&&a.TopEdge==120&&a.Width==640&&a.Height==240);
 /* A constructor already fitted at400 must retain the requested reset box. */
 screen.Scale=400;a=makewin(0,0,1,1);a.ScaleAware=TRUE;screen.FirstWindow=&a;
 assert(WindowFitInitializeGeometry(&a,&requested,1440,900,TRUE,&nw));apply_initial(&a,&nw);
 assert(a.Width==1440&&a.Height==900);aware_install(100);
 assert(a.LeftEdge==100&&a.TopEdge==120&&a.Width==640&&a.Height==240);
 /* Proposed borders take part in preflight instead of the live frame. */
 a.ScaleGeometry.axis[AG_MINHEIGHT].value=1;
 frame=(struct WindowFitFrameMetrics){&a,4,700,4,4};
 assert(WindowFitPreflight(&screen,100,&frame,1,NULL,&t,NULL,&base));
 assert(t->plans[0].desired.minheight==705&&t->plans[0].desired.height==705);
 WindowFitFinish(t,FALSE,&base);
 /* Fill dimensions retain exact physical viewport anchors at their birth. */
 screen.Scale=200;requested=(struct ScaleWindowGeometry){0,0,-1,-1,80,80,-1,-1};
 assert(WindowFitInitializeGeometry(&a,&requested,1440,900,TRUE,&nw));apply_initial(&a,&nw);
 assert(a.Width==1440&&a.Height==900);aware_install(100);assert(a.Width==720&&a.Height==450);
 requested.MinWidth=-1;before=a;
 assert(!WindowFitInitializeGeometry(&a,&requested,1440,900,TRUE,&nw));
 assert(!memcmp(&a,&before,sizeof a));
 /* A scaled child is fitted after its proposed parent, even in front order. */
 screen.Scale=100;a=makewin(0,0,1,1);a.ScaleAware=TRUE;
 requested=(struct ScaleWindowGeometry){100,120,640,240,80,80,-1,-1};
 assert(WindowFitInitializeGeometry(&a,&requested,1440,900,TRUE,&nw));apply_initial(&a,&nw);
 struct Window child=makewin(0,0,1,1);child.ScaleAware=TRUE;child.parent=&a;
 requested=(struct ScaleWindowGeometry){30,40,200,100,20,20,-1,-1};
 assert(WindowFitInitializeGeometry(&child,&requested,a.Width,a.Height,TRUE,&nw));apply_initial(&child,&nw);
 child.NextWindow=&a;screen.FirstWindow=&child;
 aware_install(400);assert(child.LeftEdge==120&&child.TopEdge==160&&child.Width==800&&child.Height==400);
 aware_install(100);assert(child.LeftEdge==130&&child.TopEdge==160&&child.RelLeftEdge==30&&child.RelTopEdge==40&&child.Width==200);
 assert(!allocations);screen=saved_screen;
 puts("PASS actual aware WindowFit: fitted reset, odd physical user axes, limits rollback, proposed borders, saved200 birth, fill, unchanged aware control, no writes on rejection");
}
int main(void) {
aware_contract();
/* The production constructor consumes the descriptor, not the ordinary tags. */
struct Screen saved_constructor_screen=screen;
screen.Width=1440;screen.Height=900;screen.Scale=400;
struct ScaleWindowGeometry descriptor={0,0,1000,600,0,0,-1,-1};
constructor_geometry=&descriptor;
struct Window born=makewin(0,0,1,1);born.ScaleAware=TRUE;screen.FirstWindow=&born;
assert(ConstructorDimensions(&born,NULL,7,9,0,0,TRUE));
assert(born.Width==1440&&born.Height==900&&born.ScaleGeometryEnabled);
assert(born.ScaleGeometry.axis[AG_MINWIDTH].value==1440&&born.ScaleGeometry.axis[AG_MINWIDTH].scale==400);
assert(born.ScaleGeometry.axis[AG_MINHEIGHT].value==900&&born.ScaleGeometry.axis[AG_MINHEIGHT].scale==400);
aware_install(100);assert(born.Width==1000&&born.Height==600&&born.MinWidth==360&&born.MinHeight==225);
/* Implicit maxima also anchor the accepted extent, independently of minima. */
screen.Scale=400;descriptor.MaxWidth=descriptor.MaxHeight=0;
born=makewin(0,0,1,1);born.ScaleAware=TRUE;
assert(ConstructorDimensions(&born,NULL,7,9,0,0,TRUE));
assert(born.ScaleGeometry.axis[AG_MAXWIDTH].value==1440&&born.ScaleGeometry.axis[AG_MAXWIDTH].scale==400);
aware_install(100);assert(born.Width==360&&born.Height==225&&born.MaxWidth==360&&born.MaxHeight==225);
/* AutoAdjustFALSE rejects fitting without changing the private state. */
screen.Scale=400;descriptor.MinWidth=descriptor.MinHeight=20;descriptor.MaxWidth=descriptor.MaxHeight=-1;
born=makewin(0,0,1,1);born.ScaleAware=TRUE;struct Window untouched=born;
assert(!ConstructorDimensions(&born,NULL,7,9,0,0,FALSE));
assert(!memcmp(&born,&untouched,sizeof born));
/* A valid box and default limits still succeed with AutoAdjustFALSE. */
descriptor=(struct ScaleWindowGeometry){10,10,100,60,0,0,0,0};
assert(ConstructorDimensions(&born,NULL,7,9,0,0,FALSE));
assert(born.LeftEdge==40&&born.TopEdge==40&&born.Width==400&&born.Height==240);
/* Negative positions are not silently clamped in that mode either. */
descriptor.Left=-1;born=makewin(0,0,1,1);born.ScaleAware=TRUE;untouched=born;
assert(!ConstructorDimensions(&born,NULL,7,9,0,0,FALSE)&&!memcmp(&born,&untouched,sizeof born));
constructor_geometry=NULL;screen=saved_constructor_screen;
puts("PASS actual descriptor constructor: fitted zero minima/maxima birth anchors, reset, AutoAdjustFALSE refusal and valid box");
struct Window a=makewin(2300,1400,200,100);screen.FirstWindow=&a;
assert(WindowFitPrepareOpen(&a,&base)&&a.FitCanonicalValid&&a.FitLeft==2300);for(int i=0;i<30;i++){scale(200);assert(a.LeftEdge==1080&&a.TopEdge==700&&a.Width==200);scale(100);assert(a.LeftEdge==2300&&a.TopEdge==1400&&a.Width==200);}
a=makewin(0,0,2500,1500);screen.FirstWindow=&a;assert(WindowFitPrepareOpen(&a,&base));int oldsizes=sizes;scale(200);assert(a.Width==1280&&a.Height==800&&sizes==oldsizes+1);scale(100);assert(a.Width==2500&&a.Height==1500);
/* Validation of a later window is atomic: no geometry or canonical changes. */
struct Window b=makewin(0,0,1400,100);b.MinWidth=1400;b.MaxWidth=1400;a.NextWindow=&b;
struct Window beforea=a,beforeb=b;struct WindowFitTransaction *t=(void*)1;int oldmoves=moves;
assert(!WindowFitPreflight(&screen,200,NULL,0,NULL,&t,NULL,&base));assert(!t&&!allocations&&moves==oldmoves);assert(!memcmp(&a,&beforea,sizeof a)&&!memcmp(&b,&beforeb,sizeof b));
failalloc=1;assert(!WindowFitPreflight(&screen,100,NULL,0,NULL,&t,NULL,&base)&&!t);failalloc=0;a.NextWindow=NULL;
/* A default/full-width birth at 200 gets constructor logical implicit min/max. */
screen.Scale=200;a=makewin(0,0,1,1);LONG vw,vh;assert(WindowFitViewport(&a,&vw,&vh)&&vw==1280&&vh==800);a.Width=vw;a.Height=vh;a.MinWidth=a.MaxWidth=vw;a.MinHeight=a.MaxHeight=vh;
assert(WindowFitPrepareOpen(&a,&base)&&a.FitWidth==1280);scale(100);assert(a.Width==1280&&a.Height==800);
/* User changes only X/width: untouched canonical Y/height survive autofit. */
a=makewin(2200,1400,200,100);assert(WindowFitPrepareOpen(&a,&base));scale(200);a.LeftEdge=123;a.Width=300;WindowFitRecordChange(&a,WINDOWFIT_X|WINDOWFIT_WIDTH);scale(100);assert(a.LeftEdge==123&&a.TopEdge==1400&&a.Width==300&&a.Height==100);
/* Child-before-parent traversal, odd physical phase, aware parent unmodified. */
a=makewin(101,51,401,303);a.ScaleAware=TRUE;b=makewin(151,101,150,100);b.parent=&a;b.RelLeftEdge=50;b.RelTopEdge=50;b.NextWindow=&a;screen.FirstWindow=&b;screen.Scale=100;
assert(WindowFitPrepareOpen(&b,&base));assert(WindowFitPreflight(&screen,200,NULL,0,NULL,&t,NULL,&base));LONG bi=find_plan(t,&b);assert(t->plans[bi].physical.left==201&&t->plans[bi].physical.top==151);assert(t->plans[bi].desired.left==100&&t->plans[bi].desired.top==75);WindowFitFinish(t,FALSE,&base);
/* Legacy parent shrinks first in plan, then child's local viewport fits. */
a=makewin(0,0,2000,1400);b=makewin(1800,1300,200,100);b.parent=&a;b.RelLeftEdge=1800;b.RelTopEdge=1300;b.NextWindow=&a;screen.FirstWindow=&b;assert(WindowFitPrepareOpen(&a,&base));assert(WindowFitPrepareOpen(&b,&base));assert(WindowFitPreflight(&screen,200,NULL,0,NULL,&t,NULL,&base));bi=find_plan(t,&b);assert(t->plans[bi].desired.relleft==1080&&t->plans[bi].desired.reltop==700);
oldmoves=moves;screen.Scale=200;assert(WindowFitApply(t,FALSE,&base));assert(order[oldmoves]==&a&&order[oldmoves+1]==&b);assert(b.RelLeftEdge==1080&&b.RelTopEdge==700&&b.LeftEdge==1080&&b.TopEdge==700);assert(WindowFitApply(t,TRUE,&base));WindowFitFinish(t,FALSE,&base);screen.Scale=100;
/* A failed common mover does not publish planned relative coordinates. */
assert(WindowFitPreflight(&screen,200,NULL,0,NULL,&t,NULL,&base));LONG oldrel=a.RelLeftEdge;failmove=1;assert(!WindowFitApply(t,FALSE,&base));assert(a.RelLeftEdge==oldrel&&!screen.FitBatch);failmove=0;WindowFitFinish(t,FALSE,&base);
/* Broken families cannot be partially applied. */
a.parent=&b;assert(!WindowFitPreflight(&screen,200,NULL,0,NULL,&t,NULL,&base)&&!t&&!allocations);a.parent=NULL;screen.FirstWindow=&b;b.NextWindow=NULL;assert(!WindowFitPreflight(&screen,200,NULL,0,NULL,&t,NULL,&base)&&!t&&!allocations);
/* Explicit client move/resize and opaque drag share this actual constraint. */
screen.FirstWindow=&a;a=makewin(0,0,100,100);
for(ULONG f=2;f<=4;f++) {screen.Scale=f*100;LONG x=2500,y=1500,w=2400,h=1500;
FixWindowCoords(&a,&x,&y,&w,&h,&base);assert(x==0&&y==0&&w==2560/(LONG)f&&h==1600/(LONG)f);
x=2500;y=1500;w=100;h=100;assert(WindowFitConstrain(&a,&x,&y,&w,&h,&base));assert(x==2560/(LONG)f-100&&y==1600/(LONG)f-100);}
screen.Scale=200;a.ScaleAware=TRUE;LONG x=2200,y=1400,w=100,h=100;assert(WindowFitConstrain(&a,&x,&y,&w,&h,&base)&&x==2200&&y==1400);
a.ScaleAware=FALSE;screen.Scale=100;assert(WindowFitConstrain(&a,&x,&y,&w,&h,&base)&&x==2200);
screen.Scale=200;a.ScaleAware=TRUE;a.layer.bounds.MinX=101;a.layer.bounds.MinY=51;a.layer.Width=401;a.layer.Height=303;
b=makewin(50,25,100,100);b.parent=&a;x=1000;y=1000;w=100;h=100;assert(WindowFitConstrain(&b,&x,&y,&w,&h,&base));assert(x==150&&y==76);
/* Live offscreen policy at 100 must survive zoom then reset exactly. */
for(int flags=0;flags<4;flags++) for(int sign=0;sign<2;sign++) {
 screen.Scale=100;screen.LayerInfo.Flags=LIFLG_SUPPORTS_OFFSCREEN_LAYERS;base.IControlPrefs.ic_Flags=ICF_OFFSCREENLAYERS;
 a=makewin(100,100,200,100);screen.FirstWindow=&a;assert(WindowFitPrepareOpen(&a,&base));
 x=sign ? -100 : 2500;y=sign ? -50 : 1550;w=200;h=100;FixWindowCoords(&a,&x,&y,&w,&h,&base);
 a.LeftEdge=x;a.TopEdge=y;WindowFitRecordChange(&a,WINDOWFIT_X|WINDOWFIT_Y);
 assert(a.FitLeft==x&&a.FitTop==y);scale(200);assert(a.LeftEdge>=0&&a.TopEdge>=0&&a.LeftEdge+a.Width<=1280&&a.TopEdge+a.Height<=800);
 base.IControlPrefs.ic_Flags=(flags&1) ? ICF_OFFSCREENLAYERS : 0;
 screen.LayerInfo.Flags=(flags&2) ? LIFLG_SUPPORTS_OFFSCREEN_LAYERS : 0;
 scale(100);assert(a.LeftEdge==(flags==3 ? x : (sign ? 0 : 2360)));assert(a.TopEdge==(flags==3 ? y : (sign ? 0 : 1500)));
}
/* A child's saved offscreen placement stays relative to its aware parent. */
base.IControlPrefs.ic_Flags=ICF_OFFSCREENLAYERS;screen.LayerInfo.Flags=LIFLG_SUPPORTS_OFFSCREEN_LAYERS;screen.Scale=100;
a=makewin(101,51,1000,800);a.ScaleAware=TRUE;b=makewin(151,101,200,100);b.parent=&a;b.RelLeftEdge=50;b.RelTopEdge=50;b.NextWindow=&a;screen.FirstWindow=&b;assert(WindowFitPrepareOpen(&b,&base));
x=2500;y=-50;w=200;h=100;FixWindowCoords(&b,&x,&y,&w,&h,&base);b.LeftEdge=x;b.TopEdge=y;b.RelLeftEdge=x-101;b.RelTopEdge=y-51;WindowFitRecordChange(&b,WINDOWFIT_X|WINDOWFIT_Y);
scale(200);assert(b.LeftEdge==350&&b.TopEdge==25);scale(100);assert(b.LeftEdge==2500&&b.TopEdge==-50&&b.RelLeftEdge==2399&&b.RelTopEdge==-101);
base.IControlPrefs.ic_Flags=0;screen.LayerInfo.Flags=0;
/* Actual constructor WA_AutoAdjust=FALSE preserves child extents at100;
   auto-adjusted births retain the constructor's own parent clamp. */
base.IControlPrefs.ic_Flags=ICF_OFFSCREENLAYERS;screen.LayerInfo.Flags=LIFLG_SUPPORTS_OFFSCREEN_LAYERS;screen.Scale=100;
a=makewin(101,51,1000,800);a.ScaleAware=TRUE;b=makewin(101,51,1200,100);b.parent=&a;
assert(ConstructorDimensions(&b,&a,1200,100,0,0,FALSE));
b.MinWidth=b.MaxWidth=b.Width;assert(WindowFitPrepareOpen(&b,&base)&&b.Width==1200&&b.FitWidth==1200);
b=makewin(101,51,1200,100);b.parent=&a;
assert(ConstructorDimensions(&b,&a,1200,100,0,0,TRUE)&&b.Width==1000);
b.MinWidth=b.MaxWidth=b.Width;assert(WindowFitPrepareOpen(&b,&base)&&b.Width==1000);
/* With AutoAdjustFALSE the constructor checks the child's relative box
   against the screen, then parent translation may leave a clipped child
   outside the physical panel. Base fitting must not alter that old result. */
b=makewin(0,0,50,50);b.parent=&a;
assert(ConstructorDimensions(&b,&a,50,50,2500,0,FALSE)&&b.LeftEdge==2601);
b.MinWidth=b.MaxWidth=50;b.MinHeight=b.MaxHeight=50;
assert(WindowFitPrepareOpen(&b,&base)&&b.LeftEdge==2601&&b.RelLeftEdge==2500);
/* Live child resize is bounded by the panel, not parent, at factor1.
   On reset both flags policies retain canonical dimensions; public origins
   obey the original absolute screen policy, with relative child coordinates. */
for(int flags=0;flags<4;flags++) {
 base.IControlPrefs.ic_Flags=ICF_OFFSCREENLAYERS;screen.LayerInfo.Flags=LIFLG_SUPPORTS_OFFSCREEN_LAYERS;screen.Scale=100;
 a=makewin(101,51,1000,800);a.ScaleAware=TRUE;b=makewin(151,101,200,100);b.parent=&a;b.RelLeftEdge=50;b.RelTopEdge=50;b.NextWindow=&a;screen.FirstWindow=&b;assert(WindowFitPrepareOpen(&b,&base));
 x=151;y=101;w=2000;h=1200;FixWindowCoords(&b,&x,&y,&w,&h,&base);assert(w==2000&&h==1200);
 b.Width=w;b.Height=h;WindowFitRecordChange(&b,WINDOWFIT_WIDTH|WINDOWFIT_HEIGHT);scale(200);assert(b.Width==500&&b.Height==400);
 base.IControlPrefs.ic_Flags=(flags&1)?ICF_OFFSCREENLAYERS:0;screen.LayerInfo.Flags=(flags&2)?LIFLG_SUPPORTS_OFFSCREEN_LAYERS:0;
 scale(100);assert(b.Width==2000&&b.Height==1200&&b.LeftEdge==151&&b.TopEdge==101&&b.RelLeftEdge==50&&b.RelTopEdge==50);
}
/* Absolute screen clamping at100 cannot accidentally add the parent twice. */
base.IControlPrefs.ic_Flags=0;screen.LayerInfo.Flags=0;screen.Scale=100;
a=makewin(101,51,1000,800);a.ScaleAware=TRUE;b=makewin(2500,1550,200,100);b.parent=&a;b.RelLeftEdge=2399;b.RelTopEdge=1499;b.NextWindow=&a;screen.FirstWindow=&b;
b.FitCanonicalValid=TRUE;b.FitLeft=2399;b.FitTop=1499;b.FitWidth=200;b.FitHeight=100;
scale(100);assert(b.LeftEdge==2360&&b.TopEdge==1500&&b.RelLeftEdge==2259&&b.RelTopEdge==1449);
assert(moves>0&&changes>0&&sizes>0);puts("PASS: production planner restore, birth, explicit axes, mixed families, offscreen policy, actual constructor child extents and base reset, preflight rejection and allocation cleanup");return 0;
}
'''
def run(body, directory, label, ignore_descriptor=False):
    source = directory / (label+'.c'); exe=directory/label
    geometry=(ROOT/'rom/graphics/scalecompose.h').read_text().replace('#include <exec/types.h>','')
    support=(ROOT/'rom/intuition/inputhandler_support.c').read_text(encoding='latin1')
    support=support[support.index('void FixWindowCoords('):]
    support=support[:support.index('/****************************************************************************************/')]
    constructor=(ROOT/'rom/intuition/openwindow.c').read_text()
    start=constructor.index('    {\n        LONG parentwidth;')
    end=constructor.index('    if (NULL == parentwin)',start)
    constructor=constructor[start:end]
    ctor_preamble = r'''
static BOOL WindowCompositionChildExtent(struct Window *p,struct Screen *s,BOOL aware,LONG *w,LONG *h) {
(void)s;(void)aware;*w=p->layer.Width;*h=p->layer.Height;return TRUE;}
static const struct ScaleWindowGeometry *constructor_geometry;
static BOOL ConstructorDimensions(struct Window *w,struct Window *parentwin,LONG width,LONG height,LONG left,LONG top,BOOL autoAdjust) {
struct NewWindow nw={.Width=width,.Height=height,.LeftEdge=left,.TopEdge=top};BOOL scaleaware=w->ScaleAware;
const struct ScaleWindowGeometry *scalegeometry=constructor_geometry;
'''
    if ignore_descriptor:
        ctor_preamble=ctor_preamble.replace('scalegeometry=constructor_geometry;', 'scalegeometry=NULL;')
    ctor_end = r'''
w->RelLeftEdge=nw.LeftEdge;w->RelTopEdge=nw.TopEdge;
w->LeftEdge=(parentwin?parentwin->LeftEdge:0)+nw.LeftEdge;
w->TopEdge=(parentwin?parentwin->TopEdge:0)+nw.TopEdge;return TRUE;
failexit:return FALSE;}
'''
    header='\n'.join(line for line in (ROOT/'rom/intuition/windowfit.h').read_text().splitlines() if not line.startswith('#include'))+'\n'
    aware=(ROOT/'rom/intuition/awaregeometry.h').read_text().replace('#include <exec/types.h>','')
    public=(ROOT/'compiler/include/intuition/intuition.h').read_text(encoding='latin1')
    descriptor=re.search(r'struct ScaleWindowGeometry\s*\{[^}]*\};',public).group(0)+'\n'
    preamble=PREAMBLE.replace('#define FALSE 0','#define FALSE 0\n'+aware+descriptor)
    limits=(ROOT/'rom/intuition/windowlimits.c').read_text(encoding='latin1')
    limits=limits[limits.index('{\n    AROS_LIBFUNC_INIT'):limits.rindex('}')+1]
    limits='static BOOL WindowLimits(struct Window *window,WORD MinWidth,WORD MinHeight,UWORD MaxWidth,UWORD MaxHeight,struct IntuitionBase *IntuitionBase)\n'+limits
    source.write_text(preamble+result_definitions(ROOT)+result_helpers(ROOT)+header+geometry+body+support+ctor_preamble+constructor+ctor_end+limits+TESTS)
    subprocess.run(['cc','-std=c99','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Wall','-Wextra','-Werror','-Wno-unused-function',str(source),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    (directory/(label+'.log')).write_text(result.stdout+result.stderr)
    (directory/(label+'.source.json')).write_text(json.dumps({'fixture_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'exit':result.returncode},indent=2)+'\n')
    return result
def main():
    body=SOURCE.read_text().split('struct FitGeometry',1)[1];body='struct FitGeometry'+body
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.output: args.output.mkdir(parents=True,exist_ok=True)
    with (contextlib.nullcontext(str(args.output.resolve())) if args.output else tempfile.TemporaryDirectory(prefix='aros-windowfit-')) as temp:
        directory=Path(temp); result=run(body,directory,'production')
        print(result.stdout,end='');assert result.returncode==0,result.stderr
        mutations={
          'autoadjust_false_ignored':body.replace('if (!autoAdjust)', 'if (0 && !autoAdjust)'),
          'zero_defaults_use_requested_extent':body.replace('(i - AG_MINWIDTH) % 2 ? plan.height : plan.width, scale', 'canonical.axis[AG_WIDTH + (i - AG_MINWIDTH) % 2].value, canonical.axis[AG_WIDTH + (i - AG_MINWIDTH) % 2].scale'),
          'fitted_constructor_replaces_canonical':body.replace('IW(window)->ScaleGeometry = canonical;', 'canonical.axis[AG_WIDTH] = (struct AwareGeometryValue){plan.width,scale}; IW(window)->ScaleGeometry = canonical;'),
          'physical_user_axis_assumed_base':body.replace('{values[i], IS(window->WScreen)->Scale};','{values[i], 100};'),
          'limits_not_installed':body.replace('window->MinWidth = geometry->minwidth;', '/* stale public minimum */'),
          'ignore_proposed_frame':body.replace('frames[j].right, frames[j].bottom','window->BorderRight, window->BorderBottom').replace('frames[j].left, frames[j].top','window->BorderLeft, window->BorderTop'),
          'lost_limit_reference':body.replace('unlimited ? -1 : value, IS(window->WScreen)->Scale','unlimited ? -1 : value, 100'),
          'old_child_viewport':body.replace('if (factor == 1)', 'if (0 && factor == 1)'),
          'old_birth_fit':body.replace('if (ScaleComposeFactor(IS(window->WScreen)->Scale) == 1)', 'if (0 && ScaleComposeFactor(IS(window->WScreen)->Scale) == 1)'),
          'old_offscreen_clamp':body.replace('if (restoreOffscreen)', 'if (0 && restoreOffscreen)'),
          'canonical_compounding':body.replace('if (IW(window)->FitAutomatic) return;','/* broken automatic capture */'),
          'missing_clamp':body.replace('if (*position > available - length) *position = available - length;','/* broken clamp */'),
          'first_window_only':body.replace('index < count; index++)','index < (count ? 1 : 0); index++)'),
          'batch_reconstrained':body.replace(' || IS(window->WScreen)->FitBatch', ''),
          'constraints_bypassed':body.replace('if (!legacy_window(window) || factor == 1 || IS(window->WScreen)->FitBatch) return TRUE;', 'if (factor >= 1) return TRUE;'),
          'physical_viewport':body.replace('window->WScreen->Width / factor','window->WScreen->Width'),
        }
        for label,mutated in mutations.items():
            assert mutated!=body,label
            result=run(mutated,directory,label);assert result.returncode!=0,label+' escaped detection'
            print('PASS: negative control rejected:',label)
        result=run(body,directory,'descriptor_ignored',ignore_descriptor=True)
        assert result.returncode!=0,'descriptor ignored escaped detection'
        print('PASS: negative control rejected: descriptor_ignored')
if __name__=='__main__':main()
