#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Check launcher/proxy ownership handoff using actual production functions."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--tsan', action='store_true')
args = parser.parse_args()
fixture = Path(__file__).resolve().parent
posixc = fixture.parents[4] / 'compiler/crt/posixc'
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)


def section(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


proxy = (posixc / '__exec.c').read_text()
source = (posixc / '__vfork.c').read_text()
header = (posixc / '__vfork.h').read_text(encoding='latin-1')
launcher = section(source, 'LONG launcher()', '/* This can be good for debugging */')
# Schedule the real child after shared-data destruction at both final waits.
# This changes only the fixture scheduler, not production lifetime operations.
assert launcher.count('Wait(1UL << child_signal);') == 2
launcher = launcher.replace('Wait(1UL << child_signal);',
                            'terminal_wait = 1; Wait(1UL << child_signal);')
replacements = {
    'VFORK_DATA': section(header, 'struct vfork_data\n{', '\npid_t __vfork'),
    'LAUNCHER': launcher,
    'PARENT_RETURN': section(source,
                            'static __attribute__((noinline)) void __vfork_exit_controlled_stack(struct vfork_data *udata)\n{',
                            '\nstatic void parent_createchild'),
    'PREPARE_PROXY': section(proxy,
                             'static APTR __exec_prepare_pretend_child(char *filename2, char *const argv[], char *const envp[],\n        struct PosixCIntBase *PosixCBase)\n{',
                             '\nstatic APTR __exec_prepare_regular'),
}
text = (fixture / 'handshake.c').read_text()
for marker, implementation in replacements.items():
    placeholder = '/* ' + marker + ' */'
    assert text.count(placeholder) == 1, marker
    text = text.replace(placeholder, implementation)
text = text.replace('STRPTR', 'char *')

controls = {
    'post-publish': ('terminal_wait = 1; Wait(1UL << child_signal);',
                     'terminal_wait = 1; Wait(1UL << child_signal); assert(udata->parent_state==8);',
                     'AddressSanitizer: heap-use-after-free'),
    'premature-close': ('terminal_wait = 1; Wait(1UL << child_signal);',
                        '/* removed final acknowledgement */',
                        'Assertion failed: (!atomic_load(&borrowed)||atomic_load(&restored))'),
    'post-free-pid': ('vfork_longjmp(env, child_pid);',
                      '(void)child_pid; vfork_longjmp(env, GetETaskID(udata->child));',
                      'AddressSanitizer: heap-use-after-free'),
    'lost-status': ('etask->et_Result1 = ret;', '(void)ret;',
                    'Assertion failed: (etask.et_Result1==(LONG)(intptr_t)ret)'),
    'no-failure-ack': ('Wait(1UL << udata->parent_signal);',
                       '/* missing failure acknowledgement */',
                       'Assertion failed: (errno==ENOENT && !u->child_executed && u->child_state==CHILD_STATE_EXEC_FAILED_READY)'),
    'no-failure-reset': ('udata->child_executed = 0;', '/* missing failure reset */',
                         'Assertion failed: (errno==ENOENT && !u->child_executed && u->child_state==CHILD_STATE_EXEC_FAILED_READY)'),
    'late-launcher-uid': ('udata->child_pid = GetETaskID(this);', '/* delayed capture */',
                          'Assertion failed: (u->child_state==CHILD_STATE_SETUP_FAILED && u->child_pid==91 && u->child_errno==ENOMEM)'),
}
cases = [('production', text, None)]
for name, (before, after, expected) in controls.items():
    # ASan owns deliberate UAF controls; the others also run with TSan.
    if args.tsan and name in ('post-publish', 'post-free-pid'):
        continue
    assert before in text, name
    actual = text.replace(before, after)
    if name == 'premature-close':
        actual = actual.replace('premature_control,', 'premature_control=1,')
    if name == 'late-launcher-uid':
        actual = actual.replace('udata->child_posixcbase = PosixCBase;',
                                'udata->child_pid = GetETaskID(this); udata->child_posixcbase = PosixCBase;')
    cases.append(('control-' + name, actual, expected))

records = []
for name, actual, expected in cases:
    path = output / (name + '.c')
    path.write_text(actual)
    binary = output / name
    command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-g', '-pthread',
               '-fsanitize=' + ('thread' if args.tsan else 'address,undefined'),
               str(path), '-o', str(binary)]
    with (output / (name + '-build.log')).open('w') as log:
        process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    records.append({'name': name + '-build', 'command': command,
                    'exit': process.returncode, 'pass': process.returncode == 0})
    if process.returncode:
        continue
    logpath = output / (name + '.log')
    with logpath.open('w') as log:
        try:
            process = subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, timeout=20)
            code = process.returncode
        except subprocess.TimeoutExpired:
            code = 124
    passed = (code not in (0, 124) and expected in logpath.read_text()) if expected else code == 0
    records.append({'name': name, 'exit': code, 'expected_cause': expected, 'pass': passed})
inputs = [posixc / '__exec.c', posixc / '__vfork.c', posixc / '__vfork.h',
          fixture / 'handshake.c', Path(__file__)]
result = {
    'result': 'PASS' if all(record['pass'] for record in records) else 'FAIL',
    'records': records,
    'inputs': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs},
    'scope': ('Complete actual launcher, failed-exec proxy and parent-return C '
              'functions; 128 failed execs before each final exit/retry. '
              'Modeled Exec signals, library Open/Close, exec body, parent '
              'restoration and scheduler. Six real launcher setup failures, including descriptor and cwd capture. '
              'No CreateNewProc, assembly or guest qualification.'),
}
(output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(result['result'])
raise SystemExit(result['result'] != 'PASS')
