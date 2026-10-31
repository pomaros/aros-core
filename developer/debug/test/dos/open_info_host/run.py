#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Actual DOS open packet helper and FAT open operation with mocked storage."""
import argparse, hashlib, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[4];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
fs=root/'rom/dos/fs_driver.c';fat=root/'rom/filesys/fat/ops.c'
s=fs.read_text();s=s[s.index('static LONG fs_open('):s.index('LONG fs_ReadLink(')]
f=fat.read_text();i=f.index('LONG OpOpenFile(');f=f[i:f.index('\n}\n',i)+3]
(out/'actual.c').write_text(s+'\n'+f)
cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-g','-fsanitize=address,undefined','-I'+str(out),str(here/'host.c'),'-o',str(out/'host')]
b=subprocess.run(cmd,capture_output=True,text=True,timeout=30);(out/'compile.log').write_text(b.stdout+b.stderr)
r=None
if not b.returncode:
 r=subprocess.run([str(out/'host')],capture_output=True,text=True,timeout=15);(out/'run.log').write_text(r.stdout+r.stderr)
record={'scope':__doc__,'result':'HOST_PASS_NOT_EXECUTED' if not b.returncode and r.returncode==0 else 'FAIL','command':cmd,'compile_exit':b.returncode,'run_exit':None if r is None else r.returncode,'inputs':{str(v.relative_to(root)):hashlib.sha256(v.read_bytes()).hexdigest() for v in [fs,fat,here/'host.c',Path(__file__)]}}
(out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(record['result']);assert record['result']!='FAIL'
