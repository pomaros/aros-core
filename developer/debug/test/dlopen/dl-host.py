#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL), Version 1.1.
"""Run the actual dlopen/dlsym/dlclose/dlerror sources against modeled exec and DOS under sanitizers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
POSIXC = ROOT / 'compiler/crt/posixc'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
output = args.output.resolve()
if output.exists() or output == ROOT or ROOT in output.parents:
    parser.error('output must be a new directory outside the source tree')
sources = {name: POSIXC / (name + '.c') for name in ('__dl', 'dlopen', 'dlsym', 'dlclose', 'dlerror', 'dladdr')}
inputs = list(sources.values()) + [POSIXC / '__dl.h', HERE / 'dl-host.c', Path(__file__).resolve()]
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
before = {str(p): digest(p) for p in inputs}
output.mkdir(parents=True)
# __dl.h's declarations, without its includes, go first.
header = re.sub(r'^#(?:include|ifndef|define ___DL_H|endif)[^\n]*\n', '', (POSIXC / '__dl.h').read_text(), flags=re.M)
for name, source in sources.items():
    text = re.sub(r'^#include[^\n]*\n', '', source.read_text(), flags=re.M)
    for public in ('dlopen', 'dlsym', 'dlclose', 'dlerror', 'dladdr'):
        text = re.sub(r'\b' + public + r'(\s*\()', r'aros_test_' + public + r'\1', text)
    if name == '__dl':
        text = header + text
    (output / ('production-' + name.lstrip('_') + '.c')).write_text(text)
(output / 'fixture.c').write_text((HERE / 'dl-host.c').read_text())
command = ['clang', '-std=gnu11', '-g', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror',
           '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
           str(output / 'fixture.c'), '-o', str(output / 'fixture')]
env = dict(os.environ, ASAN_OPTIONS=('detect_leaks=0:abort_on_error=1' if sys.platform == 'darwin'
                                    else 'detect_leaks=1:abort_on_error=1'),
           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
for index, cmd in enumerate([command, [str(output / 'fixture')]]):
    result = subprocess.run(cmd, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=300)
    (output / f'{index}.log').write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(result.stdout)
if before != {str(p): digest(p) for p in inputs}:
    raise RuntimeError('source changed during the dlopen fixture')
(output / 'result.json').write_text(json.dumps({'host_dlopen': 'PASS',
    'sanitizers': ['address', 'undefined'], 'exec_dos': 'MODELED', 'native_runtime': 'UNTESTED',
    'source_sha256': before, 'command': command}, indent=2) + '\n')
print('PASS actual dlopen/dlsym/dlclose/dlerror host fixture; native behavior remains a separate qualification')
