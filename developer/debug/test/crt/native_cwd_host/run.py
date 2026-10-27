#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Compile the production native-CWD synchronization and snapshot engines."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[4];source=root/'compiler/crt/posixc';inputs=[Path(__file__),here/'host.c']
def read(name):
 path=source/name;inputs.append(path);return re.sub(r'^#include[^\n]*\n','',path.read_text(),flags=re.M)
def block(text,name):
 start=text.index(name);opening=text.index('{',start);depth=0
 for end in range(opening,len(text)):
  depth+=(text[end]=='{')-(text[end]=='}')
  if not depth:return text[start:end+1]
 raise ValueError(name)
s=read('__cwd_runtime.c');parts={'CONTEXT_HEADER':read('__cwd_context.h'),'RUNTIME_HEADER':read('__cwd_runtime.h'),'CONTEXT_ENGINE':read('__cwd_context.c'),'BINDING_TYPE':block(s,'struct __cwd_binding {')+';','FUNCTIONS':'\n'.join(block(s,n) for n in ['static void binding_release(', 'static BOOL sync_native_cwd(', 'BOOL __cwd_runtime_snapshot(', 'BOOL __cwd_runtime_scope_enter(', 'void __cwd_runtime_scope_leave('])}
text=(here/'host.c').read_text()
for name,value in parts.items():text=text.replace('/* '+name+' */',value)
controls={'leaked-native-observation':('__cwd_directory_release(b->native_cwd);','(void)b->native_cwd;','all native observations and snapshots must be released'), 'ignore-native-change':('if (!b || !sync_native_cwd(b)) return FALSE;', 'if (!b) return FALSE;', 'native CurrentDir must select the new directory'), 'pointer-identity':('SameLock(current, __cwd_directory_lock(b->native_cwd)) == LOCK_SAME','current == __cwd_directory_lock(b->native_cwd)','unchanged native directory must preserve shared logical chdir'), 'drop-error':('if (!next) return FALSE;','if (!next) return TRUE;','failed native lock copy must refuse snapshot')}
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
for name,control in [('production',None),*controls.items()]:
 code=text;cause=None
 if control:
  old,new,cause=control
  if code.count(old)!=1:raise ValueError(name+' mutation site')
  code=code.replace(old,new)
 unit=out/(name+'.c');unit.write_text(code);binary=out/name
 cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-g','-fsanitize=address,undefined',str(unit),'-o',str(binary)]
 c=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(out/(name+'-build.log')).write_text(c.stdout+c.stderr);ok=False;exitcode=None
 if c.returncode==0:
  r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10);exitcode=r.returncode;(out/(name+'.log')).write_text(r.stdout+r.stderr);ok=(r.returncode==0 if cause is None else r.returncode!=0 and cause in r.stderr)
 rows.append(dict(name=name,compile_exit=c.returncode,run_exit=exitcode,expected_cause=cause,passed=ok))
result={'result':'PASS' if all(x['passed'] for x in rows) else 'FAIL','scope':'production context/sync/snapshot engines; modeled DOS locks and Task boundary, no guest claim','records':rows,'inputs':{str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs}}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result']);raise SystemExit(result['result']!='PASS')
