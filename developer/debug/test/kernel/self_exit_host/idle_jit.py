#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# SPDX-License-Identifier: APL-1.1
"""HOST control-flow test; no actual SPRR/CPU register or SMP execution."""
import argparse, hashlib, json, os, re, signal, subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
CORE=HERE.parents[4]
p=argparse.ArgumentParser()
p.add_argument('--out',type=Path,required=True)
a=p.parse_args(); a.out.mkdir(parents=True,exist_ok=False)
def read(path): return path.read_text()
k=CORE/'arch/aarch64-apple/kernel/kernel_cpu.c'
j=CORE/'arch/aarch64-apple/kernel/jit_protect.c'
s=read(k); js=read(j)
constant_inputs={
    CORE/'arch/aarch64-apple/include/aros/apple/jit.h':['AROS_APPLE_JIT_NONE','AROS_APPLE_JIT_SPRR','AROS_APPLE_JIT_RWX'],
    CORE/'compiler/include/exec/tasks.h':['TF_ETASK'],
    CORE/'arch/aarch64-all/include/aros/cpucontext.h':['ECF_FPU','ECF_JITWRITE'],
    k:['TASK_SPSR_ALLOWED','TASK_SPSR_IRQ_MASK','TASK_SPSR_FIQ_MASK','IDLE_SPSR_EL1H'],
}
constant_bytes={path:path.read_bytes().decode("latin-1") for path in constant_inputs}
constants=[]
for path,names in constant_inputs.items():
    for name in names:
        matches=re.findall(r'^#define[ \t]+'+re.escape(name)+r'[ \t]+[^\n]+',constant_bytes[path],re.M)
        assert len(matches)==1,(path,name,matches)
        constants.append(matches[0])
constants='\n'.join(constants)+'\n'
(a.out/'actual-constants.h').write_text(constants)

idle=s[s.index('static void enter_idle(regs_t *regs)'):s.index('    if (idle_trace_owner()',s.index('static void enter_idle(regs_t *regs)'))]+'}\n'
assert idle.count('AROSAppleJitRestore(NULL);')==1
assert idle.index('AROSAArch64WriteTaskPointer(0);')<idle.index('AROSAppleJitRestore(NULL);')<idle.index('AROSCPUCurrentState(AROS_CPU_IDLE);')
dispatch=s[s.index('void cpu_Dispatch(regs_t *regs)'):]
assert dispatch.rstrip().endswith('}') and dispatch.count('void cpu_Dispatch(')==1
jit=js[js.index('static struct ExceptionContext *context_of'):js.index('uint64_t AROSAppleJitSetWritable')]
fixture=read(HERE/'idle_jit_fixture.c')
def source(i):
    return fixture.replace('/* ACTUAL_SOURCE_CONSTANTS */',constants).replace('/* ACTUAL_CONTEXT_AND_JIT */',jit).replace('/* ACTUAL_IDLE_PREFIX */',i).replace('/* ACTUAL_DISPATCH */',dispatch)
old_idle=idle.replace('    AROSAppleJitRestore(NULL);\n', '')
(a.out/'production-kernel_cpu.c').write_text(s)
(a.out/'mutant-idle.c').write_text(old_idle)
(a.out/'production-jit_protect.c').write_text(js)
rows=[]
for name,body,sanitize,good in [('plain',idle,False,True),('sanitized',idle,True,True),('mutant-no-neutral-restore',old_idle,False,False)]:
    c=a.out/(name+'.c'); c.write_text(source(body)); binary=a.out/name
    cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-g']
    if sanitize: cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    cmd += [str(c),'-o',str(binary)]
    build=subprocess.run(cmd,capture_output=True,text=True,timeout=20)
    (a.out/(name+'-compile.log')).write_text(build.stdout+build.stderr)
    assert build.returncode==0,build.stderr
    run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
    (a.out/(name+'.log')).write_text(run.stdout+run.stderr)
    if good: assert run.returncode==0 and not run.stderr,(name,run.returncode,run.stderr)
    else:
        token='Assertion failed: (hardware_table==perm_el0_exec), function AROSCPUCurrentState,'
        lines=run.stderr.splitlines()
        assert run.returncode==-signal.SIGABRT and len(lines)==1 and lines[0].startswith(token),(run.returncode,run.stderr)
    rows.append(dict(name=name,argv=cmd,returncode=run.returncode,expected_pass=good))
assert read(k)==s and read(j)==js,'production inputs changed during run'
assert all(path.read_bytes().decode("latin-1")==data for path,data in constant_bytes.items()),'constant inputs changed during run'
inputs=list(dict.fromkeys([k,j,HERE/'idle_jit.py',HERE/'idle_jit_fixture.c',*constant_inputs]))
result=dict(result='PASS',scope='HOST actual idle prefix through CPU state publication, full dispatcher and JitRestore/context_of with platform permission callback shim; excludes idle diagnostic trace ASM, hardware and SMP',cases=rows,inputs=[dict(path=str(f),sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in inputs])
(a.out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS actual idle JIT 3 HOST cases including missing idle restore SIGABRT oracle')
