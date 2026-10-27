#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Execute the complete POSIXC native DOS callback at mocked DOS boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import resource
import subprocess

here = Path(__file__).resolve().parent
root = here.parents[4]
source = root / 'compiler/crt/posixc'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
inputs = [here / 'host.c', Path(__file__), root / 'compiler/include/dos/context.h',
          source / '__cwd_runtime.h', source / '__cwd_runtime_call.c']
header = (source / '__cwd_runtime.h').read_text()
parts = dict(PUBLIC_CONTEXT=inputs[2].read_text(),
             CALLBACK_REQUEST=re.search(r'^struct __cwd_native_call \{[^\n]+', header, re.M)[0],
             CALLBACK_OPERATIONS='\n'.join(re.findall(r'^#define POSIXC_CONTEXT_.*$', header, re.M)),
             CALLBACK_SOURCE=inputs[4].read_text())
text = inputs[0].read_text()
for name, content in parts.items():
    text = text.replace('/* ' + name + ' */', re.sub(r'^#include[^\n]*\n', '', content, flags=re.M))
forward = 'case DOS_PATH_REPLACE_OBJECT: return ReplaceObject((CONST_STRPTR)a[0], (CONST_STRPTR)a[1]);'
controls = {
    'missing-replacement': (forward, '', 'replacement reaches native DOS exactly once'),
    'swapped-paths': (forward, forward.replace('a[0]', 'a[2]').replace('a[1]', 'a[0]').replace('a[2]', 'a[1]'),
                      'native replacement gets both original paths'),
    'lost-error': (forward, 'case DOS_PATH_REPLACE_OBJECT: { BOOL result = ReplaceObject((CONST_STRPTR)a[0], (CONST_STRPTR)a[1]); SetIoErr(0); return result; }',
                   'native replacement error preserved'),
}
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
rows = []
for name in ['production', *controls]:
    actual = text
    cause = None
    if name in controls:
        old, new, cause = controls[name]
        if actual.count(old) != 1:
            rows.append(dict(name=name, passed=False, error='mutation anchor absent'))
            continue
        actual = actual.replace(old, new)
    unit = out / (name + '.c'); unit.write_text(actual)
    command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-Wno-unused-function', '-g',
               '-fsanitize=address,undefined', str(unit), '-o', str(out / name)]
    p = subprocess.run(command, capture_output=True, text=True, timeout=30)
    (out / (name + '-build.log')).write_text(p.stdout + p.stderr)
    row = dict(name=name, command=command, compile_exit=p.returncode, passed=False)
    if p.returncode == 0:
        try:
            p = subprocess.run([str(out / name)], capture_output=True, text=True, timeout=10)
            (out / (name + '.log')).write_text(p.stdout + p.stderr)
            row.update(run_exit=p.returncode, expected_cause=cause,
                       passed=(p.returncode == 0 and 'PASS: native replacement' in p.stdout) if cause is None
                       else (p.returncode != 0 and 'Assertion failed:' in p.stderr and cause in p.stderr))
        except subprocess.TimeoutExpired:
            row['error'] = 'timeout is not a qualifying control failure'
    rows.append(row)
    if name == 'production' and not row['passed']:
        break
result = dict(result='PASS' if all(r['passed'] for r in rows) else 'FAIL', records=rows,
              inputs={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
              scope='Actual POSIXC native callback/public operation numbers; mocked DOS calls. '
                    'No DOS helper scheduling, handler atomicity or guest acceptance.')
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(result['result'])
raise SystemExit(result['result'] != 'PASS')
