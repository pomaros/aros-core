#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Retain an exact-source indicator state test with sanitizers and hashes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,required=True)
a=p.parse_args(); a.output=a.output.resolve(); a.output.mkdir(parents=True,exist_ok=False)
here=Path(__file__).resolve().parent
source=here.parents[4]/'workbench/system/Indicators'
for name in ['state.c','state.h','battery.c','battery.h','consumers.c','consumers.h','subscriptions.c','subscriptions.h']:
    shutil.copy2(source/name,a.output/name)
for name in ['test.c','run.py']:
    shutil.copy2(here/name,a.output/name)
command=['clang','-std=c99','-Wall','-Wextra','-Werror','-g','-O1',
         '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(a.output),
         str(a.output/'state.c'),str(a.output/'battery.c'),str(a.output/'consumers.c'),str(a.output/'subscriptions.c'),str(a.output/'test.c'),'-o',str(a.output/'test')]
(a.output/'command.json').write_text(json.dumps(command,indent=2)+'\n')
compiled=subprocess.run(command,capture_output=True,text=True,timeout=60)
(a.output/'compile.log').write_text(compiled.stdout+compiled.stderr)
if compiled.returncode: raise SystemExit(compiled.returncode)
run=subprocess.run([str(a.output/'test')],capture_output=True,text=True,timeout=30)
(a.output/'run.log').write_text(run.stdout+run.stderr)
if run.returncode or run.stderr: raise SystemExit(run.returncode or 1)
verdict=json.loads(run.stdout)
assert verdict['result']=='PASS'
verdict['scope']='Host state/parser/placement, CPU ranking/lease and subscription ownership policy only; UI and guest acceptance unverified'
verdict['sha256']={name:hashlib.sha256((a.output/name).read_bytes()).hexdigest()
                   for name in ['state.c','state.h','battery.c','battery.h','consumers.c','consumers.h','subscriptions.c','subscriptions.h','test.c','run.py']}
(a.output/'result.json').write_text(json.dumps(verdict,indent=2)+'\n')
print(json.dumps(verdict))
