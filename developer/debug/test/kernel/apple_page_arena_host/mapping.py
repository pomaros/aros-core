#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1
"""Run actual arena mapping bodies with mprotect during their unmapped interval.

Only architecture operations are substituted. --reject REV must fault, proving
that the regression exercises inaccessible in-chunk metadata in the old code.
This does not qualify hardware TLB/cache ordering or a running AROS kernel.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import signal
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
KERNEL = ROOT / 'arch/aarch64-apple/kernel'
p = argparse.ArgumentParser()
p.add_argument('--out', required=True, type=Path)
p.add_argument('--reject')
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=False)
header = (KERNEL / 'page_allocator.h').read_text()
growth = re.search(r'struct AROSApplePageArenaGrowth \{.*?\n\};', header, re.S).group()
constants = (ROOT / 'arch/aarch64-apple/include/aros/apple/page_arena.h').read_text()
records = []
for label, rev in [('current', None)] + ([('negative', a.reject)] if a.reject else []):
    folder = a.out / label
    folder.mkdir()
    source = (KERNEL / 'page_arena_kernel.c').read_text() if rev is None else subprocess.check_output(
        ['git', '-C', str(ROOT), 'show', f'{rev}:arch/aarch64-apple/kernel/page_arena_kernel.c'], text=True)
    defs = source[source.index('#define ARENA_ADDRESS_MASK'):source.index('static void clean_range')]
    body = source[source.index('/* What a free arena page'):source.index('static int arena_can_grow')]
    body = re.sub(r'__asm__ volatile\("([^"]+)" ::: "memory"\);',
        lambda m: 'invalidate();' if 'tlbi' in m[1] else '(void)0;', body)
    assert '__asm__' not in body
    (folder / 'mapping-types.inc').write_text(constants + '\n' + growth + '\n' + defs)
    (folder / 'mapping-body.inc').write_text(body)
    binary = folder / 'mapping'
    compiled = subprocess.run(['clang', '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
        '-I', str(folder), str(HERE / 'mapping.c'), '-o', str(binary)], capture_output=True, text=True)
    (folder / 'compile.log').write_text(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
    (folder / 'run.log').write_text(run.stdout + run.stderr)
    if rev is None:
        assert run.returncode == 0 and 'detach PASS' in run.stdout, run
    else:
        assert run.returncode in (-signal.SIGSEGV, -signal.SIGBUS), run
    records.append(dict(label=label, revision=rev, source_sha256=hashlib.sha256(source.encode()).hexdigest(), rc=run.returncode))
result = dict(result='PASS_HOST_ONLY', records=records, hardware='NOT_RUN')
(a.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
