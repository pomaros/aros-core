#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Execute the actual NameFromLock body with fault-injected DOS dependencies."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
p=argparse.ArgumentParser()
p.add_argument('--output',required=True,type=Path)
a=p.parse_args()
here=Path(__file__).resolve().resolve().parent
root=here.parents[4]
source=root/'rom/dos/namefromlock.c'
s=source.read_text()
s=s[s.index('{\n    AROS_LIBFUNC_INIT'):s.index('    AROS_LIBFUNC_EXIT')]
s='static BOOL actual_name(BPTR lock, STRPTR buffer, LONG length, struct DosLibrary *DOSBase)\n'+s+'}\n'
out=a.output.resolve()
if out == root or root in out.parents: p.error('output must be outside source')
out.mkdir(parents=True,exist_ok=False)
rows=[]
for name in ['positive','missing-dup-check','lost-traversal-error']:
 lane=out/name;lane.mkdir()
 code=s
 if name=='missing-dup-check':
  before='    if (lock2 == BNULL)\n        return DOSFALSE;'
  assert code.count(before)==1;code=code.replace(before,'')
 if name=='lost-traversal-error':
  before='    SetIoErr(err);'
  assert code.count(before)==1;code=code.replace(before,'    (void)err;')
 (lane/'actual.c').write_text(code)
 cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I'+str(lane),str(here/'host.c'),'-o',str(lane/'host')]
 b=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(lane/'compile.log').write_text(b.stdout+b.stderr)
 r=None
 if b.returncode==0:
  r=subprocess.run([str(lane/'host')],capture_output=True,text=True,timeout=15)
  (lane/'run.log').write_text(r.stdout+r.stderr)
  if name != 'positive':
   expected = '!actual_name(17,buffer,sizeof(buffer),&base)' if name == 'missing-dup-check' else 'error==120 && duplicates==1 && traversals==1 && unlocks==1'
   assert r.returncode != 0 and expected in r.stderr, r.stderr
 rows.append({'name':name,'command':cmd,'compile_exit':b.returncode,'run_exit':None if r is None else r.returncode})
record={'scope':'Actual NameFromLock body, host fault-injected DupLock/namefrom_internal/UnLock. No filesystem guest execution.', 'inputs':{str(v.relative_to(root)):hashlib.sha256(v.read_bytes()).hexdigest() for v in [source,here/'host.c',Path(__file__).resolve()]},'checks':rows}
(out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
print([(r['name'],r['compile_exit'],r['run_exit']) for r in rows])
assert rows[0]['compile_exit']==0 and rows[0]['run_exit']==0
assert all(r['compile_exit']==0 and r['run_exit'] not in (0,None) for r in rows[1:])
