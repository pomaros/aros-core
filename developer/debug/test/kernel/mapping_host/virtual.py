#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""HOST anonymous reservation/commit/decommit with actual zero pager; no guest claim."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[4];SRC=ROOT/'rom/kernel/mapping'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--allow-dirty-diagnostic',action='store_true');a=p.parse_args();out=a.out.resolve()
 if out.exists() or ROOT in out.parents:p.error('fresh output outside source required')
 dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT))
 if dirty and not a.allow_dirty_diagnostic:p.error('commit inputs or select diagnostic mode')
 paths=[SRC/'mapping.c',SRC/'mapping.h',SRC/'pager.c',SRC/'pager.h',HERE/'virtual_test.c',Path(__file__).resolve()]
 hashes={str(x.relative_to(ROOT)):sha(x) for x in paths};out.mkdir(parents=True)
 result={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':dirty,'input_hashes':hashes,'records':[]}
 cases=[('baseline',None,None,None,None),
 ('decommit-pins','mapping.c','m->closing || m->in_flight ||\n        index>=m->page_count || !released','m->closing ||\n        index>=m->page_count || !released','decommit-pinned'),
 ('decommit-remove','mapping.c','!ops->remove(ops->context,m->start+index*m->page_size,physical)','0','decommit-remove-refusal'),
 ('decommit-demand','mapping.c','*p=(struct KrnMappingPage){0,p->generation+1,KMAP_RESERVED,0,p->protection};','*p=(struct KrnMappingPage){0,p->generation+1,KMAP_ABSENT,0,p->protection};','decommit-no-demand-reload'),
 ('commit-preflight','mapping.c','p->state==KMAP_RESERVED && (p->physical || p->generation==UINT64_MAX)','p->state==KMAP_RESERVED && (p->physical || 0)','commit-preflight'),
 ('zero-fill','pager.c','bytes[i]=0;','bytes[i]=0xa5;','zero-bytes')]
 try:
  for name,file,before,after,oracle in cases:
   sources={n:SRC/n for n in ['mapping.c','pager.c']}
   if file:
    text=sources[file].read_text()
    if text.count(before)!=1:raise RuntimeError(name+' mutation must match once')
    sources[file]=out/(name+'.c');sources[file].write_text(text.replace(before,after))
   binary=out/name
   cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-I'+str(SRC),str(HERE/'virtual_test.c'),*[str(x) for x in sources.values()],'-o',str(binary)]
   b=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(out/(name+'-build.log')).write_text(b.stdout+b.stderr)
   if b.returncode:raise RuntimeError(name+' compile failure')
   r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'abort_on_error=1:detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
   row={'name':name,'command':cmd,'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr,'binary_sha256':sha(binary)};result['records'].append(row);(out/(name+'.log')).write_text(r.stdout+r.stderr)
   expected='FAIL '+oracle+'\n' if oracle else 'PASS virtual reserve commit decommit zero reuse\n'
   if r.returncode!=bool(oracle) or r.stdout!=expected or r.stderr:raise RuntimeError(name+' semantic mismatch')
  if hashes!={str(x.relative_to(ROOT)):sha(x) for x in paths}:raise RuntimeError('input changed')
  result['result']='DIAGNOSTIC_PASS' if dirty else 'PASS'
 except Exception as e:result['error']=str(e)
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'result':result['result'],'error':result.get('error')}));return int(result['result']=='FAIL')
if __name__=='__main__':raise SystemExit(main())
