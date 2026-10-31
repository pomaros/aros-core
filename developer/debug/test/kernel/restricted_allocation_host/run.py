#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1.
"""Actual native allocation wrappers with injected allocator failures; HOST only."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[4]
def extract(text,prefix):
 start=text.index(prefix);end=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]+(';' if prefix.startswith('struct ') else '')+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
 if out.exists() or ROOT in out.parents:p.error('fresh output outside source required')
 out.mkdir(parents=True);source=ROOT/'arch/aarch64-apple/kernel/restricted.c';text=source.read_text()
 records=''.join(extract(text,'struct '+n+' {') for n in ['RestrictedTableChunk','RestrictedTableTransfer','RestrictedAllocation','RestrictedShared','RestrictedMemoryOperation','RestrictedSharedOperation','RestrictedDomain'])
 actual=''.join(extract(text,'static '+n) for n in ['size_t r_code_storage(','int r_supply_tables(','size_t r_record_size(','int r_reserve_memory(','uint64_t r_create_shared(','void r_free_allocation(','void r_release_shared(','void r_free_domain_backing('])
 actual=actual.replace('__asm__ volatile("wfe")','abort()')
 actual=actual.replace('register uintptr_t argument __asm__("x0")=(uintptr_t)&p;','uintptr_t argument=(uintptr_t)&p;').replace('__asm__ volatile("svc #0xa569":"+r"(argument)::"memory","cc");','argument=table_publish((void *)argument);')
 (out/'records.inc').write_text(records);(out/'actual.inc').write_text(actual)
 inputs=[source,HERE/'test.c',HERE/'run.py',ROOT/'arch/aarch64-apple/kernel/restricted_service.h']
 result={'result':'FAIL','scope':__doc__,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_inputs':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),'input_hashes':{str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs}}
 command=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(out),'-I'+str(ROOT/'arch/aarch64-apple/kernel'),'-I'+str(ROOT/'arch/aarch64-apple/include'),str(HERE/'test.c'),'-o',str(out/'test')]
 result['command']=command;b=subprocess.run(command,capture_output=True,text=True,timeout=30);(out/'build.log').write_text(b.stdout+b.stderr)
 if not b.returncode:
  r=subprocess.run([str(out/'test')],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'});result['exit']=r.returncode;(out/'run.log').write_text(r.stdout+r.stderr)
  if not r.returncode and r.stdout=='PASS native allocation failure rollback and ownership\n' and not r.stderr:result['result']='DIAGNOSTIC_PASS' if result['dirty_inputs'] else 'HOST_PASS'
 if result['input_hashes']!={str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs}:result['result']='FAIL'
 if (out/'test').exists():result['binary_sha256']=hashlib.sha256((out/'test').read_bytes()).hexdigest()
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result']);return result['result']=='FAIL'
if __name__=='__main__':raise SystemExit(main())
