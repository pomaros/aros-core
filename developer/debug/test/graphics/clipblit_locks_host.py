#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Exercise production root association and ClipBlit lock dispatch.

Layer API calls emit mutex operations. An exhaustive independent scheduler
checks two opposed cross-window blits against a presenter holding a private
GZZ stack in its normal z order. This models lock ownership; it is not a
kernel/SMP runtime qualification or a proof for callers preholding layers.
"""
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]


def function_body(path):
    source = (ROOT / path).read_text()
    body = source[source.index("{", source.index("AROS_LH")) + 1:source.rindex("}")]
    return body.replace("AROS_LIBFUNC_INIT", "").replace("AROS_LIBFUNC_EXIT", "")


getter = function_body("rom/hyperlayers/getlayerinfocompositionroot.c")
setter = function_body("rom/hyperlayers/setlayerinfocompositionroot.c")
source = (ROOT / "rom/graphics/clipblit.c").read_text()
locking = source[source.index("    struct Layer_Info *locked_li = NULL;"):
                 source.index("    /* check for overlapping blits */")]
unlocking = source[source.index("    if(destRP->Layer && (destRP->Layer != srcRP->Layer)) UnlockLayerRom"):
                   source.index("    AROS_LIBFUNC_EXIT", source.index("exit:"))]
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define NEWLAYERINFO_CALLED 1
#define FIX_GFXCOORD(x) ((void)(x))
struct LayerInfo_extra { struct Layer_Info *lie_CompositionRoot; };
struct Layer_Info { unsigned Flags; void *top_layer,*check_lp; struct LayerInfo_extra *LayerInfo_extra; int id; };
struct Layer { struct Layer_Info *LayerInfo; int id; };
struct RastPort { struct Layer *Layer; };
static void *LayersBase = (void *)(uintptr_t)1;
static int events[32],count;
static void LockLayerInfo(struct Layer_Info *li) {events[count++]=li->id+1;}
static void UnlockLayerInfo(struct Layer_Info *li) {events[count++]=-li->id-1;}
static void LockLayerRom(struct Layer *l) {events[count++]=l->id+1;}
static void UnlockLayerRom(struct Layer *l) {events[count++]=-l->id-1;}
'''
main = r'''
static void emit(struct RastPort *source,struct RastPort *destination) {
 count=0; lock_blit(source,destination);
 printf("[");for(int i=0;i<count;i++)printf("%s%d",i?",":"",events[i]);puts("]");
}
int main(void) {
 struct LayerInfo_extra ea={0},eb={0},ec={0};
 struct Layer_Info screen={0,0,0,0,0},a={1,0,0,&ea,20},b={1,0,0,&eb,21},c={1,0,0,&ec,22};
 assert(!GetLayerInfoCompositionRoot(NULL));
 assert(GetLayerInfoCompositionRoot(&screen)==&screen);
 assert(GetLayerInfoCompositionRoot(&a)==&a);
 assert(SetLayerInfoCompositionRoot(&a,&screen));
 assert(SetLayerInfoCompositionRoot(&b,&screen));
 assert(GetLayerInfoCompositionRoot(&a)==&screen);
 assert(GetLayerInfoCompositionRoot(&b)==&screen);
 assert(!SetLayerInfoCompositionRoot(&a,&screen)); /* immutable */
 assert(!SetLayerInfoCompositionRoot(&c,&a)); /* no chain */
 struct LayerInfo_extra ed={0};struct Layer_Info emptyNew={1,0,0,&ed,23};
 assert(!SetLayerInfoCompositionRoot(&c,&emptyNew)); /* future chain forbidden */
 assert(GetLayerInfoCompositionRoot(&emptyNew)==&emptyNew);
 assert(!SetLayerInfoCompositionRoot(&c,&c));
 c.top_layer=&screen;assert(!SetLayerInfoCompositionRoot(&c,&screen));c.top_layer=0;
 c.check_lp=&screen;assert(!SetLayerInfoCompositionRoot(&c,&screen));c.check_lp=0;
 c.Flags=0;assert(!SetLayerInfoCompositionRoot(&c,&screen));c.Flags=1;
 c.LayerInfo_extra=0;assert(!SetLayerInfoCompositionRoot(&c,&screen));c.LayerInfo_extra=&ec;
 assert(!SetLayerInfoCompositionRoot(NULL,&screen));assert(!SetLayerInfoCompositionRoot(&c,NULL));
 /* A1 and A2 are the private GZZ stack, B1 another legacy window, Aware physical. */
 struct Layer a1={&a,1},a2={&a,3},b1={&b,2},aware={&screen,4};
 struct RastPort ra1={&a1},ra2={&a2},rb1={&b1},rAware={&aware},none={0};
 emit(&ra1,&rb1);emit(&rb1,&ra2);emit(&ra1,&rAware);
 emit(&ra1,&ra2);emit(&none,&none);
 /* Independent screens keep the preexisting contract. */
 b.LayerInfo_extra=0; emit(&ra1,&rb1);
}
'''


def deadlock_reachable(programs):
    """Explore every mutex-respecting interleaving to completion or wait cycle."""
    seen = set()

    def visit(positions, owners):
        key = (positions, owners)
        if key in seen:
            return False
        seen.add(key)
        if all(pos == len(program) for pos, program in zip(positions, programs)):
            assert all(owner == -1 for owner in owners)
            return False
        enabled = False
        for actor, program in enumerate(programs):
            if positions[actor] == len(program):
                continue
            operation = program[positions[actor]]
            resource = abs(operation) - 1
            current = owners[resource]
            if operation > 0 and current != -1:
                continue
            if operation < 0:
                assert current == actor
            enabled = True
            next_positions = list(positions)
            next_positions[actor] += 1
            next_owners = list(owners)
            next_owners[resource] = actor if operation > 0 else -1
            if visit(tuple(next_positions), tuple(next_owners)):
                return True
        return not enabled

    return visit((0,) * len(programs), (-1,) * 32), len(seen)


with tempfile.TemporaryDirectory(prefix="aros-clipblit-locks-") as directory:
    tmp = Path(directory)
    for label, dispatch in [
        ("production", locking),
        ("private-LI-only", locking.replace("if (source == destination)",
             "if ((void)destination, srcRP->Layer->LayerInfo == destRP->Layer->LayerInfo)")),
    ]:
        text = prefix + "\nstatic struct Layer_Info *GetLayerInfoCompositionRoot(struct Layer_Info *li) {" + getter + "}\n"
        text += "static BOOL SetLayerInfoCompositionRoot(struct Layer_Info *li,struct Layer_Info *root) {" + setter + "}\n"
        text += "static void lock_blit(struct RastPort *srcRP,struct RastPort *destRP) {int xSrc=0,ySrc=0,xDest=0,yDest=0;\n"
        text += dispatch + unlocking + "}\n" + main
        (tmp / "test.c").write_text(text)
        subprocess.run(["clang", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-g", str(tmp / "test.c"),
                        "-o", str(tmp / "test")], check=True)
        result = subprocess.run([str(tmp / "test")], capture_output=True, text=True, timeout=15)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        a_to_b, b_to_a, legacy_to_aware, same_private, empty, different_screen = [
            json.loads(line) for line in result.stdout.splitlines()]
        assert empty == []
        assert 1 not in different_screen and -1 not in different_screen
        assert same_private[0] == 1 and same_private[3] == -1
        # Root screen LI, private LI, then its inner/outer layers in z order.
        presenter = [1, 21, 4, 2, -2, -4, -21, -1]
        deadlocked, states = deadlock_reachable([a_to_b, b_to_a, presenter])
        if label == "production":
            assert not deadlocked
            assert legacy_to_aware[0] == 1 and legacy_to_aware[3] == -1
            print(f"PASS production getter/setter constraints and ClipBlit roots; {states} lock interleavings, opposed blits + private GZZ presenter")
        else:
            assert deadlocked, "negative control escaped: " + label
            print("PASS negative control: private-LI-only dispatch reaches a wait cycle")
