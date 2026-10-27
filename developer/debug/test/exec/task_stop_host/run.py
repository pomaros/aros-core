#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Compile the actual stop-hook API bodies against a host CPU/list ABI."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
p.add_argument('--variant',choices=['up','smp'],default='smp')
p.add_argument('--sanitizer',choices=['address,undefined','thread'],default='address,undefined');a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[4]
def body(name):
    text=(root/'rom/task'/name).read_text()
    begin=text.index('{',text.index('AROS_LH'))
    return text[begin:].replace('AROS_LIBFUNC_INIT','').replace('AROS_LIBFUNC_EXIT','')
lock=(root/'rom/task/task_stop.h').read_text()
code=lock+'\nBOOL TaskStopHookControl(struct Hook *hook,BOOL add,struct TaskResBase *TaskResBase)\n'+body('TaskStopHookControl.c')+'\nvoid NotifyTaskStopped(struct Task *task,struct TaskResBase *TaskResBase)\n'+body('NotifyTaskStopped.c')
mutants={
 'positive':code,
 'unsupported-platform':code,
 'foreign-caller-admitted':code.replace('        FindTask(NULL) != PrivExecBase(SysBase)->ServicePort->mp_SigTask ||\n',''),
 'missing-service-port-admitted':code.replace('        !PrivExecBase(SysBase)->ServicePort ||\n',''),
 'old-exec-admitted':code.replace('if (add && (SysBase->LibNode.lib_Version < 51 ||','if (FALSE && (SysBase->LibNode.lib_Version < 51 ||'),
 'dispatch-lock-omitted':code.replace('    task_stop_lock(TaskResBase);\n    ForeachNode','    ForeachNode'),
 'dispatch-unlock-omitted':code.replace('        CALLHOOKPKT(hook, task, NULL);\n    task_stop_unlock(TaskResBase);','        CALLHOOKPKT(hook, task, NULL);'),
 'remove-without-unlink':code.replace('Remove((struct Node *)&hook->h_MinNode);','(void)hook;'),
 'removed-state-admitted':code.replace('task->tc_State != TS_INVALID ||',''),
 'current-task-admitted':code.replace('task == FindTask(NULL) ||','0 ||'),
 'disabled-control-admitted':code.replace('!hook || IDNESTCOUNT_GET >= 0 || TDNESTCOUNT_GET >= 0','!hook'),
 'forbidden-dispatch-admitted':code.replace('IDNESTCOUNT_GET >= 0 || TDNESTCOUNT_GET >= 0)','IDNESTCOUNT_GET >= 0)'),
 'duplicate-reported-success':code.replace('return add ? !found : found;','return TRUE;'),
}
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
for name,source in mutants.items():
    lane=out/name;lane.mkdir();(lane/'actual.c').write_text(source)
    command=['clang','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-pthread','-fsanitize='+a.sanitizer,'-g','-I'+str(lane),str(here/'host.c'),'-o',str(lane/'host')]
    if a.variant=='smp':command.append('-D__AROSEXEC_SMP__')
    if name=='unsupported-platform':command.append('-DTEST_NO_HANDOFF')
    build=subprocess.run(command,capture_output=True,text=True,timeout=30);(lane/'compile.log').write_text(build.stdout+build.stderr)
    exitcode=None
    if not build.returncode:
        try:
            run=subprocess.run([str(lane/'host')],capture_output=True,text=True,timeout=10);exitcode=run.returncode
            (lane/'run.log').write_text(run.stdout+run.stderr)
        except subprocess.TimeoutExpired as error:
            exitcode='timeout';(lane/'run.log').write_bytes((error.stdout or b'')+(error.stderr or b''))
    rows.append({'name':name,'command':command,'compile_exit':build.returncode,'run_exit':exitcode})
inputs=[root/'rom/task'/n for n in ['task_stop.h','TaskStopHookControl.c','NotifyTaskStopped.c']]+[here/'host.c',Path(__file__)]
result={'scope':'Actual API and lock helper with host list/IRQ/SMP adapters; not guest/quiescence/consumer cleanup proof','variant':a.variant,'inputs':{str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},'sanitizer':a.sanitizer,'checks':rows}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print([(r['name'],r['compile_exit'],r['run_exit']) for r in rows])
assert rows[0]['compile_exit']==0 and rows[0]['run_exit']==0
assert rows[1]['compile_exit']==0 and rows[1]['run_exit']==0
assert all(r['compile_exit']==0 and r['run_exit'] not in (None,0) for r in rows[2:])
