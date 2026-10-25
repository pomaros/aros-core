#!/usr/bin/env python3
# Copyright (C) 2026, John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License, version 1.1.
"""Extract actual ClipBlit root/client acquisition; exhaust bounded interleavings.

The C prefix records the real lock/unlock calls under ASan/UBSan. A complete
finite scheduler explores all interleavings against one opaque MoveTask's
physical-root/aware-layer/private-client order. It never blocks host threads.
"""
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
source = (ROOT / 'rom/graphics/clipblit.c').read_text()
begin = source.index('    struct Layer_Info *locked_li = NULL')
end = source.index('    /* check for overlapping blits */', begin)
prefix = source[begin:end]
old_begin = prefix.index('        /* Distinct physical roots')
old_end = prefix.index('\n    }', old_begin)
old = prefix[:old_begin] + """        if (source == destination) {
            LockLayerInfo(source); locked_li = source;
        }""" + prefix[old_end:]
assert old != prefix

PREAMBLE = r'''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef int16_t WORD; typedef uintptr_t IPTR;
struct Layer_Info { int id; struct Layer_Info *root; };
struct Layer { int id; struct Layer_Info *LayerInfo; };
struct RastPort { struct Layer *Layer; };
static int LayersBase=1,first;
#define FIX_GFXCOORD(x) ((void)(x))
static struct Layer_Info *GetLayerInfoCompositionRoot(struct Layer_Info *p) {return p->root?p->root:p;}
static void event(int lock,int id) {printf("%s[%d,%d]",first?"":",",lock,id);first=0;}
static void LockLayerInfo(struct Layer_Info *p) {event(1,p->id);}
static void UnlockLayerInfo(struct Layer_Info *p) {event(-1,p->id);}
static void LockLayerRom(struct Layer *p) {event(1,p->id);}
static void run(struct RastPort *srcRP,struct RastPort *destRP) {
WORD xSrc=0,ySrc=0,xDest=0,yDest=0;first=1;printf("[");
'''
POSTAMBLE = r'''
/* Client layers remain owned throughout the actual copy body, then release. */
if(destRP->Layer && destRP->Layer!=srcRP->Layer)event(-1,destRP->Layer->id);
if(srcRP->Layer)event(-1,srcRP->Layer->id);
printf("]\n");
}
int main(void) {
struct Layer_Info roots[2]={{0,0},{1,0}};
struct Layer_Info privateA={2,&roots[0]},privateB={3,&roots[1]},sameB={3,&roots[0]};
struct Layer la={2,&privateA},lb={3,&privateB},ab={4,&roots[1]},aa={5,&roots[0]},sb={3,&sameB};
struct RastPort a={&la},b={&lb},awareb={&ab},awarea={&aa},sameb={&sb},none={0};
run(&a,&awareb);run(&b,&a); /* legacy/aware + legacy/legacy across roots */
run(&a,&b);run(&b,&a); /* opposed private canvases across roots */
run(&a,&sameb);run(&sameb,&a); /* same physical root, distinct private LIs */
run(&awarea,&awareb);run(&awareb,&awarea); /* opposed aware cross-screen */
run(&a,&a);run(&a,&none); /* identical/single layer retains ordinary path */
return 0;
}
'''

def sequences(text, path, label):
    unit = path / (label + '.c'); executable = path / label
    unit.write_text(PREAMBLE + text + POSTAMBLE)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-O1',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    str(unit),'-o',str(executable)],check=True)
    result=subprocess.run([str(executable)],capture_output=True,text=True,check=True)
    return [json.loads(line) for line in result.stdout.splitlines()]

def schedule(programs):
    # pcs, lock owners, recursive lock depths: exhaustive finite state graph.
    initial=((0,)*len(programs),(-1,)*6,(0,)*6)
    pending=[initial];seen={initial};deadlocks=[];completed=0
    while pending:
        pcs,owners,depths=pending.pop()
        if all(pc==len(programs[i]) for i,pc in enumerate(pcs)):
            assert all(owner==-1 for owner in owners)
            completed+=1;continue
        advanced=False
        for task,pc in enumerate(pcs):
            if pc==len(programs[task]):continue
            lock,resource=programs[task][pc]
            if lock==1 and owners[resource] not in (-1,task):continue
            advanced=True;newpcs=list(pcs);newowners=list(owners);newdepths=list(depths)
            newpcs[task]+=1
            if lock==1:newowners[resource]=task;newdepths[resource]+=1
            else:
                assert owners[resource]==task and depths[resource]>0
                newdepths[resource]-=1
                if not newdepths[resource]:newowners[resource]=-1
            state=(tuple(newpcs),tuple(newowners),tuple(newdepths))
            if state not in seen:seen.add(state);pending.append(state)
        if not advanced:deadlocks.append((pcs,owners))
    return len(seen),completed,deadlocks

with tempfile.TemporaryDirectory(prefix='aros-crossscreen-locks-') as temp:
    directory=Path(temp);current=sequences(prefix,directory,'production');previous=sequences(old,directory,'old-equality-only')
    # Opaque MoveTask B actually owns physical screen B/all its aware layers,
    # then MoveSizeLayer on its private logical canvas needs client layer B.
    moveB=[[1,1],[1,4],[1,3],[-1,3],[-1,4],[-1,1]]
    moveA=[[1,0],[1,5],[1,2],[-1,2],[-1,5],[-1,0]]
    total=0
    for name,programs in [('legacy-aware-moveB',[current[0],current[1],moveB]),
                          ('legacy-opposed-moveB',[current[2],current[3],moveB]),
                          ('legacy-opposed-moveA',[current[2],current[3],moveA]),
                          ('same-root-opposed',[current[4],current[5]]),
                          ('aware-opposed',[current[6],current[7]])]:
        states,completed,deadlocks=schedule(programs)
        assert completed and not deadlocks,(name,deadlocks)
        total+=states;print('PASS actual production lock order:',name,'states='+str(states))
    states,completed,deadlocks=schedule([previous[0],previous[1],moveB])
    assert deadlocks,'old production equality-only root locking escaped detection'
    # Exact new 3-cycle: C ownsA waitsawareB(M), D ownsB waitsA(C), M ownsawareB waitsB(D).
    assert any(owners[2]==0 and owners[3]==1 and owners[4]==2 for pcs,owners in deadlocks)
    assert current[8]==[[1,2],[-1,2]] and current[9]==current[8]
    # A distinct pair must take both roots before either client and release
    # them once the pair is held; a shared root appears exactly once.
    assert current[0][:2]==[[1,0],[1,1]] and current[1][:2]==[[1,0],[1,1]]
    assert current[4][0]==[1,0] and sum(e==[1,0] for e in current[4])==1
    print('PASS negative control: old actual equality-only prefix reproduces privateA→awareB→privateB→privateA deadlock')
    print('PASS bounded exhaustive interleavings states='+str(total)+'; same/single-layer path unchanged; host-only, no runtime claim')
