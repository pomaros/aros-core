#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1.
"""Extract actual task-list critical sections; modeled lists/allocator, no SMP claim."""
import argparse,hashlib,json,subprocess,os
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[4]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
 if out.exists() or ROOT in out.parents:p.error('fresh output outside source required')
 out.mkdir(parents=True);source=ROOT/'rom/task/task_support.c';s=source.read_text();s=s[s.index('void task_ListLock'):s.index('/* task hook support */')]
 variants=[('actual',s),('unmasked-owner',s.replace('    Disable();','    /* Missing local exclusion. */')),
 ('free-under-lock',s.replace('    task_ListUnlock(TaskResBase);\n    if (retired) FreeMem(retired,sizeof(*retired));','    if (retired) FreeMem(retired,sizeof(*retired));\n    task_ListUnlock(TaskResBase);')),
 ('ignore-iterators',s.replace('if (IsListEmpty(&TaskResBase->trb_LockedLists))','if (1)'))]
 inputs=[source,HERE/'run.py',HERE/'test.c'];r={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),'inputs':{str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs},'variants':[]}
 try:
  for name,code in variants:
   folder=out/name;folder.mkdir();(folder/'actual.inc').write_text(code);binary=folder/'test'
   cmd=['clang','-std=c11','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(folder),str(HERE/'test.c'),'-o',str(binary)]
   b=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(folder/'build.log').write_text(b.stdout+b.stderr)
   if b.returncode:raise RuntimeError(name+' build failed')
   q=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'});(folder/'run.log').write_text(q.stdout+q.stderr)
   expected={'unmasked-owner':'disabled && !held && !*p','free-under-lock':'!held && !disabled && bytes==sizeof(struct TaskListEntry)','ignore-iterators':'!freed && !ea->tle_Task && !GetTaskEntry(&a,&base)'}
   ok=q.returncode==0 and q.stdout=='PASS task list exclusion deferred free and iterator retention\n' and not q.stderr if name=='actual' else q.returncode==-6 and expected[name] in q.stderr and 'Assertion failed:' in q.stderr and 'AddressSanitizer' not in q.stderr
   r['variants'].append({'name':name,'exit':q.returncode,'passed':ok,'command':cmd})
   if not ok:raise RuntimeError(name+' unexpected result')
  if r['inputs']!={str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs}:raise RuntimeError('inputs changed')
  r['result']='DIAGNOSTIC_PASS' if r['dirty_inputs'] else 'HOST_PASS'
 except Exception as e:r['error']=str(e)
 (out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(r['result'],r.get('error',''));return r['result']=='FAIL'
if __name__=='__main__':raise SystemExit(main())
