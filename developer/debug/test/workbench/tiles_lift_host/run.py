#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
"""Compile fresh production lift bodies against an accounting host service model."""
import argparse, hashlib, json, pathlib, subprocess
p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
here=pathlib.Path(__file__).resolve().parent
source=here.parents[4]/'workbench/system/Tiles/lift.c'
s=source.read_text(); unit=(here/'fixture.h').read_text()+'\n'+'\n'.join(line for line in s.splitlines() if not line.startswith('#include'))+'\n'+(here/'test.c').read_text()
(a.output/'unit.c').write_text(unit)
cmd=['clang','-std=c99','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined',str(a.output/'unit.c'),'-o',str(a.output/'test')]
(a.output/'command.json').write_text(json.dumps(cmd))
r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(a.output/'compile.log').write_text(r.stdout+r.stderr)
if r.returncode:raise SystemExit(r.stderr)
r=subprocess.run([str(a.output/'test')],capture_output=True,text=True,timeout=30);(a.output/'host.log').write_text(r.stdout+r.stderr)
if r.returncode or r.stderr:raise SystemExit(r.stdout+r.stderr)
v=json.loads(r.stdout);assert v['result']=='PASS';v['source_sha256']=hashlib.sha256(source.read_bytes()).hexdigest();v['scope']='Exact production lift bodies; host allocation/graphics/window model, no native focus/input/timing proof';(a.output/'result.json').write_text(json.dumps(v,indent=2)+'\n');print(json.dumps(v))
