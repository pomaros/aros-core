#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1
"""The Apple code arena allocator on HOST, compiled from its own source.

With --compare REV, the session also runs against page_allocator.c at that
git revision, and the new allocator must satisfy more 2 MiB requests.
"""
from pathlib import Path
import argparse,json,re,subprocess
ROOT=Path(__file__).resolve().parents[5]
HERE=Path(__file__).resolve().parent
KERNEL=ROOT/'arch/aarch64-apple/kernel'

def stubs(out):
 inc=out/'include'
 for d in ('aros/apple','exec','proto'):(inc/d).mkdir(parents=True)
 for h in ('runtime.h','runtime_local.h','page_arena.h'):(inc/'aros/apple'/h).symlink_to(ROOT/'arch/aarch64-apple/include/aros/apple'/h)
 (inc/'aros/config.h').write_text('')
 (inc/'aros/debug.h').write_text('#include <stdio.h>\n#define bug printf\n')
 (inc/'aros/kernel.h').write_text('#define MAP_Readable 1\n#define MAP_Writable 2\n#define MAP_Executable 4\n')
 (inc/'exec/memory.h').write_text('#define MEMF_CLEAR (1L<<16)\n#define MEMF_31BIT (1L<<12)\n#define MEMF_24BITDMA (1L<<9)\n')
 (inc/'exec/execbase.h').write_text('');(inc/'proto/exec.h').write_text('')
 return inc

def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--cc',default='clang');p.add_argument('--compare');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
 inc=stubs(a.out);records=[]
 def run(name,cmd):
  r=subprocess.run([str(v) for v in cmd],capture_output=True,text=True,timeout=300);(a.out/(name+'.log')).write_text(r.stdout+r.stderr);records.append(dict(name=name,command=[str(v) for v in cmd],rc=r.returncode));r.check_returncode();return r.stdout
 flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-I',inc]
 run('compile',[a.cc,*flags,'-I',KERNEL,KERNEL/'page_allocator.c',HERE/'test.c','-o',a.out/'arena-test'])
 out=run('run',[a.out/'arena-test'])
 assert 'ARENA: random and placement PASS' in out
 new=int(re.search(r'satisfied: (\d+) of 40',out).group(1));result=dict(result='PASS_HOST_ONLY',session_2mib_new=new)
 if a.compare:
  old=a.out/'old';old.mkdir()
  (old/'page_allocator.c').write_text(subprocess.run(['git','-C',ROOT,'show',f'{a.compare}:arch/aarch64-apple/kernel/page_allocator.c'],capture_output=True,text=True,check=True).stdout)
  for h in ('page_allocator.h','memory_lock.h','kernel_base.h'):
   if (KERNEL/h).exists():(old/h).symlink_to(KERNEL/h)
  run('compile-old',[a.cc,*flags,'-I',old,'-I',KERNEL,old/'page_allocator.c',HERE/'test.c','-o',a.out/'arena-test-old'])
  before=int(re.search(r'satisfied: (\d+) of 40',run('run-old',[a.out/'arena-test-old','session'])).group(1))
  assert new>before,(new,before);result.update(compare=a.compare,session_2mib_old=before)
 result.update(records=records,native_execution='NOT_RUN')
 (a.out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='records'}))
if __name__=='__main__':main()
