#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Actual ChildStatus/waitpid bodies with modeled Exec queues and locks."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[4]
status = root / 'rom/exec/childstatus.c'
wait = root / 'compiler/crt/posixc/waitpid.c'
def body(path):
    text = path.read_text()
    start = text.index('\n{', text.index('INTERNALS'))
    return text[start:text.rindex('}') + 1]
s, w = body(status), body(wait)
a.output = a.output.resolve()
if a.output == root or root in a.output.parents:
    p.error('output must be outside source')
a.output.mkdir(parents=True, exist_ok=False)
cases = {
    'production': (s, w),
    'lost-wildcard-query': (s.replace('tid == 0 || ', ''), w),
    'no-child-reports-active': (s, w.replace('        errno = ECHILD;\n        return -1;', '        if (tid == 0 && (options & WNOHANG)) return 0;\n        errno = ECHILD;\n        return -1;')),
}
rows = []
for name, (sbody, wbody) in cases.items():
    work = a.output / name
    work.mkdir()
    (work / 'childstatus-production.c').write_text('static ULONG fixture_ChildStatus(ULONG tid)' + sbody)
    (work / 'waitpid-production.c').write_text('static pid_t fixture_waitpid(pid_t pid, int *status, int options)' + wbody)
    (work / 'host.c').write_text((here / 'host.c').read_text())
    command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               str(work / 'host.c'), '-o', str(work / 'test')]
    subprocess.run(command, check=True, timeout=30)
    r = subprocess.run([str(work / 'test')], capture_output=True, text=True, timeout=15)
    (work / 'output.txt').write_text(r.stdout + r.stderr)
    if name == 'production':
        assert r.returncode == 0, r.stderr
    else:
        expected = 'ChildStatus(0) == CHILD_ACTIVE' if name == 'lost-wildcard-query' else 'fixture_waitpid(pid,&status,options) == result'
        assert r.returncode != 0 and expected in r.stderr, r.stderr
    rows.append({'case': name, 'command': command, 'exit': r.returncode, 'verdict': 'PASS'})
(a.output / 'result.json').write_text(json.dumps({
    'result': 'PASS', 'scope': 'Actual ChildStatus/waitpid; modeled task queues/lock ordering/reaping; no real scheduler concurrency or guest acceptance',
    'sources_sha256': {str(f.relative_to(root)): hashlib.sha256(f.read_bytes()).hexdigest()
                       for f in (status, wait, here / 'host.c', here / 'run.py')},
    'records': rows}, indent=2) + '\n')
print('PASS actual child status/wait and two compiling controls')
