#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1
"""Actual native context copy macros on HOST and as freestanding AArch64 objects."""
from pathlib import Path
import argparse,ast,json,re,subprocess
ROOT=Path(__file__).resolve().parents[5]
HERE=Path(__file__).resolve().parent
KERNEL=ROOT/'arch/aarch64-native/kernel'

def source_checks():
    cpu=(KERNEL/'kernel_cpu.c').read_text()
    switch=cpu.split('void cpu_Switch(',1)[1].split('void cpu_Dispatch(',1)[0]
    assert switch.index('STORE_TASKSTATE(')<switch.index('KrnSpinUnLock(')<switch.index('core_Switch(')
    dispatch=cpu.split('void cpu_Dispatch(',1)[1].split('void cpu_DumpRegs(',1)[0]
    assert dispatch.index('core_Dispatch(')<dispatch.index('AROSAArch64WriteTaskPointer(0)')<dispatch.index('RESTORE_TASKSTATE(')<dispatch.index('Exception()')<dispatch.index('task->tc_Launch')
    assert dispatch.count('RESTORE_TASKSTATE(')==1 and 'NativeTaskTLSRestore' not in dispatch
    private=(KERNEL/'task_context.h').read_text()
    assert 'NativeTaskTLSSave' not in private
    assert private.index('#define RESTORE_TASKSTATE')<private.index('AROSAArch64NativeTaskTLSRestore(ctx)')
    assert 'AROSAArch64NativeTaskTLSInit(ctx)' in (KERNEL/'createcontext.c').read_text()
    assert 'AARCH64_NATIVE_TASK_PREFIX\n' in (KERNEL/'cpu_init.c').read_text()
    assert 'ctx = KrnCreateContext()' in (ROOT/'arch/aarch64-native/exec/preparecontext.c').read_text()
    assert 'bsctx = KrnCreateContext()' in (KERNEL/'kernel_execsmp.c').read_text()
    assert 'tpidr_el0' not in (KERNEL/'vectors.S').read_text().lower()
    assert cpu.index('msr     tpidr_el0, xzr')<cpu.index('msr     tpidr_el1, x3')
    startup=(KERNEL/'kernel_startup.c').read_text()
    assert startup.index('msr     tpidr_el0, xzr')<startup.index('bl      __clear_bss')
    # Both ports use this same ready-list publication and migration ownership.
    scheduler=(KERNEL/'kernel_scheduler.c').read_text()
    assert 'KrnSpinUnLock(&newtask->tc_SpinLock)' in scheduler

def asm_strings(source):
    """Exact top-level asm string tokens; remove C comments before tokenizing."""
    block=re.split(r'\basm\s*\(',source,maxsplit=1)[1].split(');',1)[0]
    block=re.sub(r'/\*.*?\*/','',block,flags=re.S)
    block=re.sub(r'//[^\n]*','',block)
    return ''.join(ast.literal_eval(v) for v in re.findall(r'"(?:[^"\\]|\\.)*"',block))

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
    p.add_argument('--cc',default='clang');p.add_argument('--aarch64-cc',default='/opt/homebrew/opt/llvm/bin/clang')
    p.add_argument('--objdump',default='/opt/homebrew/opt/llvm/bin/llvm-objdump');a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=False);source_checks();records=[]
    def run(name,args):
        cmd=[str(v) for v in args];r=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
        (a.out/(name+'.log')).write_text(r.stdout+r.stderr);records.append(dict(name=name,command=cmd,rc=r.returncode));r.check_returncode();return r.stdout
    for flavor in ['host','native']:
        include=a.out/(flavor+'-include')/'aros/aarch64';include.mkdir(parents=True)
        (include/'cpucontext.h').symlink_to(ROOT/'arch/aarch64-all/include/aros/cpucontext.h')
        if flavor=='host':
            (include/'native_tls.h').write_text('#include <stdint.h>\nextern uint64_t host_cpu_tp[2];extern unsigned host_cpu;\nstatic inline uint64_t AROSAArch64ReadTaskPointer(void){return host_cpu_tp[host_cpu];}\nstatic inline void AROSAArch64WriteTaskPointer(uint64_t v){host_cpu_tp[host_cpu]=v;}\n')
        else:
            (include/'native_tls.h').symlink_to(ROOT/'arch/aarch64-all/include/aros/native_tls.h')
    run('host-compile',[a.cc,'-std=c11','-DNATIVE_TLS_HOST=1','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.out/'host-include','-I',KERNEL,HERE/'test.c','-o',a.out/'host-test'])
    run('host-run',[a.out/'host-test'])
    run('native-context-compile',[a.aarch64_cc,'--target=aarch64-none-elf','-ffreestanding','-std=c11','-O1','-Wall','-Wextra','-Werror','-I',a.out/'native-include','-I',KERNEL,'-c',HERE/'test.c','-o',a.out/'native-context.o'])
    dis=run('native-context-disassembly',[a.objdump,'-d',a.out/'native-context.o']).lower()
    assert not re.search(r'mrs\s+\w+, tpidr_el0',dis) and re.search(r'msr\s+tpidr_el0',dis) and re.search(r'add\s+x\d+, x\d+, #0x118',dis)
    run('vectors-assemble',[a.aarch64_cc,'--target=aarch64-none-elf','-x','assembler-with-cpp','-c',KERNEL/'vectors.S','-o',a.out/'vectors.o'])
    for name,path in [('secondary',KERNEL/'kernel_cpu.c'),('startup',KERNEL/'kernel_startup.c')]:
        extracted=a.out/(name+'.S');extracted.write_text(asm_strings(path.read_text()))
        run(name+'-assemble',[a.aarch64_cc,'--target=aarch64-none-elf','-c',extracted,'-o',a.out/(name+'.o')])
        dis=run(name+'-disassembly',[a.objdump,'-d',a.out/(name+'.o')]).lower()
        assert len(re.findall(r'msr\s+tpidr_el0, xzr',dis))==1
    (a.out/'result.json').write_text(json.dumps(dict(result='PASS_HOST_AND_AARCH64_OBJECTS_ONLY',public_context_bytes=280,private_prefix_bytes=288,simd_offset=288,allocation_bytes=816,records=records,native_execution='NOT_RUN',sdk_mutation=False),indent=2)+'\n')
if __name__=='__main__':main()
