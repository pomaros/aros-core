#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER. AROS Public License (APL) Version 1.1.
"""Actual send path with serialized CPU/interrupt stubs; no SMP hardware claim."""
import argparse, hashlib, json, os, subprocess
from pathlib import Path
here=Path(__file__).resolve().parent;root=here.parents[4]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve()
if out.exists() or root in out.parents:p.error('fresh output outside source required')
out.mkdir(parents=True)
src=root/'arch/aarch64-apple/kernel/smp.c';header=src.with_suffix('.h');timer=src.with_name('platform_timer_kernel.h')
t=src.read_text();start=t.index('void AROSAppleSMPSendIPI(');end=t.index('{',start)+1;depth=1
while depth:depth+=(t[end]=='{')-(t[end]=='}');end+=1
constants=next(x for x in t.splitlines() if x.startswith('#define AROS_APPLE_MEMORY_IPI'))+'\n'+next(x for x in timer.read_text().splitlines() if x.startswith('#define AROS_APPLE_TIMER_IPI'))
(out/'actual.inc').write_text(constants+'\n'+t[start:end]+'\n')
(out/'smp.h').write_bytes(header.read_bytes())
cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(out),str(here/'test.c'),'-o',str(out/'test')]
b=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(out/'build.log').write_text(b.stdout+b.stderr)
assert b.returncode==0,b.stderr
r=subprocess.run([str(out/'test')],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
(out/'run.log').write_text(r.stdout+r.stderr)
inputs=[src,header,timer,here/'test.c',Path(__file__)]
result={'result':'HOST_PASS' if r.returncode==0 and not r.stderr else 'FAIL','scope':__doc__,'input_hashes':{str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs},'command':cmd,'exit':r.returncode}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result']);raise SystemExit(result['result']=='FAIL')
