#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Actual hard-link function at a modeled DOS/path-scope boundary."""
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
root = here.parents[3]
source = root / 'compiler/crt/posixc/link.c'
raw = source.read_text()
unit = re.sub(r'^#include[^\n]*', '', raw, flags=re.M).replace('int link(', 'int aros_link(', 1)
a.output = a.output.resolve()
if a.output == root or root in a.output.parents:
    p.error('output must be outside source')
a.output.mkdir(parents=True, exist_ok=False)
cases = {
    'production': unit,
    'lost-cross-volume-check': unit.replace('destination->dvp_Port != ((struct FileLock *)BADDR(source))->fl_Task', '0', 1),
    'lost-error-restoration': unit.replace('    errno = saved_errno;', '    (void)saved_errno;', 1),
}
records = []
for name, text in cases.items():
    work = a.output / name
    work.mkdir()
    (work / 'link-production.c').write_text(text)
    (work / 'host.c').write_text((here / 'link-host.c').read_text())
    command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               str(work / 'host.c'), '-o', str(work / 'test')]
    subprocess.run(command, check=True, timeout=30)
    r = subprocess.run([str(work / 'test')], capture_output=True, text=True, timeout=15)
    (work / 'output.txt').write_text(r.stdout + r.stderr)
    if name == 'production':
        assert r.returncode == 0, r.stderr
    else:
        expected = 'result == (mode ? -1 : 0)' if name == 'lost-cross-volume-check' else 'errno == expected[mode]'
        assert r.returncode != 0 and expected in r.stderr, r.stderr
    records.append({'case': name, 'exit': r.returncode, 'command': command, 'verdict': 'PASS'})
(a.output / 'result.json').write_text(json.dumps({
    'result': 'PASS', 'scope': 'actual link body; DOS calls, path conversion/scope modeled; no mounted handler or SMP qualification',
    'sources_sha256': {str(f.relative_to(root)): hashlib.sha256(f.read_bytes()).hexdigest()
                       for f in (source, here / 'link-host.c', here / 'link-host.py')},
    'records': records}, indent=2) + '\n')
print('PASS native hard-link production and two compiling controls')
