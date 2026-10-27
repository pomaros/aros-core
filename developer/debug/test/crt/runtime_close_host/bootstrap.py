#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Actual runtime bootstrap must bypass a predecessor's logical resolver."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[4];src=root/'compiler/crt/posixc/__cwd_runtime.c';text=src.read_text();start=text.index('static BPTR native_initial_directory(');code=text[start:text.index('\nstatic int runtime_open(',start)]
prelude='''#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef uintptr_t BPTR;
typedef intptr_t SIPTR;
typedef void *APTR;
#define BNULL 0
#define NT_PROCESS 13
#define SHARED_LOCK -2
#define DOS_PATH_LOCK 2
struct Task { struct { int ln_Type; } tc_Node; };
struct Process { struct Task task; BPTR pr_CurrentDir; };
struct DosLibrary { int unused; };
struct __cwd_native_call { long operation;const SIPTR *args; };
static struct Task *current;
static int fail_dup,fail_helper,duplicates,helpers;
static BPTR expected_program=18;
static BPTR GetProgramDir(void) { assert(current->tc_Node.ln_Type==NT_PROCESS);return 18; }
static BPTR DupLock(BPTR lock) { assert(lock==17);duplicates++;return fail_dup ? 0 : lock+100; }
static BPTR Lock(const char *name) { assert(!strcmp(name,""));return 199; }
static SIPTR __cwd_native_call(APTR data,struct DosLibrary *dos) { (void)data;(void)dos;abort(); }
static SIPTR RunWithContext(BPTR cwd,BPTR program,SIPTR (*fn)(APTR,struct DosLibrary *),APTR data,SIPTR *error) {
    struct __cwd_native_call *call=data;
    assert(!cwd && program==expected_program && fn==__cwd_native_call);
    assert(call->operation==DOS_PATH_LOCK && !strcmp((const char *)call->args[0],"") && call->args[1]==SHARED_LOCK);
    helpers++;*error=fail_helper ? 23 : 0;return fail_helper ? 0 : 44;
}
'''
main='''
int main(void) {
    (void)Lock;
    struct Process process={.task.tc_Node.ln_Type=NT_PROCESS,.pr_CurrentDir=17};
    current=&process.task;BPTR program=0;
    assert(native_initial_directory(current,&program)==117 && "bootstrap must use native cwd, not old resolver");
    assert(program==18 && duplicates==1 && !helpers);
    fail_dup=1;assert(!native_initial_directory(current,&program));
    assert(duplicates==2 && !helpers);fail_dup=0;
    process.pr_CurrentDir=0;expected_program=18;
    assert(native_initial_directory(current,&program)==44 && program==18 && helpers==1);
    struct Task *plain=calloc(1,sizeof(*plain));assert(plain);plain->tc_Node.ln_Type=1;
    current=plain;expected_program=0;
    assert(native_initial_directory(current,&program)==44 && !program && helpers==2);
    fail_helper=1;assert(!native_initial_directory(current,&program));assert(helpers==3);free(plain);
    puts("PASS actual native runtime bootstrap: explicit Process cwd, root, ordinary Task and failures");
}
'''
controls={
 'old-resolver':('if (cwd) return DupLock(cwd);','if (cwd) return Lock("");','bootstrap must use native cwd, not old resolver'),
 'lost-native-cwd':('cwd = ((struct Process *)task)->pr_CurrentDir;','cwd = BNULL;',
                    'bootstrap must use native cwd, not old resolver'),
}
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
for name,mutation,cause in [('production',None,None)]+[(n,(x,y),z) for n,(x,y,z) in controls.items()]:
    actual=code
    if mutation:
        assert actual.count(mutation[0])==1
        actual=actual.replace(*mutation)
    unit=out/(name+'.c');unit.write_text(prelude+actual+main);binary=out/name
    cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-g','-fsanitize=address,undefined',str(unit),'-o',str(binary)]
    q=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(out/(name+'-compile.log')).write_text(q.stdout+q.stderr)
    row={'name':name,'command':cmd,'compile_exit':q.returncode,'expected_cause':cause,'pass':False}
    if not q.returncode:
        q=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10);(out/(name+'.log')).write_text(q.stdout+q.stderr)
        row.update(run_exit=q.returncode,**{'pass':q.returncode==0 if not cause else q.returncode!=0 and cause in q.stderr})
    rows.append(row)
result={'result':'PASS' if all(r['pass'] for r in rows) else 'FAIL','records':rows,'inputs':{str(t.relative_to(root)):hashlib.sha256(t.read_bytes()).hexdigest() for t in [src,Path(__file__)]},'scope':'Actual native_initial_directory, modeled native DOS locks and synchronous context helper; no full runtime OpenLibrary or guest claim.'}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result']);raise SystemExit(result['result']!='PASS')
