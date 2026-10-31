#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual continuation helpers and SVC cases; modeled MMU/service, HOST only."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
def function(text,name):
 start=text.rfind('static ',0,text.index(name));end=text.index('{',text.index(name))+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
 if out.exists() or ROOT in out.parents:p.error('fresh output outside source required')
 source=ROOT/'arch/aarch64-apple/kernel/restricted.c';text=source.read_text()
 actual=''.join(function(text,name) for name in ['r_code_pc(const','r_capture_code(const','r_match_code(const'])
 actual+='static int dispatch(struct Restricted *r,struct Frame *frame,unsigned svc) { void *execution=NULL;switch(svc) {\n'
 for name in ['SUBMIT','COLLECT','CALLBACK_RETURN','FAULT_HANDLER','FAULT_RESUME']:
  start=text.index('        case AROS_RESTRICTED_'+name+':');end=text.index('        case ',start+8)
  actual+=text[start:end]
 actual+='default:return 0;} }\n'
 variants=[('actual',actual,None)]
 for name,old,new,expected in [
  ('reserved-extra-commit','generation!=r->fault_page_generation+1','0','reserved_fault'),
  ('stale-generation','current.generation==identity->generation','1','identity'),
  ('stale-reservation','current.reservation==identity->reservation','1','identity'),
  ('wrong-backing','current.physical==identity->physical','1','identity'),
  ('unaligned-pc',' || (pc&3)','', 'identity'),
  ('stale-callback-entry','!r_match_code(r,&r->callback_entry)','0','callback'),
  ('stale-callback-return','!r_match_code(r,&r->callback_return)','0','callback'),
  ('stale-fault-code','!r_match_code(r,&r->fault_code)','0','fault'),
  ('overwrite-pending-entry','if (AROSRestrictedServiceSubmit','r->callback_entry=entry;\n            if (AROSRestrictedServiceSubmit','callback')]:
  assert actual.count(old)==1,(name,actual.count(old));variants.append((name,actual.replace(old,new),expected))
 out.mkdir(parents=True);inputs=[source,HERE/'test.c',Path(__file__),ROOT/'arch/aarch64-apple/include/aros/apple/restricted.h']
 hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
 result={'result':'FAIL','source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),'scope':__doc__,'inputs':hashes,'records':[]}
 try:
  for name,body,expected in variants:
   dest=out/name;dest.mkdir();(dest/'actual.inc').write_text(body);binary=dest/'test'
   cmd=['clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-I'+str(dest),'-I'+str(ROOT/'arch/aarch64-apple/include'),str(HERE/'test.c'),'-o',str(binary)]
   build=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(dest/'compile.log').write_text(build.stdout+build.stderr)
   if build.returncode:raise RuntimeError(name+' compile')
   env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
   run=subprocess.run([str(binary)],capture_output=True,text=True,env=env,timeout=10);(dest/'run.log').write_text(run.stdout+run.stderr)
   result['records'].append({'name':name,'exit':run.returncode})
   if expected:
    assertions={
     'reserved-extra-commit':'frame.x[0]==UINT64_MAX && slot.fault_active && !slot.result.resumed',
     'stale-generation':'!r_match_code(&slot,&id)',
     'stale-reservation':'!r_match_code(&slot,&id)',
     'wrong-backing':'!r_match_code(&slot,&id)',
     'unaligned-pc':'!r_capture_code(&slot,dynamic+1,&id)',
     'stale-callback-entry':'slot.result.status==-4 && !slot.callback_active',
     'stale-callback-return':'slot.result.status==-4',
     'stale-fault-code':'frame.x[0]==UINT64_MAX && slot.fault_active && !slot.result.resumed',
     'overwrite-pending-entry':'!frame.x[0] && !memcmp(&id,&slot.callback_entry,sizeof(id))'}
    prefix='Assertion failed: ('+assertions[name]+'), function '+expected+', file '
    if run.returncode!=-6 or len(run.stderr.splitlines())!=1 or not run.stderr.startswith(prefix):raise RuntimeError(name+' did not reject intended control')
   elif run.returncode or run.stderr or 'PASS continuation' not in run.stdout:raise RuntimeError('baseline failed')
  if hashes!={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}:raise RuntimeError('inputs changed')
  result['result']='HOST_PASS'
 except Exception as e:result['error']=str(e)
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['result','error'] if k in result}));return result['result']!='HOST_PASS'
if __name__=='__main__':raise SystemExit(main())
