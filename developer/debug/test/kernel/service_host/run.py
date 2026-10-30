#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""HOST test of the actual service coordinator; no AROS runtime claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
SOURCE = ROOT / 'rom/kernel/mapping'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--cc', default='clang')
    p.add_argument('--allow-dirty-diagnostic', action='store_true')
    a = p.parse_args()
    out = a.out.resolve()
    if out.exists() or out == ROOT or ROOT in out.parents:
        p.error('output must be a fresh directory outside source checkout')
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--',
        'rom/kernel/mapping', str(HERE.relative_to(ROOT))], cwd=ROOT))
    if dirty and not a.allow_dirty_diagnostic:
        p.error('dirty source requires --allow-dirty-diagnostic')
    paths = [SOURCE / x for x in ('service.c', 'service.h', 'domain.c', 'domain.h')]
    paths += [HERE / 'test.c', HERE / 'run.py']
    hashes = {str(x.relative_to(ROOT)): digest(x) for x in paths}
    out.mkdir(parents=True)
    result = {'result': 'FAIL', 'scope': 'HOST serialized coordinator, not production AROS',
        'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'dirty_inputs': dirty, 'input_hashes': hashes, 'records': []}
    mutations = [
        ('stale-token', 's->generation == (uint32_t)(token >> 32)', 's->generation != 0', 'stale-take'),
        ('wrong-owner', 's->owner != owner', '!owner', 'collect-owner'),
        ('early-release', 's->state == KS_RUNNING || s->state == KS_CANCELLED',
         's->state == KS_CANCELLED', 'cancel-retention'),
        ('generation-wrap', 's->generation != UINT32_MAX', '1', 'generation-exhausted'),
        ('late-reply', 's->state == KS_CANCELLED || q->domain->closing',
         's->state == KS_CANCELLED', 'external-stop-ack'),
    ]
    env = {**os.environ, 'ASAN_OPTIONS': 'abort_on_error=1:detect_leaks=' + ('0' if sys.platform == 'darwin' else '1'),
           'UBSAN_OPTIONS': 'halt_on_error=1'}
    try:
        result['compiler'] = subprocess.check_output([a.cc, '--version'], text=True).splitlines()[0]
        original = (SOURCE / 'service.c').read_text()
        for name, before, after, failure in [('baseline', None, None, None)] + mutations:
            source = SOURCE / 'service.c'
            if before is not None:
                if original.count(before) != 1:
                    raise RuntimeError(name + ': mutation no longer uniquely matches')
                source = out / (name + '.c')
                source.write_text(original.replace(before, after))
            binary = out / name
            command = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(SOURCE),
                str(source), str(SOURCE / 'domain.c'), str(HERE / 'test.c'), '-o', str(binary)]
            record = {'name': name, 'build_command': command}
            result['records'].append(record)
            build = subprocess.run(command, capture_output=True, text=True, timeout=30)
            (out / (name + '-build.log')).write_text(build.stdout + build.stderr)
            record['build_exit'] = build.returncode
            if build.returncode:
                raise RuntimeError(name + ': compile failed')
            record['binary_sha256'] = digest(binary)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10, env=env)
            record.update(exit=run.returncode, stdout=run.stdout, stderr=run.stderr)
            (out / (name + '.log')).write_text(run.stdout + run.stderr)
            expected = 'FAIL ' + failure + '\n' if failure else 'PASS service ownership cancellation close generations bounds\n'
            if run.returncode != (1 if failure else 0) or run.stdout != expected or run.stderr:
                raise RuntimeError(name + ': unexpected runtime result')
            record['result'] = 'PASS'
        if hashes != {str(x.relative_to(ROOT)): digest(x) for x in paths}:
            raise RuntimeError('inputs changed during run')
        result['result'] = 'DIAGNOSTIC_PASS' if dirty else 'PASS'
    except Exception as e:
        result['error'] = str(e)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'result': result['result'], 'error': result.get('error'), 'out': str(out)}))
    return int(result['result'] == 'FAIL')


if __name__ == '__main__':
    raise SystemExit(main())
