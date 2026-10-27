#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
from pathlib import Path
import argparse,hashlib,json,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--tsan',action='store_true');a=p.parse_args()
fixture=Path(__file__).resolve().parent;source=fixture.parents[4]/'rom/dos/runwithcontext.c'
body=re.sub(r'^#include[^\n]*\n','',source.read_text(),flags=re.M)
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);records=[]
for case in ['production','control-borrow-lock','control-drop-error','control-return-before-notify','control-late-lock-release']:
 actual=body
 if case=='control-borrow-lock':actual=actual.replace('copy = (BPTR)DoPktResult(port, action, args, error);','copy = lock; (void)args; (void)DOSBase; (void)DoPktResult; (void)action;')
 if case=='control-drop-error':actual=actual.replace('error = call->error;','error = 0;')
 if case=='control-return-before-notify':actual=actual.replace('Wait(call->signal);','(void)Wait; /* Missing actual notification wait. */')
 if case=='control-late-lock-release':actual=actual.replace('    UnLock(current);\n    UnLock(home);','    (void)current; (void)home;')
 test=(fixture/'test.c').read_text().replace('/* ACTUAL_SOURCE */',actual)
 path=out/(case+'.c');path.write_text(test);binary=out/case
 cmd=['clang','-std=c11','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-g','-fsanitize='+('thread' if a.tsan else 'address,undefined'),str(path),'-o',str(binary)]
 with (out/(case+'-build.log')).open('w') as log:q=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=30)
 records.append({'name':case+'-build','command':cmd,'exit':q.returncode,'pass':q.returncode==0})
 if not q.returncode:
  with (out/(case+'.log')).open('w') as log:q=subprocess.run([str(binary)],stdout=log,stderr=subprocess.STDOUT,timeout=30)
  records.append({'name':case,'exit':q.returncode,'pass':q.returncode==0 if case=='production' else q.returncode!=0})
result={'result':'PASS' if all(r['pass'] for r in records) else 'FAIL','records':records,'inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source,Path(__file__),fixture/'test.c']},'scope':'Actual gate source with pthread-backed modeled Exec/DOS Process creation and packet boundary. No actual guest or forced task-removal qualification.'}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result']);raise SystemExit(result['result']!='PASS')
