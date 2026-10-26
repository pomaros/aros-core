#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Check actual pipe-name allocation; do not model DOS pipe lifetime."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[4]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
header = root/'compiler/crt/posixc/__pipe_name.h'
original = header.read_text()
# A reset counter produces duplicate names even with no scheduler race.
marker = '__atomic_add_fetch(&serial, 1, __ATOMIC_RELAXED)'
assert original.count(marker)==1
variants = {'current': original, 'truncated-pid': original.replace('pid, number','(pid & (ULONG_MAX >> (sizeof(unsigned long)*CHAR_BIT/2))), number'),
            'reused-serial': original.replace(marker,'(__atomic_store_n(&serial, 1, __ATOMIC_RELAXED), 1UL)')}
rows = []
with tempfile.TemporaryDirectory(prefix='pipe-name-') as temporary:
    for name, text in variants.items():
        out = a.output/name
        out.mkdir()
        (out/'__pipe_name.h').write_text(text)
        binary = Path(temporary)/name
        cmd = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-I'+str(out), str(here/'test.c'), '-o', str(binary)]
        built = subprocess.run(cmd,capture_output=True,text=True,timeout=20)
        (out/'build.log').write_text(built.stdout+built.stderr)
        built.check_returncode()
        ran = subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
        (out/'run.log').write_text(ran.stdout+ran.stderr)
        expected = ran.returncode==0 if name=='current' else ran.returncode!=0 and 'Assertion failed:' in ran.stderr
        rows.append({'variant':name,'returncode':ran.returncode,'expected':expected})
result = {'result':'PASS' if all(x['expected'] for x in rows) else 'FAIL', 'runs':rows,
          'scope':'HOST actual unsigned reservation/formatting under contention; no descriptors, DOS handler, guest or hardware qualification',
          'sources':{str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest()
                     for f in (header, here/'test.c', Path(__file__).resolve())}}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
raise SystemExit(result['result']!='PASS')
