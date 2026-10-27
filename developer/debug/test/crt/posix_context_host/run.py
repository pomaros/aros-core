#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
from pathlib import Path
import argparse, hashlib, json, subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--tsan', action='store_true')
a = p.parse_args()
fixture = Path(__file__).resolve().parent
source = fixture.parents[4]
engine = source / 'compiler/crt/posixc/__cwd_context.c'
out = a.output.resolve(); out.mkdir(parents=True, exist_ok=False)
shim = out / 'shim'; shim.mkdir()
boundary = '''#ifndef TEST_BOUNDARY_H
#define TEST_BOUNDARY_H
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
typedef void *BPTR;
typedef intptr_t SIPTR;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define BNULL NULL
#define BADDR(x) (x)
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define ERROR_INVALID_LOCK 211
#define ERROR_NO_FREE_STORE 103
#define ACTION_COPY_DIR 19
#define ACTION_FREE_LOCK 15
struct MsgPort { int unused; };
struct FileLock { struct MsgPort *fl_Task; unsigned identity; };
struct SignalSemaphore { atomic_flag held; };
void *AllocVec(size_t, unsigned);
void FreeVec(void *);
void InitSemaphore(struct SignalSemaphore *);
void ObtainSemaphore(struct SignalSemaphore *);
void ReleaseSemaphore(struct SignalSemaphore *);
SIPTR DoPktResult(struct MsgPort *, LONG, const SIPTR *, SIPTR *);
#endif
'''
(shim / 'test_boundary.h').write_text(boundary)
for name in ['dos/dos.h', 'dos/dosextens.h', 'exec/memory.h', 'exec/types.h', 'exec/semaphores.h', 'proto/exec.h', 'proto/dos.h']:
    path = shim / name; path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "test_boundary.h"\n')
records = []
for case in ['production', 'control-set-unretained', 'control-snapshot-unretained', 'control-error-lost']:
    actual = engine
    if case != 'production':
        text = engine.read_text()
        if case == 'control-set-unretained':
            text = text.replace('!__cwd_directory_retain(directory)', '!directory')
        elif case == 'control-snapshot-unretained':
            text = text.replace('ok = __cwd_directory_retain(context->cwd);', 'ok = TRUE;')
        else:
            text = text.replace('secondary ? (LONG)secondary : ERROR_INVALID_LOCK', 'ERROR_INVALID_LOCK')
        actual = out / (case + '.c'); actual.write_text(text)
    wrapper = out / (case + '-test.c')
    wrapper.write_text((fixture / 'test.c').read_text().replace('"CONTEXT_SOURCE"', '"' + str(actual) + '"'))
    binary = out / case
    cmd = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-pthread',
           '-fsanitize=' + ('thread' if a.tsan else 'address,undefined'), '-I' + str(shim),
           '-iquote', str(engine.parent), str(wrapper), '-o', str(binary)]
    with (out / (case + '-build.log')).open('w') as log:
        q = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=30)
    records.append({'name': case + '-build', 'command': cmd, 'exit': q.returncode, 'pass': q.returncode == 0})
    if not q.returncode:
        with (out / (case + '.log')).open('w') as log:
            q = subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, timeout=60)
        records.append({'name': case, 'exit': q.returncode, 'pass': q.returncode == 0 if case == 'production' else q.returncode != 0})
inputs = [engine, engine.with_suffix('.h'), Path(__file__), fixture / 'test.c']
result = {'result': 'PASS' if all(r['pass'] for r in records) else 'FAIL', 'records': records,
          'inputs': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs},
          'scope': 'Actual retained context engine with modeled DOS packets and semaphores; no pthread integration, complete path consumer or guest claim.'}
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n'); print(result['result'])
raise SystemExit(result['result'] != 'PASS')
