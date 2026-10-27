#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Exercise actual FAT resize at modeled cache/FAT/directory I/O boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[5]
source = root / 'rom/filesys/fat/ops.c'
text = source.read_text()
a = text.index('LONG OpSetFileSize(')
b = text.index('\n}\n', a) + 3
body = text[a:b]
host = (here / 'host.c').read_text()
controls = {
    'zero-fill-lost': (body.replace('if (size > lock->gl->size)', 'if (0)'), 'zero before size publication'),
    'short-write-skipped': (body.replace('position += written;', 'position += length;'), 'zero before size publication'),
    'old-tail-skipped': (body.replace('ULONG position = lock->gl->size;', 'ULONG position = (lock->gl->size + 511) & ~511U;'), 'zero before size publication'),
    'rollback-link-lost': (body.replace('if (linked && !SET_NEXT_CLUSTER(sb, last, original_next))', 'if (0 && linked && !SET_NEXT_CLUSTER(sb, last, original_next))'), 'fat[c]=='),
}
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
records = []
for name, (actual, expected) in [('production', (body, None)), *controls.items()]:
    assert name == 'production' or actual != body
    path = output / (name + '.c')
    path.write_text(host.replace('/* RESIZE */', actual))
    command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined', str(path), '-o', str(output / name)]
    with (output / (name + '-compile.log')).open('w') as log:
        compiled = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=30)
    row = {'name': name, 'command': command, 'compile_exit': compiled.returncode, 'expected_cause': expected, 'run_exit': None, 'pass': False}
    if not compiled.returncode:
        logpath = output / (name + '.log')
        with logpath.open('w') as log:
            try:
                process = subprocess.run([str(output / name)], stdout=log, stderr=subprocess.STDOUT, timeout=10)
                row['run_exit'] = process.returncode
            except subprocess.TimeoutExpired:
                row['run_exit'] = 124
        row['pass'] = (row['run_exit'] not in (0, 124) and expected in logpath.read_text()) if expected else row['run_exit'] == 0
    records.append(row)
result = {'result': 'PASS' if all(row['pass'] for row in records) else 'FAIL', 'records': records,
          'inputs': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (source, here / 'host.c', Path(__file__))},
          'scope': 'Actual OpSetFileSize body, modeled FAT/cache/directory operations. No actual WriteFileChunk, disk cache ordering, crash recovery, guest or native filesystem acceptance.'}
(output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(result['result'])
for row in records:
    if not row['pass']:
        print(row['name'], row['compile_exit'], row['run_exit'])
raise SystemExit(result['result'] != 'PASS')
