#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
import argparse,pathlib,subprocess,hashlib,json
here=pathlib.Path(__file__).resolve().parent;root=here.parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',required=True);args=p.parse_args()
out=pathlib.Path(args.output);out.mkdir(parents=True,exist_ok=False);hashes={}
for f in [root/'workbench/system/Tiles/input.c',root/'workbench/system/Tiles/input.h',here/'test.c',here/'mock.h']:
 data=f.read_bytes();(out/f.name).write_bytes(data);hashes[str(f.relative_to(root))]=hashlib.sha256(data).hexdigest()
for f in ['exec/types.h','intuition/screens.h','devices/inputevent.h','libraries/commodities.h','proto/commodities.h','proto/exec.h','proto/arossupport.h']:
 path=out/f;path.parent.mkdir(exist_ok=True,parents=True);path.write_text('#include "mock.h"\n')
cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I'+str(out),str(out/'test.c'),'-o',str(out/'test')]
r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/'build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
r=subprocess.run([str(out/'test')],capture_output=True,text=True,timeout=20);(out/'run.log').write_text(r.stdout+r.stderr);assert not r.returncode and r.stdout=='tiles-input-host PASS\n' and not r.stderr,r
(out/'result.json').write_text(json.dumps({'result':'PASS','hashes':hashes,'command':cmd},indent=2)+'\n');print(r.stdout,end='')
