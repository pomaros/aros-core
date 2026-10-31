#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Extract actual dispatch restore/close validation; modeled task locks, HOST only."""
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
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
 if out.exists() or ROOT in out.parents:p.error('fresh output outside source required')
 source=ROOT/'arch/aarch64-apple/kernel/restricted.c';cpu=ROOT/'arch/aarch64-apple/kernel/kernel_cpu.c'
 text=source.read_text();c=cpu.read_text();c=c[c.index('void cpu_Dispatch('):]
 actual=''.join(function(text,name) for name in ['static void r_clear_deadline(', 'static void r_clear_wait(', 'static void r_deliver_wait(', 'static void r_finish(', 'void AROSAppleRestrictedCheckStack('])
 # Replace the unreachable target corruption halt, not any tested policy.
 assert actual.count('for (;;) __asm__ volatile("wfe");')==1
 actual=actual.replace('for (;;) __asm__ volatile("wfe");','abort();')
 start=c.index('    for (index = 0; index < 29; ++index)');end=c.index('#if !defined(__AROSEXEC_SMP__)',start)
 actual+='static void restore(struct AROSAppleRuntimeFrame *regs,struct Task *task,struct ExceptionContext *context) { unsigned index;\n'+c[start:end]
 start=c.index('    AROSAppleTaskTLSRestore(context);',end);end=c.index('#if defined(__AROSEXEC_SMP__)',start)
 actual+=c[start:end]+'}\n'
 variants=[('actual',actual,None)]
 for name,old,new,assertion in [
  ('missing-close-check','    AROSAppleRestrictedCheckStack(regs, task);','', 'slot.result.status==-5 && execution.root==0'),
  ('stale-interrupt-masks','    cpu_ApplyTaskMasks(&regs->spsr, task->tc_IDNestCnt);','', '!(f.spsr&0xc0) && AROSAppleKernelLastDispatchSPSR==f.spsr'),
  ('stale-stack-pointer','(APTR)(uintptr_t)regs->sp','(APTR)(uintptr_t)context->sp','f.elr==slot.native.elr && f.sp==slot.native.sp && task.tc_SPReg==(void *)(uintptr_t)slot.native.sp'),
  ('closed-root-retained','    *execution = (struct AROSAppleTaskExecution){0};','', 'slot.result.status==-5 && execution.root==0')]:
  assert actual.count(old)==1,(name,actual.count(old));variants.append((name,actual.replace(old,new),assertion))
 out.mkdir(parents=True);inputs=[source,cpu,HERE/'test.c',Path(__file__)]
 hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
 result={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),'inputs':hashes,'records':[]}
 try:
  for name,body,assertion in variants:
   dest=out/name;dest.mkdir();(dest/'actual.inc').write_text(body);binary=dest/'test'
   cmd=['clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-I'+str(dest),str(HERE/'test.c'),'-o',str(binary)]
   build=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(dest/'compile.log').write_text(build.stdout+build.stderr)
   if build.returncode:raise RuntimeError(name+' compile')
   run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
   (dest/'run.log').write_text(run.stdout+run.stderr);result['records'].append({'name':name,'exit':run.returncode})
   if assertion:
    prefix='Assertion failed: ('+assertion+'), function cancelled, file '
    if run.returncode!=-6 or len(run.stderr.splitlines())!=1 or not run.stderr.startswith(prefix):raise RuntimeError(name+' did not fail intended assertion')
   elif run.returncode or run.stderr or 'PASS dispatch' not in run.stdout:raise RuntimeError('baseline failed')
  if hashes!={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}:raise RuntimeError('inputs changed')
  result['result']='HOST_PASS'
 except Exception as e:result['error']=str(e)
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['result','error'] if k in result}));return result['result']!='HOST_PASS'
if __name__=='__main__':raise SystemExit(main())
