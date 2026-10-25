#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Execute the production bitmap update dispatch with guarded allocations."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
source = (ROOT / "rom/graphics/updatebitmap.c").read_text()
body = source[source.index("void update_bitmap("):]
prefix = r'''
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
typedef uint16_t UWORD;
typedef void OOP_Object;
struct Hook { int unused; };
struct BitMap { UWORD pad; };
struct Extended { struct BitMap bm; struct Hook *hook; };
struct BitMapUpdateMessage { UWORD x,y,width,height; };
struct GfxBase { int unused; };
struct monitor_displaydata { struct { void *display_compositor; } mdisplay; };
static struct monitor_displaydata monitor;
static int updates, composites, hooks;
static void *object = (void *)(uintptr_t)2;
static struct BitMap *expected;
static struct Hook listener;
static int order;
#define IS_HIDD_BM(b) ((b)->pad == 0x6148)
#define GET_BM_DRIVERDATA(b) ((void)(b), &monitor)
#define HIDD_BM_UPDATEHOOK(b) (((struct Extended *)(b))->hook)
static void compositor_UpdateBitMap(void *c, void *b,UWORD x,UWORD y,
 UWORD w,UWORD h,struct GfxBase *g) {
 (void)x;(void)y;(void)w;(void)h;(void)g;
 assert(c == &monitor && b == object); ++composites; order=1;
}
static void HIDD_BM_UpdateRect(void *b,UWORD x,UWORD y,UWORD w,UWORD h) {
 (void)x;(void)y;(void)w;(void)h; assert(b==object); ++updates; order=1;
}
static void CallHookPkt(struct Hook *h, struct BitMap *b,
 struct BitMapUpdateMessage *m) {
 assert(order==1 && h==&listener && b==expected);
 assert(m->x==7 && m->y==11 && m->width==13 && m->height==17);
 order=2; ++hooks;
}
'''
main = r'''
int main(void) {
 struct Extended *e=calloc(1,sizeof *e); assert(e);
 e->bm.pad=0x6148; expected=&e->bm;
 update_bitmap(&e->bm,object,7,11,13,17,NULL);
 assert(updates==1 && composites==0 && hooks==0);
 e->hook=&listener;
 update_bitmap(&e->bm,object,7,11,13,17,NULL);
 assert(updates==2 && hooks==1 && order==2);
 monitor.mdisplay.display_compositor=&monitor;
 update_bitmap(&e->bm,object,7,11,13,17,NULL);
 assert(composites==1 && updates==2 && hooks==2);
 update_bitmap(&e->bm,object,7,11,0,17,NULL);
 update_bitmap(&e->bm,object,7,11,13,0,NULL);
 assert(composites==3 && hooks==2);
 /* Exact classic BitMap allocation: an extension read is an ASan error. */
 struct BitMap *planar=calloc(1,sizeof *planar); assert(planar);
 update_bitmap(planar,object,7,11,13,17,NULL);
 assert(composites==4 && hooks==2);
 free(planar); free(e);
 puts("PASS bitmap update: normal/compositor notifications, empty damage, planar bounds");
}
'''
with tempfile.TemporaryDirectory(prefix="aros-bitmapupdate-") as directory:
    tmp = Path(directory)
    for label, implementation in [
        ("production", body),
        ("missing-planar-guard", body.replace("IS_HIDD_BM(bitmap) && ", "")),
        ("missing-notification", body.replace("CallHookPkt(HIDD_BM_UPDATEHOOK(bitmap), bitmap, &message);",
             "(void)message;")),
    ]:
        # Keep warning errors meaningful when the mutation removes the only call.
        text = prefix + implementation + main
        (tmp / "test.c").write_text(text)
        subprocess.run(["clang", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", "-g", "-fsanitize=address,undefined",
                        str(tmp / "test.c"), "-o", str(tmp / "test")], check=True)
        result = subprocess.run([str(tmp / "test")], capture_output=True, text=True)
        if label == "production":
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            print(result.stdout, end="")
        elif result.returncode == 0:
            raise RuntimeError("negative control escaped: " + label)
        else:
            print("PASS negative control:", label)
