#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# SPDX-License-Identifier: APL-1.1
"""Actual portable slot and extracted cpu_Dispatch, with narrow HOST shims."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

HERE = Path(__file__).resolve().parent
CORE = HERE.parents[4]
p = argparse.ArgumentParser()
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=False)
engine = CORE / 'arch/aarch64-apple/kernel/self_exit.c'
include = engine.parent
source = (include / 'kernel_cpu.c').read_text()
body = source[source.index('void cpu_Dispatch(regs_t *regs)'):]
# cpu_Dispatch is the final definition, reject a silently extended fixture.
assert body.rstrip().endswith('}') and body.count('void cpu_Dispatch(') == 1
fixture = (HERE / 'dispatch_fixture.c').read_text()
assert fixture.count('/* ACTUAL_DISPATCH_BODY */') == 1
rem = (CORE / 'rom/exec/remtask.c').read_text()
assert rem.index('Disable();', rem.index('Sending to Exec service task')) < rem.index('EXEC_DEFER_SELF_EXIT(task, SysBase, publish_self_exit)') < rem.index('KrnDispatch();')
assert '#if !defined(EXEC_DEFER_SELF_EXIT)\n            InternalPutMsg' in rem
api = (CORE / 'arch/aarch64-apple/exec/smp_api.h').read_text()
assert 'table->version==4' in api and 'table->bytes==sizeof(*table)' in api
assert api.index('(*spin_trylock)') < api.index('(*self_exit_queue)')
assert 'AROSAppleExecSMPAPIValid(AROSAppleRuntimeLocal()->exec_smp_api)' in (include/'startup_prepare.c').read_text()
assert 'schedulecpu smp self_exit smp_proof' in (include/'mmakefile.src').read_text()
rows=[]
def case(name, test, eng, must_pass):
    testfile=a.out/(name+'.c'); testfile.write_text(test)
    engfile=a.out/(name+'-engine.c'); engfile.write_text(eng)
    binary=a.out/name
    cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(include),str(engfile),str(testfile),'-o',str(binary)]
    c=subprocess.run(cmd,capture_output=True,text=True,timeout=20)
    (a.out/(name+'-compile.log')).write_text(c.stdout+c.stderr)
    assert c.returncode==0, (name,c.stderr)
    x=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
    (a.out/(name+'.log')).write_text(x.stdout+x.stderr)
    assert (x.returncode==0)==must_pass, (name,x.returncode,x.stdout,x.stderr)
    rows.append(dict(name=name,argv=cmd,returncode=x.returncode,expected_pass=must_pass))
start=time.monotonic()
e=engine.read_text()
case('slot', (HERE/'test.c').read_text(),e,True)
case('dispatch',fixture.replace('/* ACTUAL_DISPATCH_BODY */',body),e,True)
# Frozen predecessor must fail the new ready-after-publication oracle.
old_body=body.replace('            goto dispatch_after_self_exit;', '            return;')
# The label is still a valid branch target in an unreachable compile-time path.
old_body=old_body.replace('    task = core_Dispatch();','    if (0) goto dispatch_after_self_exit;\n    task = core_Dispatch();')
case('mutant-no-idle-rescan',fixture.replace('/* ACTUAL_DISPATCH_BODY */',old_body),e,False)
smp=(include/'smp.c').read_text()
adapter=smp[smp.index('static struct AppleSelfExit self_exit['):smp.index('const struct AROSAppleExecSMPAPI AROSAppleExecSMPAPI =')]
case('adapter',(HERE/'adapter_fixture.c').read_text().replace('/* ACTUAL_ADAPTER */',adapter),e,True)

definition=api[api.index('struct AROSAppleExecSMPAPI {'):api.index('extern const struct AROSAppleExecSMPAPI')]
valid=api[api.index('static inline int AROSAppleExecSMPAPIValid'):api.index('static inline const struct AROSAppleExecSMPAPI *AROSAppleExecSMPAPIGet')]
gate = r"""
#include <assert.h>
#include <stdint.h>
typedef unsigned long ULONG, IPTR;
typedef void *APTR;
typedef struct { unsigned value; } spinlock_t;
struct Hook; struct CallIPIEntry;
DEFINITION
VALIDATOR
static spinlock_t *trylock(spinlock_t *x, ULONG mode) { (void)mode; return x; }
static int queue(void *t,void *b,void (*f)(void *,void *)) { (void)t;(void)b;(void)f;return 1; }
int main(void) {
 struct AROSAppleExecSMPAPI x={0};
 assert(!AROSAppleExecSMPAPIValid(0));
 x.version=4; x.bytes=sizeof(x); x.spin_trylock=trylock; x.self_exit_queue=queue;
 assert(AROSAppleExecSMPAPIValid(&x));
 x.version=3; assert(!AROSAppleExecSMPAPIValid(&x)); x.version=4;
 --x.bytes; assert(!AROSAppleExecSMPAPIValid(&x)); ++x.bytes;
 x.self_exit_queue=0; assert(!AROSAppleExecSMPAPIValid(&x)); x.self_exit_queue=queue;
 x.spin_trylock=0; assert(!AROSAppleExecSMPAPIValid(&x));
}
""".replace('DEFINITION',definition).replace('VALIDATOR',valid)
case('api-gate',gate,e,True)
# Regression controls exercise the same tests, not copied alternate assertions.
case('mutant-wrongcpu',(HERE/'test.c').read_text(),e.replace('slot->cpu != cpu', '(slot->cpu != cpu && 0)'),False)
early=body.replace('    task = core_Dispatch();','    if (!AROSAppleSelfExitDetached(departing)) dispatch_contract_halt();\n    task = core_Dispatch();')
case('mutant-early-publication',fixture.replace('/* ACTUAL_DISPATCH_BODY */',early),e,False)
case('mutant-post-publication-read',(HERE/'test.c').read_text(),e.replace('    publish(task, base);','    publish(task, base);\n    *(volatile unsigned char *)task = 0;'),False)
inputs=[engine,include/'self_exit.h',include/'kernel_cpu.c',HERE/'test.c',HERE/'dispatch_fixture.c',HERE/'adapter_fixture.c',include/'smp.c',Path(__file__).resolve()]
(a.out/'result.json').write_text(json.dumps(dict(result='PASS',scope='HOST actual slot/extracted dispatch only; no native/SMP scheduler claim',elapsed=time.monotonic()-start,cases=rows,inputs=[dict(path=str(x),sha256=hashlib.sha256(x.read_bytes()).hexdigest()) for x in inputs]),indent=2)+'\n')
print('PASS',len(rows),'actual-body HOST cases')
