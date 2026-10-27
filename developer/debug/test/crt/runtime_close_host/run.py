#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Actual runtime close guard: owner identity, pins, scopes and binding order."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[4]
path=root/'compiler/crt/posixc/__cwd_runtime.c';source=path.read_text()
start=source.index('static int runtime_can_close(')
actual=source[start:source.index('\nstatic void runtime_close(',start)]
prelude='''#include <assert.h>
#include <stddef.h>
#include <stdio.h>
typedef unsigned long ULONG;
#define TRUE 1
#define FALSE 0
struct Task { ULONG id; } task;
struct __cwd_binding { ULONG owner_id, scope_depth; void *context_scope; };
struct PosixCIntBase { struct __cwd_binding *cwd_runtime; ULONG count; };
static struct __cwd_binding *active;
static struct Task *FindTask(void *unused) { (void)unused; return &task; }
static ULONG GetETaskID(struct Task *t) { return t->id; }
static ULONG __GM_GetBaseOpenCount(struct PosixCIntBase *b) { return b->count; }
static struct __cwd_binding *binding(void) { return active; }
'''
main='''
int main(void) {
    struct __cwd_binding own={.owner_id=42},other={.owner_id=43};
    struct PosixCIntBase base={.cwd_runtime=&own};
    for(unsigned foreign=0;foreign<2;foreign++)
    for(unsigned current=0;current<2;current++)
    for(unsigned scope=0;scope<2;scope++)
    for(unsigned proxy=0;proxy<2;proxy++)
    for(unsigned pins=0;pins<2;pins++) {
        task.id=foreign ? 43 : 42;active=current ? &own : &other;
        own.scope_depth=scope;own.context_scope=proxy ? &own : NULL;
        base.count=pins ? 2 : 1;
        int allowed=runtime_can_close(&base);
        if(foreign) assert(!allowed && "foreign close must refuse even with extra references");
        else if(pins) assert(allowed && "temporary pin release must not leak during scopes");
        else assert(allowed==(current && !scope && !proxy) && "final close requires current quiescent binding");
    }
    task.id=0;own.owner_id=0;assert(!runtime_can_close(&base));
    base.cwd_runtime=NULL;assert(runtime_can_close(&base));
    puts("PASS actual runtime close guard: 32 owner/pin/binding/scope combinations and failed-open rollback");
}
'''
controls={
 'pin-leak':('return __GM_GetBaseOpenCount(base) > 1 ||','return (__GM_GetBaseOpenCount(base), FALSE) ||','temporary pin release must not leak during scopes'),
 'foreign-pin':('if (!b->owner_id || b->owner_id != GetETaskID(FindTask(NULL))) return FALSE;','if (FALSE && (!b->owner_id || b->owner_id != GetETaskID(FindTask(NULL)))) return FALSE;','foreign close must refuse even with extra references'),
 'out-of-order':('(binding() == b && !b->scope_depth && !b->context_scope)','((void)binding(), !b->scope_depth && !b->context_scope)','final close requires current quiescent binding'),
}
out=args.output.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
for name,mutation,cause in [('production',None,None)]+[(n,(a,b),c) for n,(a,b,c) in controls.items()]:
    code=actual
    if mutation:
        assert code.count(mutation[0])==1,name
        code=code.replace(*mutation)
    unit=out/(name+'.c');unit.write_text(prelude+code+main);binary=out/name
    cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(unit),'-o',str(binary)]
    p=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(out/(name+'-compile.log')).write_text(p.stdout+p.stderr)
    row={'name':name,'command':cmd,'compile_exit':p.returncode,'expected_cause':cause,'pass':False}
    if not p.returncode:
        p=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10);(out/(name+'.log')).write_text(p.stdout+p.stderr)
        row.update(run_exit=p.returncode,**{'pass':p.returncode==0 if not cause else p.returncode!=0 and cause in p.stderr})
    rows.append(row)
result={'result':'PASS' if all(r['pass'] for r in rows) else 'FAIL','records':rows,
 'inputs':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [path,Path(__file__)]},
 'scope':'Actual runtime_can_close; modeled Task identity, active binding and generated metadata getter. No complete OpenLibrary/CloseLibrary lifecycle or guest claim.'}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result']);raise SystemExit(result['result']!='PASS')
