#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Sanitized HOST regression of actual privileged-root clone helper."""
import argparse, hashlib, json, os, subprocess, sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
KERNEL=ROOT/'arch/aarch64-all/kernel'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--cc',default='clang');p.add_argument('--allow-dirty-diagnostic',action='store_true');a=p.parse_args()
 out=a.out.resolve()
 if out.exists() or out==ROOT or ROOT in out.parents:p.error('fresh output outside source required')
 dirty=bool(subprocess.check_output(['git','status','--porcelain','--',str(KERNEL.relative_to(ROOT)),str(HERE.relative_to(ROOT))],cwd=ROOT))
 if dirty and not a.allow_dirty_diagnostic:p.error('dirty inputs require diagnostic mode')
 out.mkdir(parents=True)
 inputs=[KERNEL/x for x in ('mapping.c','mapping.h','setprotection.c','protection.h')]+[HERE/'test.c',HERE/'run.py']
 frozen={str(x.relative_to(ROOT)):sha(x) for x in inputs}
 result={'result':'FAIL','scope':'HOST root snapshot helper only; no restricted AROS execution','source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':dirty,'input_hashes':frozen,'records':[]}
 controls=[('keep-user','(desc & ~(UINT64_C(1) << 6)) | M_UXN','desc | M_UXN','deny-el0'),('omit-uxn','(desc & ~(UINT64_C(1) << 6)) | M_UXN','(desc & ~(UINT64_C(1) << 6))','force-uxn'),('omit-pxn','desc |= M_PXN;','desc |= 0;','preserve-el1-execute'),('ignore-budget','if (!*budget) return 0;','if (0) return 0;','budget-refusal'),('unused-root','i >= entries || !(desc & M_VALID)','(!entries) || !(desc & M_VALID)','unused-root-entry')]
 env={**os.environ,'ASAN_OPTIONS':'abort_on_error=1:detect_leaks='+('0' if sys.platform=='darwin' else '1'),'UBSAN_OPTIONS':'halt_on_error=1'}
 try:
  result['compiler']=subprocess.check_output([a.cc,'--version'],text=True).splitlines()[0]
  original=(KERNEL/'mapping.c').read_text()
  for name,old,new,fail in [('baseline',None,None,None)]+controls:
   source=KERNEL/'mapping.c'
   if old:
    if original.count(old)!=1:raise RuntimeError('ambiguous mutation '+name)
    source=out/(name+'.c');source.write_text(original.replace(old,new))
   binary=out/name
   cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(KERNEL),str(source),str(KERNEL/'setprotection.c'),str(HERE/'test.c'),'-o',str(binary)]
   record={'name':name,'command':cmd};result['records'].append(record)
   b=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(out/(name+'-build.log')).write_text(b.stdout+b.stderr);record['build_exit']=b.returncode
   if b.returncode:raise RuntimeError(name+': build failed')
   record['binary_sha256']=sha(binary)
   r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10,env=env);record.update(exit=r.returncode,stdout=r.stdout,stderr=r.stderr)
   expected='FAIL '+fail+'\n' if fail else 'PASS privileged root clone geometry permissions ownership refusals\n'
   if r.returncode!=(1 if fail else 0) or r.stdout!=expected or r.stderr:raise RuntimeError(name+': unexpected result')
   record['result']='PASS'
  if frozen!={str(x.relative_to(ROOT)):sha(x) for x in inputs}:raise RuntimeError('inputs changed')
  result['result']='DIAGNOSTIC_PASS' if dirty else 'PASS'
 except Exception as e:result['error']=str(e)
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ('result','error') if k in result}));return int(result['result']=='FAIL')
if __name__=='__main__':raise SystemExit(main())
