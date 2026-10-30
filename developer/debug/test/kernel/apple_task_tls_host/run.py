#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1
"""Actual TLS-context helpers on HOST, and AArch64 assembly without a kernel build."""
from pathlib import Path
import argparse,json,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[5]
HERE=Path(__file__).resolve().parent

def check_source():
 cpu=(ROOT/'arch/aarch64-apple/kernel/kernel_cpu.c').read_text()
 assert 'TaskTLSSave' not in cpu
 switch=cpu.split('void cpu_Switch(',1)[1].split('\n}',1)[0]
 assert switch.index('cpu_SaveContext(')<switch.index('core_Switch(')<switch.index('EXEC_SPINLOCK_UNLOCK(')
 dispatch=cpu.split('void cpu_Dispatch(',1)[1]
 assert dispatch.index('core_Dispatch(')<dispatch.index('AROSAppleTaskTLSRestore(context)')
 assert 'AROSAArch64WriteTaskPointer(0)' in cpu.split('static void enter_idle(',1)[1].split('\n}',1)[0]
 create=(ROOT/'arch/aarch64-apple/kernel/createcontext.c').read_text();assert 'AROSAppleTaskTLSInit(context)' in create
 prepare=(ROOT/'arch/aarch64-all/exec/preparecontext.c').read_text();assert 'ctx = KrnCreateContext()' in prepare
 intr=(ROOT/'rom/kernel/kernel_intr.c').read_text();assert intr.count('cpu_Switch(regs)')==2 and intr.count('cpu_Dispatch(regs)')==2
 crash=(ROOT/'arch/aarch64-apple/kernel/task_crash.c').read_text();assert 'cpu_SaveContext(frame, task)' in crash and 'cpu_Dispatch(frame)' in crash
 vectors=(ROOT/'arch/aarch64-apple/kernel/runtime_vectors.S').read_text().lower()
 assert 'mrs     x1, tpidr_el0' in vectors and 'msr     tpidr_el0, x1' in vectors
 assert 'regs->task_tls = AROSAArch64ReadTaskPointer()' in dispatch
 assert 'regs->task_tls = 0' in cpu
 bootstrap=(ROOT/'arch/aarch64-apple/kernel/startup_prepare.c').read_text();assert bootstrap.index('msr tpidr_el0')<bootstrap.index('initialize_state(state)')
 secondary=(ROOT/'arch/aarch64-apple/kernel/smp_entry.S').read_text()
 # The shared backend has one PSCI entry, not the former two hardware entries.
 entry=secondary.split('AROSAppleSMPEntry:',1)[1].split('.Lfault:',1)[0]
 assert entry.count('msr tpidr_el0,xzr')==1
 assert entry.index('msr tpidr_el0,xzr')<entry.index('bl AROSAppleSMPMain')

def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--cc',default='clang');p.add_argument('--aarch64-cc',default='/opt/homebrew/opt/llvm/bin/clang');p.add_argument('--objdump',default='/opt/homebrew/opt/llvm/bin/llvm-objdump');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
 check_source();records=[]
 def run(name,cmd):
  r=subprocess.run([str(v) for v in cmd],capture_output=True,text=True,timeout=30);(a.out/(name+'.log')).write_text(r.stdout+r.stderr);records.append(dict(name=name,command=[str(v) for v in cmd],rc=r.returncode));r.check_returncode();return r.stdout
 include=a.out/'include/aros/aarch64';include.mkdir(parents=True)
 types=a.out/'include/exec';types.mkdir()
 (types/'types.h').write_text('#include <stdint.h>\ntypedef uint64_t UQUAD;typedef uint32_t ULONG;typedef void *APTR;\n')
 (include/'cpucontext.h').symlink_to(ROOT/'arch/aarch64-all/include/aros/cpucontext.h')
 (include/'native_tls.h').write_text('#include <stdint.h>\nextern uint64_t host_cpu_tp[2];extern unsigned host_cpu;\nstatic inline uint64_t AROSAArch64ReadTaskPointer(void){return host_cpu_tp[host_cpu];}\nstatic inline void AROSAArch64WriteTaskPointer(uint64_t v){host_cpu_tp[host_cpu]=v;}\n')
 apple=a.out/'include/aros/apple';apple.mkdir(parents=True)
 (apple/'runtime.h').symlink_to(ROOT/'arch/aarch64-apple/include/aros/apple/runtime.h')
 (apple/'runtime_local.h').symlink_to(ROOT/'arch/aarch64-apple/include/aros/apple/runtime_local.h')
 run('host-compile',[a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.out/'include','-I',ROOT/'arch/aarch64-apple/kernel',HERE/'test.c','-o',a.out/'host-test'])
 run('host-run',[a.out/'host-test'])
 # Compile the actual private wrapper and public context on AArch64 too.
 # Only scalar typedefs replace SDK headers; native register helpers are real.
 native=a.out/'native-include/aros/aarch64';native.mkdir(parents=True)
 native_types=a.out/'native-include/exec';native_types.mkdir()
 (native_types/'types.h').write_text((types/'types.h').read_text())
 (native/'cpucontext.h').symlink_to(ROOT/'arch/aarch64-all/include/aros/cpucontext.h')
 (native/'native_tls.h').symlink_to(ROOT/'arch/aarch64-all/include/aros/native_tls.h')
 native_apple=a.out/'native-include/aros/apple';native_apple.mkdir(parents=True)
 (native_apple/'runtime.h').symlink_to(ROOT/'arch/aarch64-apple/include/aros/apple/runtime.h')
 (native_apple/'runtime_local.h').symlink_to(ROOT/'arch/aarch64-apple/include/aros/apple/runtime_local.h')
 fixture=a.out/'register-access.c';fixture.write_text('#include <stdint.h>\ntypedef uint64_t UQUAD;typedef uint32_t ULONG;typedef void *APTR;\n#include "task_tls.h"\n_Static_assert(AROS_APPLE_DISPATCH_CONTEXT_SIZE == 848, "owned size");\nvoid roundtrip(struct ExceptionContext *v){AROSAppleTaskTLSInit(v);AROSAppleTaskTLSRestore(v);}\n')
 run('aarch64-compile',[a.aarch64_cc,'--target=aarch64-none-elf','-ffreestanding','-std=c11','-O1','-Wall','-Wextra','-Werror','-I',a.out/'native-include','-I',ROOT/'arch/aarch64-apple/kernel','-c',fixture,'-o',a.out/'register-access.o'])
 assembly=run('aarch64-disassembly',[a.objdump,'-d',a.out/'register-access.o']).lower();assert re.search(r'msr\s+tpidr_el0',assembly) and not re.search(r'mrs\s+\w+, tpidr_el0',assembly) and re.search(r'add\s+x\d+, x0, #0x118',assembly) and 'isb' in assembly
 run('vectors-assemble',[a.aarch64_cc,'--target=aarch64-none-elf','-I',ROOT/'arch/aarch64-apple/include','-x','assembler-with-cpp','-c',ROOT/'arch/aarch64-apple/kernel/runtime_vectors.S','-o',a.out/'runtime-vectors.o'])
 run('secondary-assemble',[a.aarch64_cc,'--target=aarch64-none-elf','-x','assembler-with-cpp','-c',ROOT/'arch/aarch64-apple/kernel/smp_entry.S','-o',a.out/'smp-entry.o'])
 secondary=run('secondary-disassembly',[a.objdump,'-d',a.out/'smp-entry.o']).lower();assert len(re.findall(r'msr\s+tpidr_el0, xzr',secondary))==1
 (a.out/'result.json').write_text(json.dumps(dict(result='PASS_HOST_AND_AARCH64_ASSEMBLY_ONLY',public_context_bytes=280,private_prefix_bytes=288,simd_offset=288,allocation_bytes=848,records=records,native_execution='NOT_RUN',sdk_mutation=False),indent=2)+'\n')
if __name__=='__main__':main()
