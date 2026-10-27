#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Actual POSIXC source selection and capture across a pin instance switch."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();here=Path(__file__).resolve().parent;root=here.parents[4]
runtime=root/'compiler/crt/posixc/__cwd_runtime.c'
hooks=root/'compiler/crt/posixc/__threadhook.c'

def block(text,signature):
    start=text.index(signature);begin=text.index('{',start);depth=0
    for end in range(begin,len(text)):
        depth+=(text[end]=='{')-(text[end]=='}')
        if not depth:return text[start:end+1]
    raise ValueError(signature)

s=runtime.read_text();h=hooks.read_text()
actual='\n'.join([block(s,'struct __cwd_admission {')+';',
    block(s,'APTR __cwd_runtime_source('),block(s,'struct __cwd_admission *__cwd_admission_capture('),
    block(s,'struct Library *__cwd_admission_leave('),block(s,'void __cwd_admission_cancel('),
    block(h,'static APTR context_select('),block(h,'static BOOL context_capture(')])
prelude='''#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
typedef void *APTR;
typedef unsigned long ULONG;
typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
struct Library { int value; };
struct Task { ULONG id; } task;
struct __cwd_context { unsigned references; int id; };
struct __fd_namespace { unsigned references; int id; };
struct PosixCIntBase { struct __fd_namespace *fd_namespace; };
struct __cwd_binding { struct PosixCIntBase *base; ULONG owner_id; struct __cwd_context *context; };
static struct __cwd_binding *current;
static unsigned allocations;
static int fail_allocation,fail_context,fail_namespace;
static struct Task *FindTask(void *unused) { (void)unused;return &task; }
static ULONG GetETaskID(struct Task *t) { return t->id; }
static struct __cwd_binding *binding(void) { return current; }
static void *AllocVec(size_t size,unsigned flags) {
    assert(flags==(MEMF_PUBLIC|MEMF_CLEAR));if(fail_allocation)return NULL;
    void *p=calloc(1,size);assert(p);allocations++;return p;
}
static void FreeVec(void *p) { assert(p && allocations);allocations--;free(p); }
static BOOL __cwd_context_retain(struct __cwd_context *p) {
    if(!p || fail_context)return FALSE;assert(p->references);p->references++;return TRUE;
}
static void __cwd_context_release(struct __cwd_context *p) { assert(p && p->references);p->references--; }
static BOOL __fd_namespace_retain(struct __fd_namespace *p) {
    if(!p || fail_namespace)return FALSE;assert(p->references);p->references++;return TRUE;
}
static void __fd_namespace_drop(struct __fd_namespace *p) { assert(p && p->references);p->references--; }
'''
main='''
int main(void) {
    struct __cwd_context original_context={1,1},pin_context={1,2};
    struct __fd_namespace original_files={1,1},pin_files={1,2};
    struct PosixCIntBase *creator=calloc(1,sizeof(*creator));
    struct __cwd_binding *origin=calloc(1,sizeof(*origin));
    struct PosixCIntBase pin={&pin_files};
    struct __cwd_binding pin_binding={&pin,42,&pin_context};
    assert(creator && origin);creator->fd_namespace=&original_files;
    *origin=(struct __cwd_binding){creator,42,&original_context};
    task.id=42;current=origin;APTR selected=context_select();assert(selected==origin);
    /* OpenLibrary's pin is allowed to select a fresh per-task instance. */
    current=&pin_binding;
    APTR value=NULL;assert(context_capture(selected,&value));
    struct __cwd_admission *admitted=value;
    assert(admitted->context==&original_context && admitted->descriptors==&original_files &&
           "capture must retain the selected creator, not the pin instance");
    assert(original_context.references==2 && original_files.references==2);
    assert(pin_context.references==1 && pin_files.references==1);
    __cwd_admission_cancel(admitted);assert(!allocations);
    for(unsigned failure=0;failure<3;failure++) {
        fail_allocation=failure==0;fail_context=failure==1;fail_namespace=failure==2;
        value=NULL;assert(!context_capture(selected,&value) && !value);
        assert(original_context.references==1 && original_files.references==1 && !allocations);
    }
    fail_allocation=fail_context=fail_namespace=0;
    task.id=43;assert(!__cwd_admission_capture(selected));task.id=42;
    assert(!__cwd_admission_capture(NULL));
    assert(context_capture(selected,&value));admitted=value;
    /* Nothing borrowed from the selected base survives capture. */
    original_context.references--;original_files.references--;
    free(creator);free(origin);selected=NULL;
    assert(!__cwd_admission_leave(admitted));
    assert(!original_context.references && !original_files.references && !allocations);
    assert(pin_context.references==1 && pin_files.references==1);
    puts("PASS actual POSIXC select/capture/leave: pin switch, creator retirement, rollback and ownership");
}
'''
controls={
 'reselect-pin':('struct __cwd_binding *selected = source;','struct __cwd_binding *selected = binding(); (void)source;',
                 'capture must retain the selected creator, not the pin instance'),
 'capture-current-files':('a->descriptors = selected->base->fd_namespace;','a->descriptors = binding()->base->fd_namespace;',
                 'capture must retain the selected creator, not the pin instance'),
 'missing-files-retain':('if (!__fd_namespace_retain(a->descriptors)) a->descriptors = NULL;',
                        'if (FALSE) { (void)__fd_namespace_retain; a->descriptors = NULL; }',
                        'original_context.references==2 && original_files.references==2'),
}
out=args.output.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
for name,mutation,cause in [('production',None,None)]+[(n,(a,b),c) for n,(a,b,c) in controls.items()]:
    code=actual
    if mutation:
        assert code.count(mutation[0])==1,name
        code=code.replace(*mutation)
    unit=out/(name+'.c');unit.write_text(prelude+code+main);binary=out/name
    command=['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(unit),'-o',str(binary)]
    p=subprocess.run(command,capture_output=True,text=True,timeout=30);(out/(name+'-compile.log')).write_text(p.stdout+p.stderr)
    row={'name':name,'command':command,'compile_exit':p.returncode,'expected_cause':cause,'pass':False}
    if not p.returncode:
        p=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10);(out/(name+'.log')).write_text(p.stdout+p.stderr)
        row.update(run_exit=p.returncode,**{'pass':p.returncode==0 if not cause else p.returncode!=0 and cause in p.stderr})
    rows.append(row)
result={'result':'PASS' if all(x['pass'] for x in rows) else 'FAIL','records':rows,
 'inputs':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [runtime,hooks,Path(__file__)]},
 'scope':'Actual POSIXC selection/capture/leave and provider select/capture wrappers; modeled binding switch, context/namespace retain, allocator and Task identity. No real generated OpenLibrary, resolver, pthread or guest claim.'}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result']);raise SystemExit(result['result']!='PASS')
