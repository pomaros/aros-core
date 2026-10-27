#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Actual ServiceTask with bounded queue and allocation ownership shims."""
import argparse, hashlib, json, os, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[4]
source=root/'rom/exec/service.c';text=source.read_text();body=text[text.index('void ServiceTask('):]
variants={'supported':body,'unsupported':body,
 'omit-notification':body.replace('NotifyTaskStopped(task);','(void)TaskResBase;'),
 'notify-fallthrough':body.replace('state == TS_INVALID && ','')}
a.output.mkdir(parents=True,exist_ok=False);rows=[]
for name,code in variants.items():
 lane=a.output/name;lane.mkdir();(lane/'service_actual.c').write_text(code)
 cmd=['clang','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-D__AROSEXEC_SMP__','-fsanitize=address,undefined','-I'+str(lane),str(here/'service.c'),'-o',str(lane/'host')]
 if name=='unsupported':cmd+=['-DTEST_NO_HANDOFF']
 c=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(lane/'compile.log').write_text(c.stdout+c.stderr);assert c.returncode==0,c.stderr
 r=subprocess.run([str(lane/'host')],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'});(lane/'run.log').write_text(r.stdout+r.stderr)
 assert (r.returncode==0)==(name in ('supported','unsupported')),(name,r.stderr)
 rows.append(dict(name=name,returncode=r.returncode))
(a.output/'result.json').write_text(json.dumps(dict(scope='HOST actual ServiceTask; modeled queue and allocator, no hardware claim',cases=rows,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest()),indent=2)+'\n')
print('PASS actual ServiceTask: supported/unsupported and two rejected mutants')
