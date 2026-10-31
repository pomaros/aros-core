#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual scheduler park/wake and hooks with modeled locks/lists; HOST only."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
def function(text,signature):
 start=text.index(signature);end=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]+'\n'
def block(text):
 lines=text.splitlines(True);depth=0;out=''
 for l in lines:
  if l.startswith('#if'):depth+=1
  if l.startswith('#endif'):depth-=1
  out+=l
  if not depth:return out
 raise ValueError('unterminated block')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
 if out.exists() or ROOT in out.parents:p.error('fresh output outside source required')
 source=ROOT/'arch/aarch64-apple/kernel/kernel_scheduler.c';generic=ROOT/'arch/aarch64-native/kernel/kernel_scheduler.c';restricted=ROOT/'arch/aarch64-apple/kernel/restricted.c'
 s=source.read_text();g=generic.read_text()
 actual=''.join(function(s,name) for name in ['static struct ExceptionContext *restricted_context(', 'static int restricted_park(struct Task *task)', 'int AROSAppleRestrictedSetWait('])
 actual+='static void outgoing(struct Task *task) {\n'+block(g[g.index('#ifdef CORE_SCHEDULER_PARK\n        if (task->'):])+'}\n'
 start=g.index('#ifdef CORE_SCHEDULER_PARK\n        /* Native signals')
 actual+='static int incoming(struct Task *newtask) { int taskStateLocked=1;\n'+block(g[start:])+'return 1;\ndispatch_select:return 0; }\n'
 r=restricted.read_text()
 actual+=function(r,'static void r_cancel_waiters(struct RestrictedDomain *domain)')+function(r,'static int r_wake_channel(')
 actual+='static int payload(struct Restricted *r,struct Frame *frame,struct Task *task,struct ExceptionContext *context,unsigned svc) { switch(svc) {\n'
 start=r.index('        case AROS_RESTRICTED_THREAD_INFO:');end=r.index('        case AROS_RESTRICTED_YIELD:',start)
 actual+=r[start:end]+'default:return 0;} }\n'
 variants=[('actual',actual,None,None)]
 controls=[
 ('lost-wake-after-thaw','GetIntETask(task)->iet_FrozenState=TS_READY;','(void)0;', 'iet.iet_FrozenState==TS_READY','ownership'),
 ('stale-wake-generation','frame->x[0]!=r->wake_generation ||','', 'payload(&slot,&frame,&task,&context,AROS_RESTRICTED_WAIT)==1 && !frame.x[0] && !execution.park_flags','generations'),
 ('generation-overflow',' || r->wake_generation==UINT64_MAX) continue;', ') continue;', '!r_wake_channel(&domain,17) && slot.wake_generation==UINT64_MAX','generations'),
 ('foreign-domain','r->domain!=domain ||','', '!r_wake_channel(&other,17) && !r_wake_channel(&domain,18)','generations'),
 ('wake-before-save','execution->park_flags=0;','execution->park_flags=AROS_TASK_PARK_WAIT;', 'task.tc_State==TS_READY && task.tc_Node.list==2 && !execution.park_flags','interleavings'),
 ('missing-park-ack','execution->park_flags|=AROS_TASK_PARKED;','', 'task.tc_State==TS_READY && task.tc_Node.list==2 && !execution.park_flags && ipis==1','interleavings'),
 ('native-signal-bypass','if (newtask->tc_State==TS_READY && CORE_SCHEDULER_PARK(newtask))','if (0)', '!incoming(&task)','native_signal'),
 ('foreign-context','restricted_context(task)!=context ||','', '!AROSAppleRestrictedSetWait(&task,&foreign,1) && !execution.park_flags','ownership')]
 for name,old,new,assertion,fn in controls:
  assert actual.count(old)==1,(name,actual.count(old));variants.append((name,actual.replace(old,new),assertion,fn))
 out.mkdir(parents=True);inputs=[source,generic,restricted,HERE/'test.c',Path(__file__),ROOT/'arch/aarch64-apple/kernel/task_tls.h',ROOT/'arch/aarch64-apple/include/aros/apple/restricted.h']
 hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
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
   elif run.returncode or run.stderr or 'PASS wait' not in run.stdout:raise RuntimeError('baseline failed')
  if hashes!={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}:raise RuntimeError('inputs changed')
  result['result']='HOST_PASS'
 except Exception as e:result['error']=str(e)
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['result','error'] if k in result}));return result['result']!='HOST_PASS'
if __name__=='__main__':raise SystemExit(main())
