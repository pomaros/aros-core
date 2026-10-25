#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Execute actual constructor unwind and gadget disposal with guarded objects."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4] / "rom/intuition"


def block(source, start):
    begin = source.index("{", start)
    level = 1
    end = begin + 1
    while level:
        level += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]


opening = (ROOT / "openwindow.c").read_text()
misc = (ROOT / "intuition_misc.c").read_text()
unwind = block(opening, opening.index("if (w)", opening.index("failexit:")))
kill = block(misc, misc.index("VOID KillWinSysGadgets("))
# Sibling publication belongs to the action's successful gadget branch.
publication = opening.index("w->parent->firstchild = w;")
assert publication > opening.index("if (CreateWinSysGadgets(w, IntuitionBase))")
assert "parentwin->firstchild = w" not in opening[:opening.index("failexit:")]
prefix = r'''
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
typedef void Object; typedef void *APTR; typedef void *Msg;
typedef unsigned short UWORD; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define VOID void
#define NUM_SYSGADS 8
#define DRIF_DIRECTCOLOR 0x10000
#define WDM_EXITWINDOW 10
struct Font { int unused; };
struct Region { int unused; };
struct Gadget { Object *GadgetRender; };
struct Screen { struct { unsigned dri_Flags; } DInfo; void *WinDecorObj; };
struct Window { struct Font *IFont; };
struct IntWindow { struct Window window; Object *sysgads[NUM_SYSGADS];
 struct { struct Font *dri_Font; } DecorDrawInfo;
 struct Region *OutlineShape; uintptr_t DecorUserBuffer; unsigned DecorUserBufferSize; };
struct IntuitionBase { int unused; };
struct wdpExitWindow { unsigned MethodID; uintptr_t wdp_UserBuffer; BOOL wdp_TrueColor; };
#define IW(w) ((struct IntWindow *)(w))
#define IS(s) (s)
#define SYSGAD(w,i) (IW(w)->sysgads[i])
static int allocations, exits, closes, composition, compositionFrees;
static void *owned(size_t n) {void *p=calloc(1,n);assert(p);++allocations;return p;}
static void DisposeObject(void *p) {if(p){--allocations;free(p);}}
static void RemoveGadget(struct Window *w,struct Gadget *g){(void)w;(void)g;}
static void ModifyIDCMP(struct Window *w,int f){(void)w;(void)f;}
static void CloseFont(struct Font *f){DisposeObject(f);}
static void DisposeRegion(struct Region *r){DisposeObject(r);}
static void FreeMem(void *p,size_t n){assert(n);DisposeObject(p);}
static void DoMethodA(void *o,void *m) {
 struct wdpExitWindow *msg=m;assert(o==(void *)(uintptr_t)1);
 assert(msg->MethodID==WDM_EXITWINDOW && msg->wdp_TrueColor);
 if(msg->wdp_UserBuffer)assert(*(char *)msg->wdp_UserBuffer==0);
 ++exits;
}
static void WindowCompositionDispose(struct Window *w,struct IntuitionBase *b) {
 (void)w;(void)b;if(composition){composition=0;++compositionFrees;}
}
'''
support = r'''
static void intui_CloseWindow(struct Window *w,struct IntuitionBase *b) {
 ++closes;KillWinSysGadgets(w,b);WindowCompositionDispose(w,b);
}
static void fail(int driver_init_done,int layers_ready,int decor_init_done,
 struct Window *w,struct Screen *screen,struct IntuitionBase *IntuitionBase) {
 struct {int layers_ready;} msg={layers_ready};
 struct {struct Screen *Screen;} nw={screen};
'''
main = r'''
int main(void) {
 struct IntuitionBase base;
 struct Screen screen={{DRIF_DIRECTCOLOR},(void *)(uintptr_t)1};
 for(int bits=0;bits<32;++bits) {
  struct IntWindow *iw=owned(sizeof *iw);struct Window *w=&iw->window;
  iw->window.IFont=owned(sizeof(struct Font));
  iw->DecorDrawInfo.dri_Font=owned(sizeof(struct Font));
  if(bits&1)iw->OutlineShape=owned(sizeof(struct Region));
  if(bits&2){iw->DecorUserBuffer=(uintptr_t)owned(32);iw->DecorUserBufferSize=32;}
  for(int i=0;i<NUM_SYSGADS;++i) {
   struct Gadget *g=owned(sizeof *g);g->GadgetRender=owned(16);iw->sysgads[i]=g;
  }
  /* Partial CreateWinSysGadgets failure has already disposed its gadgets. */
  KillWinSysGadgets(w,&base);
  int before=exits,closeBefore=closes;
  composition=1;compositionFrees=0;
  fail(bits&4,bits&8,bits&16,w,&screen,&base);
  assert(allocations==0 && compositionFrees==1 && !composition);
  assert(exits==before+!!(bits&16));
  assert(closes==closeBefore+!!(bits&(4|8)));
 }
 puts("PASS production unwind: 32 partial-construction states, gadget disposal, outline/decorator ownership");
}
'''
with tempfile.TemporaryDirectory(prefix="aros-composition-cleanup-") as directory:
    tmp = Path(directory)
    for label, implementation in [
        ("production", kill),
        ("stale-gadget-slots", kill.replace("SYSGAD(w, i) = NULL;", "")),
    ]:
        text = prefix + "\nstatic void KillWinSysGadgets(struct Window *w,struct IntuitionBase *IntuitionBase)\n" + implementation
        text += support + unwind + "}\n" + main
        (tmp / "test.c").write_text(text)
        subprocess.run(["clang", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-g", "-fsanitize=address,undefined",
                        str(tmp / "test.c"), "-o", str(tmp / "test")], check=True)
        result = subprocess.run([str(tmp / "test")], capture_output=True, text=True)
        if label == "production":
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            print(result.stdout, end="")
        elif result.returncode == 0:
            raise RuntimeError("stale gadget negative control escaped")
        else:
            print("PASS negative control:", label)
