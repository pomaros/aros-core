#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
from pathlib import Path
import argparse,re,subprocess,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--tsan',action='store_true');a=p.parse_args();fixture=Path(__file__).resolve().parent;source=fixture.parents[4];o=a.output.resolve();o.mkdir(parents=True,exist_ok=False)
shim=o/'shim/exec';shim.mkdir(parents=True);(shim/'types.h').write_text('/* Types supplied by fixture before public header. */\n')
paths={n:source/'rom/dos'/n for n in ['taskpathresolver.c','taskpathresolver.h','ioerr.c','setioerr.c','open.c']};paths['public']=source/'compiler/include/dos/context.h'
strip=lambda x:re.sub(r'^#include[^\n]*\n','',x,flags=re.M)
text=(fixture/'test.c').read_text()
for marker,key in [('PUBLIC_HEADER','public'),('PRIVATE_HEADER','taskpathresolver.h'),('REGISTRY_SOURCE','taskpathresolver.c'),('IOERR_SOURCE','ioerr.c'),('SETIOERR_SOURCE','setioerr.c')]:text=text.replace('/* '+marker+' */',strip(paths[key].read_text()))
opening=paths['open.c'].read_text();start=opening.index('DOS_PATH_DISPATCH(');end=opening.index(';',start)+1
text=text.replace('/* OPEN_DISPATCH */',opening[start:end]);records=[]
for case in ['production','control-drop-dispatch','control-drop-error','control-parent-borrow','control-drop-policy','control-task-fallback','control-unheld-policy']:
 actual=text
 if case=='control-drop-dispatch':actual=actual.replace(opening[start:end],'(void)__dos_path_operation;')
 if case=='control-drop-error':actual=actual.replace('SetIoErr(error);','SetIoErr(0); (void)error;')
 if case=='control-unheld-policy':actual=actual.replace('resolver->active = TRUE;\n    if (!resolver->accept', 'if (!resolver->accept').replace('result = resolver->invoke', 'resolver->active = TRUE;\n    result = resolver->invoke')
 if case=='control-drop-policy':actual=actual.replace('if (!resolver->accept(resolver->data)) {','if (FALSE) {')
 if case=='control-task-fallback':actual=actual.replace('if (FindTask(NULL)->tc_Node.ln_Type == NT_PROCESS) {','if (TRUE) {')
 if case=='control-parent-borrow':actual=actual.replace('return (struct DosPathResolver *)GetTaskStorageSlot(slot);','return (struct DosPathResolver *)GetParentTaskStorageSlot(slot);')
 path=o/(case+'.c');path.write_text(actual);binary=o/case;cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-g','-pthread','-fsanitize='+('thread' if a.tsan else 'address,undefined'),'-I'+str(o/'shim'),str(path),'-o',str(binary)]
 with(o/(case+'-build.log')).open('w') as f:q=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=30)
 records.append({'name':case+'-build','command':cmd,'exit':q.returncode,'pass':q.returncode==0})
 if not q.returncode:
  with(o/(case+'.log')).open('w') as f:q=subprocess.run([str(binary)],stdout=f,stderr=subprocess.STDOUT,timeout=30)
  records.append({'name':case,'exit':q.returncode,'pass':q.returncode==0 if case=='production' else q.returncode!=0})
result={'result':'PASS' if all(c['pass'] for c in records) else 'FAIL','records':records,'inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*paths.values(),fixture/'test.c',Path(__file__)]},'scope':'Actual registry/IoErr/SetIoErr and Open dispatch prefix; native operation, task resource storage and allocation are modeled. Concurrent per-Task registrations and ordinary-Task boundary only; no whole POSIX runtime/guest claim.'};(o/'result.json').write_text(json.dumps(result,indent=2));print(result['result']);raise SystemExit(result['result']!='PASS')
