#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Compile freshly extracted production RT and lifecycle hooks, with host mocks."""
import argparse, hashlib, json, pathlib, re, subprocess
p = argparse.ArgumentParser()
p.add_argument('--output', type=pathlib.Path, required=True)
p.add_argument('--cc', default='clang')
a = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[5]
a.output.mkdir(parents=True, exist_ok=False)
files = ['rom/task/TaskResourceControl.c', 'rom/task/task_rt.h',
         'rom/task/task_execfuncs.c', 'rom/dos/createnewproc.c']
sources = {f: (root/f).read_text() for f in files}
def noincludes(s):
    return re.sub(r'^#include[^\n]*\n', '', s, flags=re.M)
api = noincludes(sources[files[0]])
api = re.sub(r'AROS_LH1\(IPTR, TaskResourceControl,.*?22, Task\)',
             'IPTR rt_api(struct RTRequest *request, struct TaskResBase *TaskResBase)', api, flags=re.S)
rem = sources[files[2]].split('AROS_LH1(void, RemTask,', 1)[1].split('/* Called from DOS:RunCommand */', 1)[0]
rem = 'static void production_RemTask(struct Task *task, struct ExecBase *SysBase)\n' + rem[rem.index('{'):]
# The exact DOS early-exit block, with source-order assertions against teardown.
dos = sources[files[3]]
start = dos.index('    {\n        struct TaskResBase *TaskResBase = OpenResource("task.resource");')
end = dos.index('\n    D(bug("Deleting local variables', start)
assert start < dos.index('freeLocalVars(me, DOSBase);', start)
for marker in ['PRF_CLOSEINPUT', 'PRF_CLOSEOUTPUT', 'UnLoadSeg', 'CurrentDir', 'CloseLibrary']:
    assert marker in dos[end:], marker
assert rem.index('taskres_NotifyTasks') < rem.index('taskres_ResourceExit') < rem.index('Forbid()') < rem.index('AROS_CALL1')
block = dos[start:end]
fixture = pathlib.Path(__file__).with_name('fixture.c').read_text()
unit = fixture.replace('/* PUBLIC_HEADER */', noincludes(sources[files[1]]))
unit = unit.replace('/* PRODUCTION_API */', api).replace('/* PRODUCTION_REMTASK */', rem)
unit = unit.replace('/* PRODUCTION_DOS_EXIT */', block)
(a.output/'fixture.generated.c').write_text(unit)
cmd = [a.cc, '-std=gnu11', '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
       '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', str(a.output/'fixture.generated.c'), '-o', str(a.output/'fixture')]
(a.output/'compiler-command.json').write_text(json.dumps(cmd)+'\n')
c = subprocess.run(cmd, text=True, capture_output=True, timeout=60)
(a.output/'compile.log').write_text(c.stdout+c.stderr)
if c.returncode:
    print(c.stdout+c.stderr, end='')
    raise SystemExit(c.returncode)
r = subprocess.run([str(a.output/'fixture')], text=True, capture_output=True, timeout=30)
(a.output/'host.log').write_text(r.stdout+r.stderr)
print(r.stdout+r.stderr, end='')
if r.returncode: raise SystemExit(r.returncode)
if r.stderr: raise SystemExit('Unexpected stderr (including sanitizer diagnostics)')
result = json.loads(r.stdout)
assert result['status'] == 'PASS'
for case in ['clean_exit', 'close_failure_consumed', 'transfer', 'signal_identity',
             'wrong_context_rejected', 'raw_task_dos_transfer_rejected', 'backing_dependency', 'cycles_quarantined',
             'mutation_abort_quarantined', 'pending_port_quarantined',
             'remote_metadata_retained', 'remote_during_cleanup_retained']:
    assert result[case] is True, case
assert result['omitted_registration_leaks'] == 1
assert result['intentional_retained_allocations_before_test_teardown'] == 10
result.update(source_sha256={f:hashlib.sha256(s.encode()).hexdigest() for f,s in sources.items()},
              compiler_command=cmd, scope='Exact production RT implementation and RemTask wrapper; extracted DOS early-exit block; mocked UP host Exec/DOS services, not a guest boot or multicore execution')
(a.output/'resource-lifetime.json').write_text(json.dumps(result, indent=2)+'\n')
