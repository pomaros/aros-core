#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Owned C-runtime admission: the actual aros/crt_thread_context.h, the
actual ThreadInfo, StarterFunc, pthread_create, GetThreadId and
GetThreadInfo, with host Exec/task.resource mocks (prelude.h) where each
AROS Task is a host thread. Mutants remove each guarantee."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path

p = argparse.ArgumentParser(); p.add_argument('--output', type=Path, required=True)
p.add_argument('--sanitizer', choices=['address,undefined','thread'], default='address,undefined'); a = p.parse_args()
here = Path(__file__).resolve().parent; root = here.parents[4]
out = a.output.resolve(); out.mkdir(parents=True, exist_ok=False)
header = root / 'compiler/include/aros/crt_thread_context.h'
create = root / 'compiler/pthread/pthread_create.c'
intern = root / 'compiler/pthread/pthread_intern.h'
pthreadc = root / 'compiler/pthread/pthread.c'

def block(text, start):
    i = text.index(start); j = text.index('{', i); d = 0
    for k in range(j, len(text)):
        d += text[k] == '{'; d -= text[k] == '}'
        if d == 0: return text[i:k + 1]
    raise ValueError(start)

c = create.read_text(); it = intern.read_text(); pc = pthreadc.read_text()
threadinfo = it[it.index('typedef struct\n{\n    void *(*start)(void *);'):it.index('} ThreadInfo;') + len('} ThreadInfo;')]
admission = block(it, 'struct PThreadAdmission\n')+';'
clear = block(c, 'static void ClearThreadInfo(')
release = block(c, 'static void ReleaseAdmission(')
starter = block(c, 'static void StarterFunc(void)')
body = c[c.rindex('*****************************************************************************/\n{') + len('*****************************************************************************/\n'):]
creator = 'int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg)\n' + body
helpers = 'ThreadInfo *threads[PTHREAD_THREADS_MAX];\npthread_t threads_used;\n' + block(pc, 'ThreadInfo *GetThreadInfo(pthread_t thread)') + '\n' + block(pc, 'pthread_t GetThreadId(struct Task *task)') + '\n'
actual = '\n'.join([threadinfo, admission, helpers, clear, release, starter, creator])
for n in ['pthread_create', 'pthread_t', 'pthread_attr_t', 'pthread_attr_init']:
    actual = re.sub(r'\b' + n + r'\b', 'aros_' + n, actual)
actual = actual.replace('PTHREAD_CREATE_DETACHED', 'AROS_CREATE_DETACHED')

mutations = {
    'positive': None,
    'success-before-enter': ('create', '    Wait(admit->mask);\n    admitted = admit->result;\n', '    admitted = TRUE;\n'),
    'start-after-refusal': ('create', '            ClearThreadInfo(inf);\n            ReleaseSemaphore(&thread_sem);\n            return;\n', '            ReleaseSemaphore(&thread_sem);\n'),
    'worker-frees-token': ('create', '        ReleaseAdmission(admit);\n        Signal(creator, mask);\n', '        FreeVec(admit);\n        Signal(creator, mask);\n'),
    'pins-released-before-ack': ('create', '    Wait(admit->mask);\n    admitted = admit->result;\n    CRTThreadContextReleasePins(&set);\n', '    CRTThreadContextReleasePins(&set);\n    Wait(admit->mask);\n    admitted = admit->result;\n'),
    'pins-from-worker-copy': ('create', '    CRTThreadContextReleasePins(&set);\n', '    CRTThreadContextReleasePins(&inf->ctxset);\n'),
    'leave-not-called': ('create', '    CRTThreadContextLeave(&inf->ctxset);\n', ''),
    'no-cancel-on-create-failure': ('create', '        CRTThreadContextCancel(&set);\n#endif\n        return EAGAIN;\n    }\n    ReleaseSemaphore(&thread_sem);', '        /* leaked contexts */\n#endif\n        return EAGAIN;\n    }\n    ReleaseSemaphore(&thread_sem);'),
    'memset-after-leave': ('create', '    else\n        ClearThreadInfo(inf);\n', '    else\n        memset(inf, 0, sizeof(ThreadInfo));\n'),
    'memset-after-refusal': ('create', '            ClearThreadInfo(inf);\n            ReleaseSemaphore(&thread_sem);\n            return;\n', '            memset(inf, 0, sizeof(ThreadInfo));\n            ReleaseSemaphore(&thread_sem);\n            return;\n'),

}
expected_causes = {
    'success-before-enter': 'Assertion failed: (atomic_load(&prov[0].entered) == 1 && atomic_load(&prov[1].captured) == 0)',
    'start-after-refusal': 'Assertion failed: (!atomic_load(&started) && atomic_load(&prov[0].left) == 1 && atomic_load(&prov[1].cancelled) == 1)',
    'worker-frees-token': 'AddressSanitizer: heap-use-after-free',
    'pins-released-before-ack': 'Assertion failed: ((p)->loaded && !(p)->dead)',
    'pins-from-worker-copy': 'Assertion failed: (atomic_load(&prov[0].creator_closes) == 1 && atomic_load(&prov[1].creator_closes) == 0 && !(this_task->crt_live > 0))',
    'leave-not-called': 'Assertion failed: (atomic_load(&started) == 1 && atomic_load(&start_saw_worker) == 1 && atomic_load(&prov[0].left) == 1)',
    'no-cancel-on-create-failure': 'Assertion failed: (atomic_load(&prov[0].cancelled) == 1 && GetThreadId(NULL) != PTHREAD_THREADS_MAX)',
    'memset-after-leave': 'Assertion failed: (this_task->crt_live > 0)',
    'memset-after-refusal': 'Assertion failed: (this_task->crt_live > 0)',
}
# A use after free, which AddressSanitizer finds and ThreadSanitizer does not.
memory_only = {'worker-frees-token'}
stubs=out/'stub'
for name in re.findall(r'^#include <([^>]+)>',header.read_text(),re.M):
    f=stubs/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Declared by prelude.h. */\n')
rows = []
for name, m in mutations.items():
    if a.sanitizer == 'thread' and name in memory_only:
        continue
    lane = out / name; (lane / 'aros').mkdir(parents=True)
    parts = {'create': actual, 'header': header.read_text()}
    if m:
        where, before, after = m
        assert parts[where].count(before) == 1, (name, parts[where].count(before))
        parts[where] = parts[where].replace(before, after)
    # C-library calls in the worker go through the admitted-lifetime check
    i = parts['create'].index('static void StarterFunc(void)')
    j = parts['create'].index('int aros_pthread_create(')
    parts['create'] = parts['create'][:i] + parts['create'][i:j].replace('memset(', 'crt_memset(') + parts['create'][j:]
    (lane / 'aros/crt_thread_context.h').write_text(parts['header'])
    (lane / 'pthread_actual.c').write_text(parts['create'])
    cmd = ['clang', '-std=gnu11', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
           '-D__AROS__', '-D__AROSEXEC_SMP__', '-pthread', '-fsanitize=' + a.sanitizer, '-g', '-include', str(here / 'prelude.h'),
           '-I' + str(lane), '-I' + str(stubs), str(here / 'host.c'), '-o', str(lane / 'host')]
    b = subprocess.run(cmd, capture_output=True, text=True); (lane / 'compile.log').write_text(b.stdout + b.stderr)
    r = None
    if b.returncode == 0:
        try:
            r = subprocess.run([str(lane / 'host')], capture_output=True, text=True, timeout=20)
            (lane / 'run.log').write_text(r.stdout + r.stderr); code = r.returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    expected=expected_causes.get(name)
    passed=b.returncode==0 and r is not None and (
        r.returncode!=0 and expected in r.stdout+r.stderr if expected else r.returncode==0)
    rows.append({'name': name, 'compile_exit': b.returncode,
                 'run_exit': None if b.returncode else code, 'expected_cause': expected, 'pass': passed})
inputs = [header, create, intern, pthreadc, here / 'host.c', here / 'prelude.h', Path(__file__)]
record = {'scope': 'Actual crt_thread_context.h and pthread_create/StarterFunc/GetThreadId admission code with host-thread Tasks and modeled Exec namespace/signals/semaphores and provider libraries. No POSIXC provider, generated startup or native guest.',
          'sanitizer': a.sanitizer, 'inputs': {str(v.relative_to(root)): hashlib.sha256(v.read_bytes()).hexdigest() for v in inputs}, 'checks': rows, 'result': 'PASS' if all(r['pass'] for r in rows) else 'FAIL'}
(out / 'result.json').write_text(json.dumps(record, indent=2) + '\n'); print(rows)
assert all(r['pass'] for r in rows)
print('crt-context-host result=PASS sanitizer=' + a.sanitizer)
