#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual outer trap admission; inner service/scheduler stubs, HOST only."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
def function(text,signature):
 start=text.index(signature);end=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline-only',action='store_true');a=p.parse_args();out=a.out.resolve()
 if out.exists() or ROOT in out.parents:p.error('fresh output outside source required')
 source=ROOT/'arch/aarch64-apple/kernel/restricted.c';s=source.read_text()
 actual='\n'.join(l for l in s.splitlines() if l.startswith('#define R_'))+'\n'+function(s,'int AROSAppleRestrictedTrap(')
 variants=[('actual',actual,None,None)]
 if not a.baseline_only:
  for name,old,new,assertion,fn in [
   ('old-native-range','svc>R_RESUME','svc>R_TABLE_GROW','AROSAppleRestrictedTrap(&frame)==1 && calls==1 && !locked','native'),
   ('unrelated-native-service','svc>R_RESUME','svc>0xffff','!AROSAppleRestrictedTrap(&frame) && !calls','native'),
   ('privileged-origin',' || (frame->spsr&31)','', '!AROSAppleRestrictedTrap(&frame) && !calls','boundaries')]:
   assert actual.count(old)==1,(name,actual.count(old));variants.append((name,actual.replace(old,new),assertion,fn))
 out.mkdir(parents=True);inputs=[source,HERE/'test.c',Path(__file__),ROOT/'arch/aarch64-apple/include/aros/apple/restricted.h'];hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
 result={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),'inputs':hashes,'records':[]}
 try:
  for name,body,assertion,fn in variants:
   dest=out/name;dest.mkdir();(dest/'actual.inc').write_text(body);binary=dest/'test'
   cmd=['clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-I'+str(dest),'-I'+str(ROOT/'arch/aarch64-apple/include'),str(HERE/'test.c'),'-o',str(binary)]
   build=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(dest/'compile.log').write_text(build.stdout+build.stderr)
   if build.returncode:raise RuntimeError(name+' compile')
   run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
   (dest/'run.log').write_text(run.stdout+run.stderr);result['records'].append({'name':name,'exit':run.returncode})
   if assertion:
    prefix='Assertion failed: ('+assertion+'), function '+fn+', file '
    if run.returncode!=-6 or len(run.stderr.splitlines())!=1 or not run.stderr.startswith(prefix):raise RuntimeError(name+' did not fail intended assertion')
   elif run.returncode or run.stderr or 'PASS native admission' not in run.stdout:raise RuntimeError('baseline failed')
  if hashes!={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}:raise RuntimeError('inputs changed')
  result['result']='HOST_PASS'
 except Exception as e:result['error']=str(e)
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['result','error'] if k in result}));return result['result']!='HOST_PASS'
if __name__=='__main__':raise SystemExit(main())
