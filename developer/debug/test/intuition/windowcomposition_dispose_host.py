#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Run composition teardown against the actual layers root-ownership body."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]


def body(text, signature):
    start = text.index("{", text.index(signature))
    end, depth = start + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


engine = (ROOT / "rom/intuition/windowcomposition.c").read_text()
layers = (ROOT / "rom/hyperlayers/basicfuncs.c").read_text()
dispose = body(engine, "void WindowCompositionDispose(")
free_extra = body(layers, "void _FreeExtLayerInfo(")
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
typedef int BOOL;
#define TRUE 1
#define NULL_GUARD(p) do { if (!(p)) return; } while(0)
struct Layer { unsigned kind; };
struct Region { unsigned kind; };
struct SignalSemaphore { unsigned unused; };
struct LayerInfo_extra { unsigned unused; };
struct Layer_Info { int fatten_count; struct Layer *check_lp; void *LayerInfo_extra; };
struct GfxBase { unsigned unused; };
struct LayersBase { unsigned unused; };
struct Library { unsigned unused; };
struct IntuitionBase { struct GfxBase *GfxBase; struct LayersBase *LayersBase; struct Library *OOPBase; };
struct WindowComposition {
 struct SignalSemaphore damageLock; int stopping; struct Layer *proxy;
 struct Region *projectedShape, *logicalShapeSnapshot; struct Layer_Info *logical;
 void *bitmap, *gc, *sourceRow, *destRow;
};
struct Window { struct WindowComposition *Composition; };
#define IW(w) (w)
#define GetPrivIBase(b) (b)
static unsigned allocated, deleted[2];
static void *owned(size_t n) { void *p=calloc(1,n);assert(p);++allocated;return p; }
static void FreeMem(void *p,size_t n) { assert(p&&n&&allocated);--allocated;free(p); }
static void FreeVec(void *p) { if(p) FreeMem(p,1); }
static void FreeBitMap(void *p) { FreeVec(p); }
static void OOP_DisposeObject(void *p) { FreeVec(p); }
static void DisposeRegion(void *p) { FreeVec(p); }
static void ObtainSemaphore(struct SignalSemaphore *p) {(void)p;}
static void ReleaseSemaphore(struct SignalSemaphore *p) {(void)p;}
static void DeleteLayer(unsigned flags,struct Layer *p) {
 (void)flags; assert(p&&p->kind<2);++deleted[p->kind];FreeVec(p);
}
'''
middle = r'''
static void DisposeLayerInfo(struct Layer_Info *li) {
 _FreeExtLayerInfo(li,NULL);FreeMem(li,sizeof(*li));
}
'''
main = r'''
int main(void) {
 struct IntuitionBase base={0}; struct Window window={0};
 for(unsigned mask=0;mask<128;mask++) {
  struct WindowComposition *s=owned(sizeof(*s));window.Composition=s;
  deleted[0]=deleted[1]=0;
  if(mask&1){s->proxy=owned(sizeof(*s->proxy));s->proxy->kind=0;}
  if(mask&2)s->projectedShape=owned(sizeof(*s->projectedShape));
  if(mask&4)s->logicalShapeSnapshot=owned(sizeof(*s->logicalShapeSnapshot));
  if(mask&8){
   s->logical=owned(sizeof(*s->logical));s->logical->fatten_count=0;
   s->logical->LayerInfo_extra=owned(sizeof(struct LayerInfo_extra));
   s->logical->check_lp=owned(sizeof(struct Layer));s->logical->check_lp->kind=1;
  }
  if(mask&16)s->bitmap=owned(1);
  if(mask&32)s->gc=owned(1);
  if(mask&64){s->sourceRow=owned(4);s->destRow=owned(4);}
  WindowCompositionDispose(&window,&base);
  assert(!window.Composition&&!allocated);
  assert(deleted[0]==!!(mask&1)&&deleted[1]==!!(mask&8));
  WindowCompositionDispose(&window,&base);assert(!allocated);
 }
 puts("PASS actual composition/layers teardown: 128 states, implicit root exactly once, repeated dispose");
}
'''

with tempfile.TemporaryDirectory(prefix="scale-dispose-") as directory:
    directory = Path(directory)
    source, executable = directory / "test.c", directory / "test"

    def run(candidate):
        source.write_text(prefix + "static void _FreeExtLayerInfo(struct Layer_Info *li,struct LayersBase *LayersBase)\n" +
                          free_extra + middle +
                          "static void WindowCompositionDispose(struct Window *window,struct IntuitionBase *IntuitionBase)\n" +
                          candidate + main)
        subprocess.run(["cc", "-fsanitize=address,undefined", "-g", str(source), "-o", str(executable)], check=True)
        return subprocess.run([str(executable)], capture_output=True, text=True)

    result = run(dispose)
    assert result.returncode == 0, result.stdout + result.stderr
    print(result.stdout, end="")
    mutant = dispose.replace("DisposeLayerInfo(state->logical);",
                             "if(state->logical->check_lp) DeleteLayer(0,state->logical->check_lp);\nDisposeLayerInfo(state->logical);")
    assert mutant != dispose and run(mutant).returncode != 0
    print("PASS negative control: double implicit-root deletion")
