#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL), version 1.1.
"""Check the transitive dependencies of every public desktop entry target."""
import argparse
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[4] / 'arch/aarch64-apple/mmakefile.src')
a = p.parse_args()
graph = {}
pending = ''
for line in a.source.read_text().splitlines():
    if not line.startswith('#MM'):
        continue
    part = line[3:].lstrip('- ').strip()
    pending += ' ' + part.rstrip('\\').strip()
    if part.endswith('\\'):
        continue
    if ':' in pending:
        targets, dependencies = pending.split(':', 1)
        for target in targets.split():
            graph.setdefault(target, set()).update(dependencies.split())
    pending = ''

def closure(target, active=None):
    active = set() if active is None else active
    if target in active:
        raise ValueError('cyclic desktop dependency: ' + target)
    result = {target}
    for dependency in graph.get(target, ()):
        result.update(closure(dependency, active | {target}))
    return result

required = {'kernel-debug', 'kernel-processor', 'kernel-entropy'}
failed = False
for target in ('apple-wanderer-modules', 'apple-wanderer-payloads', 'apple-wanderer-cell-payloads'):
    missing = required - closure(target)
    if missing:
        print('desktop-build-closure result=FAIL target=' + target + ' missing=' + ','.join(sorted(missing)))
        failed = True
if failed:
    raise SystemExit(1)
print('desktop-build-closure result=PASS entrypoints=3 residents=debug,processor,entropy')
