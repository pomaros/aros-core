#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1
"""The Apple code window on HOST, compiled from its own source."""
from pathlib import Path
import argparse,json,subprocess
ROOT=Path(__file__).resolve().parents[5]
HERE=Path(__file__).resolve().parent
KERNEL=ROOT/'arch/aarch64-apple/kernel'

def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--cc',default='clang');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
 inc=a.out/'include/aros/apple';inc.mkdir(parents=True)
 for h in ('runtime.h','runtime_local.h','page_arena.h'):(inc/h).symlink_to(ROOT/'arch/aarch64-apple/include/aros/apple'/h)
 records=[]
 def run(name,cmd):
  r=subprocess.run([str(v) for v in cmd],capture_output=True,text=True,timeout=300);(a.out/(name+'.log')).write_text(r.stdout+r.stderr);records.append(dict(name=name,command=[str(v) for v in cmd],rc=r.returncode));r.check_returncode();return r.stdout
 run('compile',[a.cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.out/'include','-I',KERNEL,KERNEL/'code_window.c',HERE/'test.c','-o',a.out/'window-test'])
 out=run('run',[a.out/'window-test']);assert 'CODEWINDOW: PASS' in out
 (a.out/'result.json').write_text(json.dumps(dict(result='PASS_HOST_ONLY',records=records,native_execution='NOT_RUN'),indent=2)+'\n');print(out.strip())
if __name__=='__main__':main()
