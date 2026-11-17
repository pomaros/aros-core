#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER
# Distributed under the AROS Public License 1.1.
"""Compile the production stdio engines and buffered DOS64 seek on modeled I/O."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[4]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)


def block(path, signature):
    text = (root / path).read_text()
    start = text.index(signature)
    begin = text.index('{', start)
    depth = 0
    for end in range(begin, len(text)):
        if text[end] == '{':
            depth += 1
        elif text[end] == '}':
            depth -= 1
            if depth == 0:
                return text[start:end + 1].replace('FILE *', 'MockFile *')
    raise ValueError(signature)


parts = {
    'FHFLAGS': (root / 'rom/dos/dos_fhflags.h').read_text(),
    'SEEK': block('compiler/crt/posixc/__fseeko.c', 'int __fseeko64 ('),
    'TELL': block('compiler/crt/posixc/__ftello.c', 'off64_t __ftello64('),
    'RAW_SEEK': block('compiler/crt/posixc/__dos64.c', 'static QUAD __dos64_rawseek('),
    'RAW_TELL': block('compiler/crt/posixc/__dos64.c', 'static QUAD __dos64_rawgetpos('),
    'BUFFERED': block('rom/dos64/dos64_support.c', 'QUAD dos64_SeekBuffered('),
}
fixture = (here / 'host.c').read_text()
variants = {'production': (parts, None)}
for name, key, old, new, diagnostic in [
    ('legacy-read-flush', 'SEEK', 'if (__dos64_seek(', 'if (!Flush(fdesc->fcb->handle) || __dos64_seek(', 'wide relative seek'),
    ('legacy-tell-flush', 'TELL', 'cnt = __dos64_getpos', 'Flush(fdesc->fcb->handle); cnt = __dos64_getpos', 'tell preserves buffer'),
    ('ignored-wide-write-failure', 'BUFFERED', 'if (!Flush(file))\n            return -1;', '(void)Flush(file);', 'wide flush failure'),
    ('ignored-narrow-write-failure', 'RAW_SEEK', 'if (!Flush(fh))\n        return -1;', '(void)Flush(fh);', 'narrow flush failure'),
    ('ignored-narrow-tell-failure', 'RAW_TELL', 'if (!Flush(fh))\n        return -1;', '(void)Flush(fh);', 'narrow tell flush failure'),
]:
    changed = dict(parts)
    assert old in changed[key], name
    changed[key] = changed[key].replace(old, new, 1)
    variants[name] = (changed, diagnostic)
results = []
for name, (selected, diagnostic) in variants.items():
    source = fixture
    for key, value in selected.items():
        source = source.replace('/* @' + key + '@ */', value)
    path = out / (name + '.c')
    path.write_text(source)
    cmd = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
           '-fsanitize=address,undefined', str(path), '-o', str(out / name)]
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    run = subprocess.run([str(out / name)], capture_output=True, text=True, timeout=15)
    (out / (name + '.log')).write_text(run.stdout + run.stderr)
    if diagnostic:
        assert run.returncode != 0 and ('FAIL: ' + diagnostic + '\n') in run.stderr, (name, run.stderr)
    else:
        assert run.returncode == 0 and 'PASS' in run.stdout, run.stderr
        print(run.stdout.strip())
    results.append({'name': name, 'returncode': run.returncode, 'expected_failure': diagnostic})
(out / 'result.json').write_text(json.dumps({'result': 'PASS', 'scope': 'HOST modeled DOS boundaries; not guest qualification', 'cases': results,
    'production_sha256': {key: hashlib.sha256(value.encode()).hexdigest() for key, value in parts.items()}}, indent=2) + '\n')
print('stdio-seek HOST PASS; five compiling negative controls rejected')
