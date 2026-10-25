#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,hashlib,json,pathlib,subprocess
here=pathlib.Path(__file__).resolve().parent;root=here.parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args();out=pathlib.Path(a.output);out.mkdir(parents=True,exist_ok=False)
header=(root/'arch/aarch64-apple/kernel/task_accounting.h').read_text();(out/'test.c').write_bytes((here/'test.c').read_bytes());results={}
for name in ['production','keep_idle_active','omit_live_slice','wall_clock_window']:
 code=header
 if name=='keep_idle_active':code=code.replace('et->iet_private1=0;','/* retained idle start */')
 if name=='omit_live_slice':code=code.replace('if(active&&et->iet_private1)','if(active&&0&&et->iet_private1)')
 if name=='wall_clock_window':code=code.replace('uint64_t busy=et->iet_private2;','awake=now;uint64_t busy=et->iet_private2;')
 d=out/name;d.mkdir();(d/'task_accounting.h').write_text(code)
 cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-I'+str(d),str(out/'test.c'),'-o',str(d/'test')]
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(d/'build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 r=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=20);(d/'run.log').write_text(r.stdout+r.stderr)
 if name=='production':assert r.returncode==0 and r.stdout=='apple-task-cpu-host PASS\n' and not r.stderr,r
 else:assert r.returncode!=0 and 'Assertion failed:' in r.stderr,r
 results[name]={'exit':r.returncode,'command':cmd}
query=(root/'arch/aarch64-apple/task/QueryTaskTagList.c').read_text()
a=query.index('static struct IntETask *apple_cpu_query(');b=query.index('#define TASK_CPU_QUERY_PREPARE',a)
unit=(here/'query.c').read_text().replace('/* EXTRACT_QUERY */',query[a:b])
for name in ['query','dereference_before_validation']:
 code=unit
 if name=='dereference_before_validation':
  old='struct IntETask *et=NULL;';assert code.count(old)==1;code=code.replace(old,'struct IntETask *et=GetIntETask(task);')
 d=out/name;d.mkdir();(d/'task_accounting.h').write_text(header);(d/'query.c').write_text(code)
 cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-I'+str(d),str(d/'query.c'),'-o',str(d/'test')]
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(d/'build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 r=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=20);(d/'run.log').write_text(r.stdout+r.stderr)
 if name=='query':assert r.returncode==0 and r.stdout=='apple-task-query-host PASS\n' and not r.stderr,r
 else:assert r.returncode!=0 and ('AddressSanitizer' in r.stderr or 'runtime error:' in r.stderr or 'Assertion failed:' in r.stderr),r
 results[name]={'exit':r.returncode,'command':cmd,'source_sha256':hashlib.sha256(query.encode()).hexdigest()}
# Compile the actual SMP conditional: preserve existing non-CPU task pointer,
# while deliberately returning unavailable (zero) CPU accounting.
d=out/'smp_unavailable';d.mkdir();(d/'task_accounting.h').write_text(header)
smp=unit[:unit.index('int main(void)')]+"""int main(void){struct TaskResBase base={{&entry},{NULL}};struct AppleTaskSample sample;struct IntETask snapshot;
setup();assert(apple_cpu_query(&base,allocated,&sample,&snapshot)==GetIntETask(allocated));
assert(!sample.seconds&&!sample.micros&&!sample.usage&&!opens&&!closes);retire();puts("smp unavailable PASS");}
"""
(d/'query.c').write_text(smp)
cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-D__AROSEXEC_SMP__','-g','-fsanitize=address,undefined','-I'+str(d),str(d/'query.c'),'-o',str(d/'test')]
r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(d/'build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
r=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=20);(d/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0 and r.stdout=='smp unavailable PASS\n' and not r.stderr,r
results['smp_unavailable']={'exit':r.returncode,'command':cmd}
(out/'result.json').write_text(json.dumps({'result':'PASS','source_sha256':hashlib.sha256(header.encode()).hexdigest(),'runs':results},indent=2)+'\n');print('apple-task-cpu-host PASS plus four rejected mutants including lifetime')
