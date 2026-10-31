#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Sequential strict HOST sanitizer replay and semantic mutation controls."""
import argparse,hashlib,json,os,signal,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
KERNEL=ROOT/'arch/aarch64-all/kernel'
def identity(path):
    path=Path(path).resolve()
    return dict(path=str(path),bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def dump(path,value):
    path.write_text(json.dumps(value,indent=2)+'\n')
EXPECTED_ASSERTIONS={
    'execute-nx-bypass':('!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,4,1,&pa) && pa==SENTINEL','user_admission'),
    'execute-write-alias':('!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,4,1,&pa) && pa==SENTINEL','user_admission'),
    'alias-user-access':('!AArch64RemapPrivilegedPage(&o,f.root,f.tcr,f.va,DATA_PA,5)','privileged_alias'),
    'alias-user-execute':('!AArch64RemapPrivilegedPage(&o,f.root,f.tcr,f.va,DATA_PA,3)','privileged_alias'),
    'allow-privileged-buffer':('!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,1,1,&pa) && pa==SENTINEL','user_admission'),
    'allow-readonly-output':('!AArch64UserPagePhysical(&query,f.root,f.tcr,f.va,2,1,&pa) && pa==SENTINEL','user_admission'),
    'wrong-unmap-owner':('!AArch64UnmapPage(&o,f.root,f.tcr,f.va,DATA_PA+((uint64_t)1<<page))','basic'),
    'permit-jit-bit':('!AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,bad[k],1)','permissions'),
    'drop-translation-offset':('AArch64TranslateResident(&o,f.root,f.tcr,f.va+123,&pa) && pa==DATA_PA+123','basic'),
    'ignore-physical-width':('!AArch64MapPage(&o,f.root,f.tcr,f.va,DATA_PA,1,1)','allocation_failures'),
    'permit-tagged-geometry':('!AArch64MapPage(&o,f.root,tcr,f.va,DATA_PA,1,1)','invalid_geometry'),
}
def expected_assertion(name,returncode,stderr):
    expression,function=EXPECTED_ASSERTIONS[name]
    prefix=('Assertion failed: ('+expression+'), function '+function+', file ').encode()
    lines=stderr.splitlines()
    return (returncode==-signal.SIGABRT and len(lines)==1 and
            lines[0].startswith(prefix) and stderr.count(b'Assertion failed:')==1)
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,required=True);parser.add_argument('--cc',default='/usr/bin/clang');parser.add_argument('--allow-dirty-diagnostic',action='store_true');args=parser.parse_args()
    out=args.out.resolve()
    if out.exists():raise ValueError('fresh output directory required')
    if out == ROOT or ROOT in out.parents:raise ValueError('output must be outside production source')
    source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    dirty=bool(subprocess.check_output(['git','status','--porcelain','--','arch/aarch64-all/kernel','developer/debug/test/kernel/aarch64_mapping_host'],cwd=ROOT,text=True))
    if dirty and not args.allow_dirty_diagnostic:raise ValueError('dirty source requires diagnostic mode')
    paths=[KERNEL/'mapping.c',KERNEL/'mapping.h',KERNEL/'setprotection.c',KERNEL/'protection.h',HERE/'test.c',Path(__file__)]
    before=[identity(p)for p in paths]
    out.mkdir(parents=True);dump(out/'inputs.json',before)
    env=dict(os.environ);env['ASAN_OPTIONS']='halt_on_error=1:detect_leaks=0';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    original=(KERNEL/'mapping.c').read_text()
    mutants={
      'execute-nx-bypass':('(*slot & M_UXN) || !(*slot & (UINT64_C(1) << 7))','!(*slot & (UINT64_C(1) << 7))'),
      'execute-write-alias':('(*slot & M_UXN) || !(*slot & (UINT64_C(1) << 7))','(*slot & M_UXN)'),
      'alias-user-access':('(*slot & (UINT64_C(1)<<6))','0'),
      'alias-user-execute':('!(*slot & M_UXN)','0'),
      'allow-privileged-buffer':('!(*slot & (UINT64_C(1) << 6)) ||','0 ||'),
      'allow-readonly-output':('(access == AARCH64_PROTECT_WRITE && (*slot & (UINT64_C(1) << 7))) ||','0 ||'),
      'wrong-unmap-owner':('(*slot & walk.mask) != pa ||','0 ||'),
      'permit-jit-bit':('!(permissions & ~31U)','!(permissions & ~63U)'),
      'drop-translation-offset':('*physical=pa|(va&offset);','*physical=pa;'),
      'ignore-physical-width':('!(pa >> walk->physical_bits)','1'),
      'permit-tagged-geometry':('(UINT64_C(1) << 37)','UINT64_C(0)'),
    }
    variants={'production':KERNEL/'mapping.c'}
    for name,(needle,replacement)in mutants.items():
        if original.count(needle)!=1:raise ValueError('mutation location ambiguous '+name)
        target=out/(name+'.c');target.write_text(original.replace(needle,replacement));variants[name]=target
    records=[]
    for name,source in variants.items():
        binary=out/name
        argv=[args.cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(KERNEL),str(HERE/'test.c'),str(source),str(KERNEL/'setprotection.c'),'-o',str(binary)]
        compile_result=subprocess.run(argv,env=env,capture_output=True,timeout=30)
        (out/(name+'-compile.stdout')).write_bytes(compile_result.stdout);(out/(name+'-compile.stderr')).write_bytes(compile_result.stderr)
        record=dict(variant=name,compile_command=argv,compile_rc=compile_result.returncode,source=identity(source))
        if compile_result.returncode:
            records.append(record);dump(out/'result.json',dict(result='FAIL',reason='compile',records=records));return 1
        run=subprocess.run([str(binary)],env=env,capture_output=True,timeout=20)
        (out/(name+'.stdout')).write_bytes(run.stdout);(out/(name+'.stderr')).write_bytes(run.stderr)
        record.update(run_rc=run.returncode,binary=identity(binary))
        if name=='production':
            passed=run.returncode==0 and b'result=PASS'in run.stdout and not run.stderr
            record['result']='PASS'if passed else'FAIL'
        else:
            # A compile failure, unrelated sanitizer crash, or silent nonzero
            # does not demonstrate that the intended semantic assertion fired.
            passed=expected_assertion(name,run.returncode,run.stderr)
            record['expected_signal']='SIGABRT'
            record['expected_assertion'],record['expected_function']=EXPECTED_ASSERTIONS[name]
            record['result']='EXPECTED_ASSERTION'if passed else'FAIL'
        records.append(record)
        if not passed:
            dump(out/'result.json',dict(result='FAIL',reason=name,records=records));return 1
    after=[identity(p)for p in paths]
    if after!=before:raise ValueError('source changed during replay')
    dump(out/'result.json',dict(result='DIAGNOSTIC_PASS' if dirty else 'PASS',source_commit=source_commit,dirty_source=dirty,scope='HOST_actual_C_only_native_NOT_RUN',sanitizers=['address','undefined'],leak_sanitizer='UNSUPPORTED_ON_HOST_TABLE_LEDGER_FREED_EXPLICITLY',production_variants=1,semantic_mutants_rejected=len(mutants),inputs_unchanged=True,records=records))
    print(json.dumps(dict(result='DIAGNOSTIC_PASS' if dirty else 'PASS',production_variants=1,semantic_mutants_rejected=len(mutants),out=str(out))))
    return 0
if __name__=='__main__':raise SystemExit(main())
