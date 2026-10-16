#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,hashlib,json,pathlib,subprocess
here=pathlib.Path(__file__).resolve().parent
root=here.parents[4];src=root/'workbench/system/Tiles'
p=argparse.ArgumentParser();p.add_argument('--output',required=True);args=p.parse_args()
out=pathlib.Path(args.output);out.mkdir(parents=True,exist_ok=False)
inputs=[src/'tiles_group.c',src/'tiles_group.h',here/'test.c']
hashes={}
for f in inputs:
    data=f.read_bytes();(out/f.name).write_bytes(data);hashes[str(f.relative_to(root))]=hashlib.sha256(data).hexdigest()
cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(out),str(out/'tiles_group.c'),str(out/'test.c'),'-o',str(out/'test')]
build=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
(out/'build.log').write_text(build.stdout+build.stderr);assert build.returncode==0,build.stderr
run=subprocess.run([str(out/'test')],capture_output=True,text=True,timeout=20)
(out/'run.log').write_text(run.stdout+run.stderr)
assert run.returncode==0 and run.stdout=='tiles-group-host PASS\n' and not run.stderr,run
(out/'result.json').write_text(json.dumps({'result':'PASS','hashes':hashes,'command':cmd,'exit':run.returncode},indent=2)+'\n')
print(run.stdout,end='')
