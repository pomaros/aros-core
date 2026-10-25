#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL), Version 1.1.
"""Run the actual futimens/utimensat/utimes implementations against modeled DOS under sanitizers."""
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
if output.exists() or args.output.is_symlink() or output == ROOT or ROOT in output.parents:
    parser.error('output must be a new directory outside the source tree')
sources = {name: POSIXC / (name + '.c') for name in ('__utimens', 'utimensat', 'futimens', 'utimes')}
stat_source = POSIXC / '__stat.c'
inputs = list(sources.values()) + [stat_source, HERE / 'times-host.c', Path(__file__).resolve()]
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
before = {str(p): digest(p) for p in inputs}
output.mkdir(parents=True)
for name, source in sources.items():
    text = re.sub(r'^#include[^\n]*\n', '', source.read_text(), flags=re.M)
    for public in ('utimensat', 'futimens', 'utimes'):
        text = re.sub(r'\b' + public + r'(\s*\()', r'aros_test_' + public + r'\1', text)
    (output / ('production-' + name.lstrip('_') + '.c')).write_text(text)
# __stat()'s reading of the date, so the fixture checks the true inverse.
stat_text = stat_source.read_text()
mtime = re.search(r'sb->st_mtime\s*=\s*(.*?);', stat_text, flags=re.S).group(1)
nsec = re.search(r'sb->st_mtim\.tv_nsec\s*=\s*(.*?);', stat_text, flags=re.S).group(1)
(output / 'stat-expressions.h').write_text(
    '#define STAT_MTIME_EXPRESSION (' + ' '.join(mtime.split()) + ')\n'
    '#define STAT_NSEC_EXPRESSION (' + ' '.join(nsec.split()) + ')\n')
(output / 'fixture.c').write_text((HERE / 'times-host.c').read_text())
command = ['clang', '-std=c11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
           '-Wno-unused-parameter', '-Wno-unused-function',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
           str(output / 'fixture.c'), '-o', str(output / 'fixture')]
env = dict(os.environ, ASAN_OPTIONS=('detect_leaks=0:abort_on_error=1' if sys.platform == 'darwin'
                                    else 'detect_leaks=1:abort_on_error=1'),
           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
for index, cmd in enumerate([command, [str(output / 'fixture')]]):
    result = subprocess.run(cmd, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=60)
    (output / f'{index}.log').write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(result.stdout)
if before != {str(p): digest(p) for p in inputs}:
    raise RuntimeError('source changed during file-times fixture')
(output / 'result.json').write_text(json.dumps({'host_file_times': 'PASS',
    'sanitizers': ['address', 'undefined'], 'DOS': 'MODELED', 'native_runtime': 'UNTESTED',
    'source_sha256': before, 'command': command}, indent=2) + '\n')
print('PASS actual futimens/utimensat/utimes host fixture; native DOS behavior remains a separate qualification')
