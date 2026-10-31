#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""HOST test of the actual owned-buffer boundary; no AROS runtime claim."""
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
    paths = [SOURCE / x for x in ('user_buffer.c', 'user_buffer.h', 'mapping.c', 'mapping.h', 'domain.c', 'domain.h', 'service.c', 'service.h', 'user_service.c', 'user_service.h')]
    paths += [HERE / 'test.c', HERE / 'run.py']
    hashes = {str(x.relative_to(ROOT)): digest(x) for x in paths}
    out.mkdir(parents=True)
    result = {'result': 'FAIL', 'scope': 'HOST owned-buffer boundary, not hardware translation or production AROS',
        'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'dirty_inputs': dirty, 'input_hashes': hashes, 'records': []}
    mutations = [
        ('service-thread-owner', 'r->principal = requester;', 'r->principal = principal;', 'thread-identities'),
        ('service-buffer-authority', 'KrnBufferWrite(&r->output, r->buffer_principal, bytes', 'KrnBufferWrite(&r->output, principal, bytes', 'thread-buffer-authority'),
        ('service-reset-generation', 'r->token = generation;', 'r->token = generation - generation;', 'cross-queue-generation'),
        ('service-stale-worker', 'r->token != token || (r->state != KS_RUNNING', '!token || (r->state != KS_RUNNING', 'stale-worker-complete'),
        ('foreign-owner', 'm->owner != s', '0', 'foreign-mapping'),
        ('domain-owner', 's->principal == principal', 'principal != 0', 'domain-owner'),
        ('stale-domain', 's->generation == (uint32_t)(handle >> 32)', 's->generation != 0', 'domain-generation'),
        ('permission', '(p->protection & access) == access', '1', 'guard-tail-atomic'),
        ('stale-page', 'p->generation == b->generation', '1', 'page-generation'),
        ('late-write', 'm->closing || m->shared', 'm->shared', 'no-late-write'),
        ('skip-pins', '++lease->parts[i].mapping->in_flight', '(void)lease->parts[i].mapping', 'all-pages-pinned'),
    ]
    env = {**os.environ, 'ASAN_OPTIONS': 'abort_on_error=1:detect_leaks=' + ('0' if sys.platform == 'darwin' else '1'),
           'UBSAN_OPTIONS': 'halt_on_error=1'}
    try:
        result['compiler'] = subprocess.check_output([a.cc, '--version'], text=True).splitlines()[0]
        originals = {name: (SOURCE/name).read_text() for name in ('user_buffer.c', 'user_service.c')}
        for name, before, after, failure in [('baseline', None, None, None)] + mutations:
            filename = 'user_service.c' if name.startswith('service-') else 'user_buffer.c'
            original = originals[filename]
            source = SOURCE / filename
            if before is not None:
                if original.count(before) != 1:
                    raise RuntimeError(name + ': mutation no longer uniquely matches')
                source = out / (name + '.c')
                source.write_text(original.replace(before, after))
            binary = out / name
            command = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(SOURCE),
                str(source if filename == 'user_buffer.c' else SOURCE/'user_buffer.c'), str(source if filename == 'user_service.c' else SOURCE/'user_service.c'), str(SOURCE / 'service.c'), str(SOURCE / 'mapping.c'), str(SOURCE / 'domain.c'), str(HERE / 'test.c'), '-o', str(binary)]
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
            expected = 'FAIL ' + failure + '\n' if failure else 'PASS owned buffers bounds permissions generations lifetime\n'
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
