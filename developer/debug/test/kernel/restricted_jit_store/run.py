#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Differential scalar release stores and extracted restricted admission, HOST and QEMU."""
import argparse
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
KERNEL=ROOT/'arch/aarch64-all/kernel'
SOURCE=ROOT/'arch/aarch64-apple/kernel/restricted.c'
EXPECTED='PASS release stores and restricted admission\n'

def function(text,name):
    start=text.rfind('static ',0,text.index(name))
    end=text.index('{',text.index(name))+1
    depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]+'\n'

def corpus(out):
    # Assembler encodings, not copies of the decoder's masks. Each function
    # also executes that very instruction for the differential comparison.
    specs=[('stlurb',1,0,1,-7),('stlurb',1,0,1,255),
           ('stlurh',2,0,1,-256),('stlurh',2,0,1,254),
           ('stlur',4,0,1,-252),('stlur',4,0,1,252),
           ('stlur',8,0,1,-256),('stlur',8,0,1,248),
           ('stlur',4,4,20,8),('stlur',8,0,31,0),
           ('stlur',8,31,1,-8),('stlurb',1,31,31,15)]
    assembly=['.text','.arch armv8.4-a',
              '#ifdef __APPLE__','#define SYM(x) _##x','#else','#define SYM(x) x','#endif']
    header=[];rows=[]
    for i,(op,size,rn,rt,offset) in enumerate(specs):
        fn=f'probe{i}';ins=f'instruction{i}'
        assembly += [f'.global SYM({fn}), SYM({ins})','.p2align 2',f'SYM({fn}):']
        if rt==20: assembly += ['str x20,[sp,#-16]!','mov x20,x1']
        if rn==4: assembly += ['mov x4,x0']
        if rn==31: assembly += ['mov x9,sp','mov sp,x0']
        reg=('x' if size==8 else 'w')+('zr' if rt==31 else str(rt))
        base='sp' if rn==31 else f'x{rn}'
        assembly += [f'SYM({ins}):',f'{op} {reg},[{base},#{offset}]']
        if rn==31: assembly += ['mov sp,x9']
        if rt==20: assembly += ['ldr x20,[sp],#16']
        assembly += ['ret']
        header += [f'extern void {fn}(void *,uint64_t);extern const uint32_t {ins};']
        rows += [f'{{{fn},&{ins},{size},{rn},{rt},{offset}}}']
    (out/'corpus.S').write_text('\n'.join(assembly)+'\n')
    (out/'corpus.inc').write_text('\n'.join(header)+'\nstatic const struct StoreCase cases[]={'+','.join(rows)+'};\n')

def main():
    global ROOT,HERE,KERNEL,SOURCE
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--cc',default='/opt/homebrew/opt/llvm/bin/clang')
    p.add_argument('--qemu',default='qemu-system-aarch64')
    p.add_argument('--source',type=Path,default=ROOT,help='selected common or composed source tree')
    a=p.parse_args();out=a.out.resolve()
    ROOT=a.source.resolve();HERE=ROOT/'developer/debug/test/kernel/restricted_jit_store'
    KERNEL=ROOT/'arch/aarch64-all/kernel';SOURCE=ROOT/'arch/aarch64-apple/kernel/restricted.c'
    if out.exists() or ROOT in out.parents:p.error('fresh output outside source required')
    out.mkdir(parents=True)
    inputs=[SOURCE,KERNEL/'release_store.h',*sorted(HERE.glob('*.[cS]')),Path(__file__).resolve(),
            ROOT/'arch/aarch64-apple/include/aros/apple/runtime.h',
            ROOT/'arch/aarch64-apple/include/aros/apple/runtime_local.h',
            HERE.parent/'restricted_domain_qemu/link.ld',HERE.parent/'restricted_domain_qemu/run.py']
    composition=ROOT/'COMPOSED-SOURCE.json'
    if composition.is_file():
        identity=json.loads(composition.read_text());inputs.append(composition)
        source_commit=identity['core_revision'];dirty=None
    else:
        identity=None
        source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
        dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT))
    hashes=lambda:{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}
    before=hashes()
    result={'result':'FAIL','scope':__doc__,'source_commit':source_commit,
            'dirty_inputs':dirty,'composition':identity,
            'input_hashes':before,'records':[]}
    text=SOURCE.read_text()
    actual=''.join(function(text,n) for n in ['r_jit_page(const','r_jit_store_fault(const','r_emulate_jit_store(struct'])
    variants=[('baseline',actual,None),
              ('ignore-far',actual.replace('store.address!=frame->far ||',''), 'FAIL emulation-refusal\n'),
              ('no-advance',actual.replace('frame->elr+=4;',''), 'FAIL only-pc-changes\n'),
              ('ignore-source-class',actual.replace('r_jit_page(r,frame->elr) &&',''), 'FAIL classification-refusal\n'),
              ('relaxed-order',actual,'ORDER')]
    try:
        # A supported store returns before paging. A refused same-class store
        # cannot be handed to the lazy pager for an endless no-change repair.
        trap=function(text,'r_trap_locked(struct')
        assert 'if (jit_store && r_emulate_jit_store(r,frame)) return 1;' in trap
        assert 'if (!jit_store && ec==0x24 &&' in trap
        for name,body,failure in variants:
            dest=out/name;dest.mkdir();corpus(dest);(dest/'actual.inc').write_text(body)
            header=(KERNEL/'release_store.h').read_text()
            if failure=='ORDER':header=header.replace('__ATOMIC_RELEASE','__ATOMIC_RELAXED')
            (dest/'release_store.h').write_text(header)
            common=['-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(dest),'-I'+str(KERNEL),
                    '-I'+str(ROOT/'arch/aarch64-apple/include'),str(HERE/'test.c'),str(dest/'corpus.S')]
            cmd=[a.cc,*common,'-g','-fsanitize=address,undefined','-o',str(dest/'host')]
            build=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
            (dest/'host-build.log').write_text(build.stdout+build.stderr)
            if build.returncode:raise RuntimeError(name+' host build')
            run=subprocess.run([str(dest/'host')],capture_output=True,text=True,timeout=15,
                               env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
            (dest/'host.log').write_text(run.stdout+run.stderr)
            result['records'].append({'name':name,'command':cmd,'host_exit':run.returncode})
            if failure and failure!='ORDER':
                if run.returncode!=1 or run.stderr!=failure:raise RuntimeError(name+' missed negative control')
                continue
            if run.returncode or run.stdout!=EXPECTED or run.stderr:raise RuntimeError('HOST differential test')
            elf=dest/'test.elf'
            cmd=[a.cc,'--target=aarch64-none-elf','-march=armv8.4-a','-ffreestanding','-fno-builtin',
                 '-mgeneral-regs-only','-nostdlib','-fuse-ld=lld','-DFREESTANDING',*common,
                 str(HERE/'entry.S'),str(HERE/'freestanding.c'),
                 '-Wl,-T,'+str(HERE.parent/'restricted_domain_qemu/link.ld'),'-o',str(elf)]
            build=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
            (dest/'qemu-build.log').write_text(build.stdout+build.stderr)
            if build.returncode:raise RuntimeError('QEMU build')
            disassembly=subprocess.check_output([str(Path(a.cc).with_name('llvm-objdump')),
                '--disassemble-symbols=AROSReleaseStoreTest','--no-show-raw-insn',str(elf)],text=True)
            (dest/'release-disassembly.txt').write_text(disassembly)
            release=all(re.search(pattern,disassembly) for pattern in
                        [r'\bstlrb\s+w',r'\bstlrh\s+w',r'\bstlr\s+w',r'\bstlr\s+x'])
            if failure=='ORDER':
                if release:raise RuntimeError('relaxed negative control retained release instructions')
                result['records'][-1]['release_order_rejected']=True
                continue
            if not release:raise RuntimeError('compiler did not preserve all release widths')
            spec=importlib.util.spec_from_file_location('domain_fixture',HERE.parent/'restricted_domain_qemu/run.py')
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            with tempfile.TemporaryDirectory(prefix='release-store-') as tmp:
                sock=Path(tmp)/'qmp'
                command=[shutil.which(a.qemu),'-machine','virt,virtualization=off','-cpu','max','-accel','tcg',
                         '-smp','1','-m','128M','-display','none','-monitor','none','-serial','stdio','-no-reboot','-S',
                         '-qmp','unix:'+str(sock)+',server=on,wait=off','-semihosting-config','enable=on,target=native','-kernel',str(elf)]
                proc=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                monitor=None
                try:
                    monitor=module.Monitor(sock,proc)
                    status=monitor.call('query-status');regs=monitor.call('human-monitor-command',{'command-line':'info registers'})
                    (dest/'qemu-preflight.txt').write_text(json.dumps(status)+'\n'+regs)
                    if status['running'] or 'PC=0000000040080000' not in regs:raise RuntimeError('QEMU preflight')
                    monitor.call('cont')
                    stdout,stderr=proc.communicate(timeout=15)
                    (dest/'qemu.log').write_text(stdout+stderr)
                    if proc.returncode or stdout!=EXPECTED or stderr:raise RuntimeError('QEMU differential test')
                finally:
                    if proc.poll() is None:proc.kill();proc.wait()
                    if monitor:monitor.close()
                result['records'][-1].update(qemu_command=command,qemu_build=cmd,qemu_exit=proc.returncode,
                                            elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest())
        if hashes()!=before:raise RuntimeError('inputs changed')
        result['result']='PASS' if result['dirty_inputs'] is False else 'DIAGNOSTIC_PASS'
    except Exception as e:result['error']=str(e)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('result','error') if k in result}))
    return result['result'] not in ('PASS','DIAGNOSTIC_PASS')
if __name__=='__main__':raise SystemExit(main())
