#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Run actual vfork scope and cwd/namespace engines with host boundary models."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

here = Path(__file__).resolve().parent
source = here.parents[4] / 'compiler/crt/posixc'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--tsan', action='store_true')
args = parser.parse_args()
inputs = [Path(__file__), here / 'scope.c']


def read(name):
    path = source / name
    inputs.append(path)
    return re.sub(r'^#include[^\n]*\n', '', path.read_text(), flags=re.M)


def block(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if not depth:
            return text[start:end + 1]
    raise ValueError(signature)


runtime = read('__cwd_runtime.c')
names = ['static struct __cwd_binding *binding(', 'static void binding_release(',
         'struct PosixCIntBase *__cwd_runtime_base(',
         'struct __cwd_context *__cwd_runtime_context(',
         'BOOL __cwd_runtime_context_push(', 'BOOL __cwd_runtime_context_pop(',
         'BOOL __cwd_runtime_snapshot(', 'BOOL __cwd_runtime_scope_enter(',
         'void __cwd_runtime_scope_leave(', 'BOOL __cwd_runtime_conversion(']
parts = dict(CONTEXT_HEADER=read('__cwd_context.h'),
             NAMESPACE_HEADER=read('__fd_namespace.h'),
             RUNTIME_HEADER=read('__cwd_runtime.h'),
             CONTEXT_ENGINE=read('__cwd_context.c'),
             NAMESPACE_ENGINE=read('__fd_namespace.c'),
             BINDING_TYPE=block(runtime, 'struct __cwd_binding {') + ';',
             RUNTIME_FUNCTIONS='\n'.join(block(runtime, name) for name in names),
             VFORK_BINDING=read('__vfork_binding.h'))
text = (here / 'scope.c').read_text()
for name, value in parts.items():
    marker = '/* ' + name + ' */'
    assert text.count(marker) == 1, name
    text = text.replace(marker, value)
controls = {
    'unretained': ('!__fd_namespace_retain(child)) return FALSE;', '!child) return FALSE;',
                   '!__vfork_binding_enter(base, child, next, &scope)'),
    'lost-namespace': ('base->fd_namespace = scope->previous;', 'base->fd_namespace = child;',
                       'base->fd_namespace == original && base->fd_owner == base'),
    'lost-context-pop': ('!__cwd_runtime_context_pop(&scope->context)) return FALSE;',
                         'FALSE) return FALSE;', '!__vfork_binding_leave(base, &scope)'),
    'lost-owner-guard': ('scope->task != FindTask(NULL) ||', '',
                         '!__vfork_binding_leave(base, &scope)'),
}
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
records = []
for name in ['production', *controls]:
    actual = text
    cause = None
    if name in controls:
        old, new, cause = controls[name]
        assert actual.count(old) == 1, name
        actual = actual.replace(old, new)
    unit = out / (name + '.c')
    unit.write_text(actual)
    binary = out / name
    command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-function', '-g', '-pthread',
               '-fsanitize=' + ('thread' if args.tsan else 'address,undefined'),
               str(unit), '-o', str(binary)]
    compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
    (out / (name + '-build.log')).write_text(compiled.stdout + compiled.stderr)
    ok = compiled.returncode == 0
    run_exit = None
    if ok:
        try:
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            log = run.stdout + run.stderr
            run_exit = run.returncode
            ok = (run.returncode == 0 and 'PASS: 2000 proxy scopes' in log) if cause is None else (
                run.returncode != 0 and 'Assertion failed:' in log and cause in log)
        except subprocess.TimeoutExpired:
            log = 'TIMEOUT: not a qualifying control failure\n'
            ok = False
        (out / (name + '.log')).write_text(log)
    records.append(dict(name=name, command=command, compile_exit=compiled.returncode,
                        run_exit=run_exit, expected_cause=cause, pass_result=ok))
result = dict(result='PASS' if all(r['pass_result'] for r in records) else 'FAIL',
              records=records, inputs={str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                      for p in inputs},
              scope='Actual vfork binding, runtime push/pop and heap cwd/namespace engines; '
                    'modeled Exec task storage, DOS packets and lifetime attachment. '
                    'No provider admission, generated library startup, assembly jumps or guest acceptance.')
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(result['result'])
raise SystemExit(result['result'] != 'PASS')
