#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual exception delivery, registration and continuation admission; modeled fault stack, HOST only."""
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
 actual=''.join(function(text,name) for name in ['r_code_pc(const','r_capture_code(const','r_match_code(const','r_deliverable(unsigned','r_deliver_exception(struct'])
 actual+='static int dispatch(struct Restricted *r,struct AROSAppleRuntimeFrame *frame,unsigned svc) { void *execution=NULL;\n'
 start=text.index('        if (r->exception_active && svc!=');end=text.index('        switch (svc) {',start)
 actual+=text[start:end]+'switch(svc) {\n'
 for name in ['RESTORE_CONTEXT','EXCEPTION_HANDLER','FAULT_HANDLER']:
  start=text.index('        case AROS_RESTRICTED_'+name+':');end=text.index('        case ',start+8)
  actual+=text[start:end]
 actual+='case AROS_RESTRICTED_EXIT:return 7;default:return 0;} }\n'
 variants=[('actual',actual,None)]
 for name,old,new,expected in [
  ('deliver-while-active','!r->exception_handler || r->exception_active ||','!r->exception_handler ||','delivered'),
  ('deliver-during-repair','        r->fault_active || r->callback_active ||','        r->callback_active ||','refusals'),
  ('deliver-stale-handler','!r_match_code(r,&r->exception_code)) return 0;','0) return 0;','refusals'),
  ('deliver-any-class','ec==0x26 || ec==0x2c || ec==0x3c;','ec==0x26 || ec==0x2c || ec==0x3c || ec==0x18;','refusals'),
  ('deliver-small-stack','r->fault_bytes<sizeof(*record)+1024 ||','','refusals'),
  ('register-during-repair','            if (r->fault_active || (frame->x[0] && !r_capture_code(r,frame->x[0],&r->exception_code))) {','            if (frame->x[0] && !r_capture_code(r,frame->x[0],&r->exception_code)) {','registration'),
  ('restore-keeps-active','                r->exception_active=0;r_stack_bounds(r,0);','                r_stack_bounds(r,0);','continuation'),
  ('deny-nothing-while-active','        if (r->exception_active && svc!=AROS_RESTRICTED_RESTORE_CONTEXT && svc!=AROS_RESTRICTED_EXIT) {','        if (0) {','registration')]:
  assert actual.count(old)==1,(name,actual.count(old));variants.append((name,actual.replace(old,new),expected))
 out.mkdir(parents=True);inputs=[source,HERE/'test.c',Path(__file__),ROOT/'arch/aarch64-apple/include/aros/apple/restricted.h']
 hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
 result={'result':'FAIL','source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),'scope':__doc__,'inputs':hashes,'records':[]}
 assertions={
  'deliver-while-active':'!r_deliver_exception(&slot,&frame)',
  'deliver-during-repair':'!r_deliver_exception(&slot,&frame)',
  'deliver-stale-handler':'!r_deliver_exception(&slot,&frame)',
  'deliver-any-class':'!r_deliver_exception(&slot,&frame)',
  'deliver-small-stack':'!r_deliver_exception(&slot,&frame)',
  'register-during-repair':'!frame.x[0] && slot.exception_handler==dynamic+8',
  'restore-keeps-active':'!slot.exception_active && slot.bounds_fault==0 && slot.bounds_calls==2 && slot.result.rejected==1',
  'deny-nothing-while-active':'frame.x[0]==UINT64_MAX && slot.result.denied==1 && slot.exception_handler==dynamic+8'}
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
    prefix='Assertion failed: ('+assertions[name]+'), function '+expected+', file '
    if run.returncode!=-6 or len(run.stderr.splitlines())!=1 or not run.stderr.startswith(prefix):raise RuntimeError(name+' did not reject intended control')
   elif run.returncode or run.stderr or 'PASS exception delivery' not in run.stdout:raise RuntimeError('baseline failed')
  if hashes!={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}:raise RuntimeError('inputs changed')
  result['result']='HOST_PASS'
 except Exception as e:result['error']=str(e)
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['result','error'] if k in result}));return result['result']!='HOST_PASS'
if __name__=='__main__':raise SystemExit(main())
