#!/usr/bin/env python3
# SPDX-License-Identifier: APL-1.1
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
"""Host lifecycle tests of production tile.c and Wanderer's tile adapter.
Graphics and OS entry points are fixtures; this is not GUI qualification.
"""
from pathlib import Path
import re
import argparse
import contextlib
import hashlib
import json
import subprocess
import tempfile
here = Path(__file__).resolve().parent
root = here.parents[4]
lib = root / 'workbench/libs/workbench'
def clean(s):
    return re.sub(r'^\s*#include[^\n]*', '', s, flags=re.M)
public = (root/'compiler/include/workbench/workbench.h').read_text(encoding='latin1')
public = public[public.index('/* Program-drawn tiles.'):public.index('/*** Tags for use with AddAppIconA()')]
adapter = (root/'workbench/system/Wanderer/appobjects.c').read_text(encoding='latin1')
adapter = adapter[adapter.index('/* Called with the Workbench registry mutex held;'):]
unit = ((here/'fixture.h').read_text() + '\n' + public + '\n' +
        clean((lib/'tile_state.h').read_text()) + '\n' +
        clean((lib/'tile_clock.h').read_text()) + '\n' +
        clean((lib/'tile.h').read_text()) + '\n' +
        clean((lib/'tile_messages.h').read_text()) + '\n' +
        clean((lib/'tile.c').read_text()) + '\n' + adapter + '\n' +
        (root/'workbench/system/Wanderer/Classes/iconlist_grid.h').read_text() + '\n' +
        (here/'test_grid.c').read_text() + '\n' +
        (here/'test_pointer.c').read_text() + '\n' +
        (here/'test_indicator.c').read_text() + '\n' +
        (here/'test_snapshot.c').read_text() + '\n' +
        (here/'test.c').read_text())
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, help='new directory preserving generated source and logs')
args = parser.parse_args()
if args.output:
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
context = contextlib.nullcontext(str(args.output)) if args.output else tempfile.TemporaryDirectory(prefix='aros-tiles-')
with context as tmp:
    src = Path(tmp)/'test.c'
    exe = Path(tmp)/'test'
    src.write_text(unit)
    command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
               '-fsanitize=address,undefined', str(src), '-o', str(exe)]
    (Path(tmp)/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    compile = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (Path(tmp)/'compile.log').write_text(compile.stdout+compile.stderr)
    if compile.returncode: raise SystemExit(compile.stderr)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    (Path(tmp)/'host.log').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,end='')
    if result.returncode or result.stderr: raise SystemExit('host test failed')
    required = ['tiles-grid-host PASS:', 'tiles-host PASS:', 'tiles-pointer-host PASS:', 'tiles-indicator-host PASS:', 'tiles-snapshot-host PASS:']
    assert all(result.stdout.count(marker)==1 for marker in required)
    report = {'result':'PASS','generated_source_sha256':hashlib.sha256(unit.encode()).hexdigest(),
              'scope':'Actual library tile.c and presenter adapter with host OS/graphics mocks; not guest evidence',
              'exit_code':result.returncode}
    (Path(tmp)/'result.json').write_text(json.dumps(report,indent=2)+'\n')
