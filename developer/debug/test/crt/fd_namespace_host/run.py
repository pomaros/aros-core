#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
from pathlib import Path
import argparse,subprocess,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--tsan',action='store_true');a=p.parse_args();f=Path(__file__).resolve().parent;r=f.parents[4];e=r/'compiler/crt/posixc/__fd_namespace.c';o=a.output.resolve();o.mkdir(parents=True,exist_ok=False);shim=o/'shim';shim.mkdir()
b='''#ifndef TEST_BOUNDARY_H
#define TEST_BOUNDARY_H
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
typedef uint32_t ULONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
struct SignalSemaphore { pthread_mutex_t mutex; };
struct Library;
void *AllocVec(size_t,unsigned);
void FreeVec(void *);
void InitSemaphore(struct SignalSemaphore *);
void CloseLibrary(struct Library *);
#endif
'''
(shim/'test_boundary.h').write_text(b)
for n in ['exec/types.h','exec/semaphores.h','exec/memory.h','proto/exec.h']:
 t=shim/n;t.parent.mkdir(parents=True,exist_ok=True);t.write_text('#include "test_boundary.h"\n')
records=[]
for case in ['production','control-unretained','control-early-close','control-no-dispose']:
 text=e.read_text()
 if case=='control-unretained':text=text.replace('old + 1,','old,')
 if case=='control-early-close':text=text.replace('if (!space || __atomic_sub_fetch(&space->references, 1, __ATOMIC_ACQ_REL)) return;', 'if (!space) return; if (space->registry) { CloseLibrary(space->registry); space->registry = NULL; } if (__atomic_sub_fetch(&space->references, 1, __ATOMIC_ACQ_REL)) return;')
 if case=='control-no-dispose':text=text.replace('dispose(space->array[i], space->registry, i);','(void)dispose;')
 actual=o/(case+'-source.c');actual.write_text(text);wrapper=o/(case+'-test.c');wrapper.write_text((f/'test.c').read_text().replace('NAMESPACE_SOURCE',str(actual)));binary=o/case
 cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-pthread','-fsanitize='+('thread' if a.tsan else 'address,undefined'),'-I'+str(shim),'-iquote',str(e.parent),str(wrapper),'-o',str(binary)]
 with(o/(case+'-build.log')).open('w') as log:q=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT, timeout=30)
 records.append({'name':case+'-build','exit':q.returncode,'pass':q.returncode==0,'command':cmd})
 if q.returncode==0:
  with(o/(case+'.log')).open('w') as log:q=subprocess.run([str(binary)],stdout=log,stderr=subprocess.STDOUT,timeout=30)
  records.append({'name':case,'exit':q.returncode,'pass':q.returncode==0 if case=='production' else q.returncode!=0})
inputs=[e,e.with_suffix('.h'),Path(__file__),f/'test.c'];result={'result':'PASS' if all(c['pass'] for c in records) else 'FAIL','records':records,'inputs':{str(t):hashlib.sha256(t.read_bytes()).hexdigest() for t in inputs},'scope':'Actual heap namespace engine; modeled descriptor disposal, registry and Exec allocation. No POSIX admission/client/guest claim.'};(o/'result.json').write_text(json.dumps(result,indent=2));print(result['result']);raise SystemExit(result['result']!='PASS')
