#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Actual POSIXC namespace client, with Exec/library/DOS startup boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',required=True,type=Path)
parser.add_argument('--tsan',action='store_true')
args=parser.parse_args()
here=Path(__file__).resolve().parent
root=here.parents[4]
posixc=root/'compiler/crt/posixc'
output=args.output.resolve()
output.mkdir(parents=True,exist_ok=False)
inputs=[]
def read(name):
    path=posixc/name;inputs.append(path);return path.read_text()
def block(text,signature):
    start=text.index(signature);begin=text.index('{',start);depth=0
    for end in range(begin,len(text)):
        depth+=(text[end]=='{')-(text[end]=='}')
        if not depth:return text[start:end+1]
    raise ValueError(signature)
header=read('__fdesc.h');source=read('__fdesc.c');locks=read('__reclock.c')
parts={
 'FCB_TYPES':block(header,'typedef struct _fcb\n')+' fcb;',
 'FDESC_TYPES':block(header,'typedef struct _fdesc\n')+' fdesc;',
 'RECLOCK_TYPES':block(locks,'struct reclock_file\n')+';\n'+block(locks,'struct reclock\n')+';',
 'ROOT_INIT':block(locks,'static int reclock_init('),
 'SPACE':block(source,'static struct __fd_namespace *__fd_space('),
 'CURRENT_SPACE':block(source,'struct __fd_namespace *__fd_namespace_current('),
 'REGISTRY':block(source,'static int __fdlib_available('),
 'TABLE_LOCK':block(source,'void __fdesc_lock('),
 'TABLE_UNLOCK':block(source,'void __fdesc_unlock('),
 'OWNER':block(locks,'static struct __fd_namespace *owner_namespace('),
 'OWNER_PID':block(locks,'static pid_t owner_pid('),
 'RELEASE_OWNER':block(locks,'void __posixc_reclock_release_namespace('),
 'ALLOC_DESC':block(source,'fdesc *__alloc_fdesc('),
 'FREE_DESC':block(source,'void __free_fdesc('),
 'DISPOSE':block(source,'static void dispose_descriptor('),
 'INIT_FD':block(source,'int __init_fd('),
 'EXIT_FD':block(source,'void __exit_fd('),
 'OPTIONAL_CLOSE':block(read('__optionallibs.c'),'int __optionallibs_close('),
 'POOL_CLOSE':block(read('__posixc_init.c'),'static void __posixc_close('),
}
text=(here/'runtime.c').read_text()
for marker,code in parts.items():
    placeholder='/* '+marker+' */';assert text.count(placeholder)==1,marker
    text=text.replace(placeholder,code)
engine=read('__fd_namespace.c');inputs.append(posixc/'__fd_namespace.h')
shim=output/'shim';shim.mkdir()
(shim/'test_boundary.h').write_text('''#ifndef TEST_BOUNDARY_H
#define TEST_BOUNDARY_H
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
typedef uint32_t ULONG;
typedef int BOOL;
struct SignalSemaphore { pthread_mutex_t mutex; };
struct Library;
void *AllocVec(size_t,unsigned);
void FreeVec(void *);
void InitSemaphore(struct SignalSemaphore *);
void CloseLibrary(struct Library *);
#endif
''')
for name in ['exec/types.h','exec/semaphores.h','exec/memory.h','proto/exec.h']:
    path=shim/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#include "test_boundary.h"\n')
controls={
 'unretained-client':('if (!__fd_namespace_retain(space))','if (FALSE)',
   'Assertion failed: (atomic_load(&closed_registry)==0 && atomic_load(&active_records)==1 && atomic_load(&pools_freed)==1)'),
 'borrowed-creator':('if ((base->flags & PRETEND_CHILD) && base->fd_owner)','if (base->fd_owner)',
   'AddressSanitizer: heap-use-after-free'),
 'early-registry-close':('    /* The descriptor namespace releases fd.library at final owner teardown. */',
   '    CloseLibrary(__fd_space(PosixCBase)->registry);',
   'Assertion failed: (atomic_load(&active_records)==0 && atomic_load(&closed_blocks)==3)'),
 'lost-owner-finalization':('                           __posixc_reclock_release_namespace);','                           NULL);',
   'Assertion failed: (atomic_load(&active_records)==0 && atomic_load(&closed_blocks)==3)'),
 'lost-process-identity':('return owner ? (pid_t)owner->process_id : 0;',
   '(void)owner; return 0;',
   'Assertion failed: (space && space->process_id==42 && owner_pid(owner_namespace())==42)'),
}
cases=[('production',text,None)]
for name,(before,after,expected) in controls.items():
    if args.tsan and name=='borrowed-creator':continue
    assert before in text,name
    cases.append((name,text.replace(before,after),expected))
rows=[]
for name,actual,expected in cases:
    lane=output/name;lane.mkdir();engine_path=lane/'namespace.c';engine_path.write_text(engine)
    unit=lane/'runtime.c';unit.write_text(actual.replace('NAMESPACE_SOURCE',str(engine_path)))
    binary=lane/'test';command=['clang','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function',
      '-pthread','-g','-fsanitize='+('thread' if args.tsan else 'address,undefined'),
      '-I'+str(shim),'-iquote',str(posixc),str(unit),'-o',str(binary)]
    with(lane/'compile.log').open('w') as log:p=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=60)
    row={'name':name,'command':command,'compile_exit':p.returncode,'run_exit':None,'expected_cause':expected,'pass':False}
    if not p.returncode:
        with(lane/'run.log').open('w') as log:
            try:p=subprocess.run([str(binary)],stdout=log,stderr=subprocess.STDOUT,timeout=20);row['run_exit']=p.returncode
            except subprocess.TimeoutExpired:row['run_exit']=124
        row['pass']=(row['run_exit'] not in (0,124) and expected in (lane/'run.log').read_text()) if expected else row['run_exit']==0
    rows.append(row)
r={'result':'PASS' if all(x['pass'] for x in rows) else 'FAIL','records':rows,
 'inputs':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*inputs,here/'runtime.c',Path(__file__)]},
 'scope':'Actual namespace engine and POSIXC attachment/exit, allocation/free, owner identity/cleanup, module lock initialization and optional/private-pool close functions. Modeled parent lookup, Task identity, library calls, stdio setup, file-block disposal and native record-lock removal. Eight workers after creator base/Task/pool free; no capture-before-create admission, full I/O/inheritance, loader, cwd or guest claim.'}
(output/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(r['result'])
for row in rows:
    if not row['pass']:print(row['name'],row['compile_exit'],row['run_exit'])
raise SystemExit(r['result']!='PASS')
