#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Exercise actual pipe() rollback with modeled DOS/descriptor boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[4]
source = root / 'compiler/crt/posixc/pipe.c'
raw = source.read_text()
a.output = a.output.resolve()
if a.output == root or root in a.output.parents:
    p.error('output must be outside source')
a.output.mkdir(parents=True, exist_ok=False)
unit = re.sub(r'^#include[^\n]*', '', raw, flags=re.M)
unit = unit.replace('int pipe(', 'int fixture_pipe(', 1)
controls = {
    'production': unit,
    'lost-file-block-cleanup': unit.replace('    FreeVec(rfcb);\n    FreeVec(wfcb);', '', 1),
    'lost-error-preservation': unit.replace('    errno = error;\n    return -1;', '    return -1;', 1),
}
records = []
for name, text in controls.items():
    work = a.output / name
    work.mkdir()
    (work / 'pipe-production.c').write_text(text)
    (work / 'host.c').write_text((here / 'host.c').read_text())
    command = ['clang', '-std=c11', '-g', '-O1', '-Wall', '-Wextra',
               '-Werror=implicit-function-declaration', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', str(work / 'host.c'), '-o', str(work / 'test')]
    subprocess.run(command, check=True)
    r = subprocess.run([str(work / 'test')], capture_output=True, text=True, timeout=20)
    (work / 'output.txt').write_text(r.stdout + r.stderr)
    if name == 'production':
        assert r.returncode == 0, r.stderr
    else:
        expected = '!allocations && !handles && !reservations' if name == 'lost-file-block-cleanup' else 'errno == expected'
        assert r.returncode != 0 and expected in r.stderr, r.stderr
    records.append({'case': name, 'command': command, 'exit': r.returncode, 'verdict': 'PASS'})
(a.output / 'result.json').write_text(json.dumps({
    'result': 'PASS', 'scope': 'actual pipe function; modeled Exec/DOS/fd APIs; no guest pipe transport',
    'sources_sha256': {str(f.relative_to(root)): hashlib.sha256(f.read_bytes()).hexdigest()
                       for f in (source, here / 'host.c', here / 'run.py')},
    'records': records}, indent=2) + '\n')
print('PASS production and two compiling defective controls')
