#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual production space/channel adapter with synthetic PTE memory; HOST only."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--allow-dirty-diagnostic',action='store_true');a=p.parse_args()
 out=a.out.resolve();native=ROOT/'arch/aarch64-apple/kernel';shared=ROOT/'arch/aarch64-all/kernel';source=native/'restricted_service.c'
 if out.exists() or ROOT in out.parents:p.error('fresh output outside checkout required')
 dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT))
 if dirty and not a.allow_dirty_diagnostic:p.error('commit inputs or select diagnostic mode')
 inputs=[source,native/'restricted_service.h',ROOT/'arch/aarch64-apple/include/aros/apple/restricted.h',shared/'mapping.c',shared/'mapping.h',shared/'setprotection.c',shared/'protection.h',HERE/'test.c',Path(__file__)]
 inputs+=sorted((ROOT/'rom/kernel/mapping').glob('*.[ch]'))
 hashes={str(p.relative_to(ROOT)):sha(p) for p in inputs};out.mkdir(parents=True)
 r={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':dirty,'input_hashes':hashes,'records':[]}
 original=source.read_text();cases=[('baseline',None,None,None),
  ('channel-stops-domain','s->closing=1;','s->closing=1; KrnMappingDomainStop(&s->space->domain);','channel-close-keeps-domain'),
  ('stale-channel','active[index-1].generation==(uint32_t)(handle>>32)','handle!=0','stale-channel-refused'),
  ('early-space-unmap','if (s->channels) return 0;','if (0 && s->channels) return 0;','space-keeps-live-backing')]
 try:
  for name,before,after,oracle in cases:
   used=source
   if before:
    if original.count(before)!=1:raise RuntimeError('mutation must match once: '+name)
    used=out/(name+'.c');used.write_text(original.replace(before,after))
   binary=out/name
   cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-I'+str(native),'-I'+str(ROOT/'arch/aarch64-apple/include'),str(HERE/'test.c'),str(used),str(shared/'mapping.c'),str(shared/'setprotection.c'),'-o',str(binary)]
   build=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(out/(name+'-build.log')).write_text(build.stdout+build.stderr)
   if build.returncode:raise RuntimeError(name+' build failed')
   run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'abort_on_error=1:detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
   (out/(name+'.log')).write_text(run.stdout+run.stderr)
   expected='FAIL '+oracle+'\n' if oracle else 'PASS restricted space and channel ownership\n'
   row={'name':name,'command':cmd,'build_exit':build.returncode,'exit':run.returncode,'stdout':run.stdout,'stderr':run.stderr,'binary_sha256':sha(binary)};r['records'].append(row)
   if run.returncode!=bool(oracle) or run.stdout!=expected or run.stderr:raise RuntimeError(name+' semantic mismatch')
  if hashes!={str(p.relative_to(ROOT)):sha(p) for p in inputs}:raise RuntimeError('inputs changed')
  r['result']='DIAGNOSTIC_PASS' if dirty else 'PASS'
 except Exception as e:r['error']=str(e)
 (out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'result':r['result'],'error':r.get('error')}));return int(r['result']=='FAIL')
if __name__=='__main__':raise SystemExit(main())
