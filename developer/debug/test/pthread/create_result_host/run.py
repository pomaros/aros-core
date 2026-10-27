#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
import argparse,hashlib,json,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[4];source=root/'compiler/pthread/pthread_create.c';text=source.read_text();body=text[text.index('{\n    ThreadInfo *inf;\n    struct Task *created;'):]
code='int pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*start)(void *),void *arg)\n'+body
variants={'positive':code,'reread-retired-record':code.replace('    *thread = threadnew;','    if (!inf->task) return EAGAIN;\n    *thread = threadnew;'),'clear-after-release':code.replace('        memset(inf, 0, sizeof(*inf));\n        ReleaseSemaphore(&thread_sem);','        ReleaseSemaphore(&thread_sem);\n        inf->parent = NULL;')}
a.output.mkdir(parents=True,exist_ok=False);rows=[]
with tempfile.TemporaryDirectory(prefix='pthread-create-') as tmp:
 for name,c in variants.items():
  d=Path(tmp)/name;d.mkdir();(d/'actual.c').write_text(c)
  cmd=['clang','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-I'+str(d),str(here/'host.c'),'-o',str(d/'host')]
  b=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(a.output/(name+'-compile.log')).write_text(b.stdout+b.stderr);assert b.returncode==0,b.stderr
  r=subprocess.run([str(d/'host')],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'});(a.output/(name+'.log')).write_text(r.stdout+r.stderr);assert (r.returncode==0)==(name=='positive'),(name,r.stderr)
  rows.append(dict(name=name,exit=r.returncode))
(a.output/'result.json').write_text(json.dumps(dict(scope='HOST actual pthread_create with deterministic scheduler-unlock/allocator facades; no guest/CRT-admission claim',cases=rows,sha256=hashlib.sha256(source.read_bytes()).hexdigest()),indent=2)+'\n')
print('PASS actual pthread_create and two rejected runtime mutations')
