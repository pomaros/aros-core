#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Exercise production vfork lifetime code with modeled Task execution."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
fixture = Path(__file__).resolve().parent
root = fixture.parents[4]
posixc = root / 'compiler/crt/posixc'
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
inputs = [posixc / '__vfork.c', posixc / '__vfork.h', fixture / 'create.c', Path(__file__)]
source = inputs[0].read_text()
header = inputs[1].read_text(encoding='latin-1')


def section(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def native(name, signature):
    path = root / 'rom/exec' / (name + '.c')
    inputs.append(path)
    text = path.read_text()
    return signature + '\n' + text[text.index('{\n    AROS_LIBFUNC_INIT'):text.rindex('} /*') + 1]


waitpid = posixc / 'waitpid.c'
inputs.append(waitpid)
waitcode = waitpid.read_text()
replacements = {
    'VFORK_DATA': section(header, 'struct vfork_data\n{', '\npid_t __vfork'),
    'CREATE_CHILD': section(source, 'static void parent_createchild(struct vfork_data *udata)\n{',
                            '\nstatic void parent_enterpretendchild'),
    'EXIT_CONTROLLED': section(source,
                              'static __attribute__((noinline)) void __vfork_exit_controlled_stack(struct vfork_data *udata)\n{',
                              '\nstatic void parent_createchild'),
    'CHILD_WAIT': native('childwait', 'IPTR ChildWait(ULONG tid)'),
    'CHILD_FREE': native('childfree', 'void ChildFree(ULONG tid)'),
    'CHILD_STATUS': native('childstatus', 'ULONG ChildStatus(ULONG tid)'),
    'WAITPID': ('static int host_waitpid(int pid,int *status,int options)\n' +
                waitcode[waitcode.index('{\n    struct ETask *et;'):waitcode.rfind('}') + 1]),
}
text = (fixture / 'create.c').read_text()
for marker, implementation in replacements.items():
    placeholder = '/* ' + marker + ' */'
    assert text.count(placeholder) == 1, marker
    text = text.replace(placeholder, implementation)

# Every control must compile and fail for its named cause, rather than for an
# unrelated assertion or sanitizer error.
controls = {
    'signal-leak': ('FreeSignal(udata->parent_signal);', '/* missing signal cleanup */',
                    'Assertion failed: (!signals&&!allocations&&!running&&!pending_exit)'),
    'nested-flags': ('PosixCBase->flags = old_flags;',
                     'PosixCBase->flags &= ~VFORK_PARENT; (void)old_flags;',
                     'Assertion failed: (base.flags==old_flags)'),
    'no-reap': ('ChildFree(udata->child_pid);', '/* missing reap */',
                'Assertion failed: (records==1)'),
    'no-join': ('ChildWait(udata->child_pid);', '/* missing join */',
                'Assertion failed: (e->dead)'),
    'late-uid': ('ChildWait(udata->child_pid);', 'ChildWait(GetETaskID(udata->child));',
                 'AddressSanitizer: heap-use-after-free'),
    'return-after-free': ('vfork_longjmp(env, child_pid);',
                          'vfork_longjmp(env, GetETaskID(udata->child)); (void)child_pid;',
                          'AddressSanitizer: heap-use-after-free'),
}
cases = [('production', text, None)]
for name, (before, after, expected) in controls.items():
    assert before in text, name
    cases.append(('control-' + name, text.replace(before, after), expected))
records = []
for name, actual, expected in cases:
    path = output / (name + '.c')
    path.write_text(actual)
    binary = output / name
    command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-g', '-fsanitize=address,undefined',
               str(path), '-o', str(binary)]
    with (output / (name + '-build.log')).open('w') as log:
        process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    records.append({'name': name + '-build', 'command': command,
                    'exit': process.returncode, 'pass': process.returncode == 0})
    if process.returncode:
        continue
    logpath = output / (name + '.log')
    with logpath.open('w') as log:
        process = subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, timeout=20)
    passed = (process.returncode != 0 and expected in logpath.read_text()) if expected else process.returncode == 0
    records.append({'name': name, 'exit': process.returncode, 'expected_cause': expected, 'pass': passed})
result = {
    'result': 'PASS' if all(record['pass'] for record in records) else 'FAIL',
    'records': records,
    'inputs': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs},
    'scope': ('Actual complete parent_createchild and controlled-stack return, Exec '
              'ChildWait/ChildFree/ChildStatus UP bodies and waitpid. Real intrusive '
              'ETask lists; modeled Task execution/signalling, disposal, DOS creation '
              'and startup restoration. 640 attempts, immediate and delayed child '
              'exit, return after child and shared-data destruction. '
              'No launcher execution, guest, assembly or SMP qualification.'),
}
(output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(result['result'])
raise SystemExit(result['result'] != 'PASS')
