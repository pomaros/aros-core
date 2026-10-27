#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License 1.1.
"""Actual stdc task-error owner and linked/module accessors; modeled native boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--tsan', action='store_true')
a = p.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[4]
owner = root / 'compiler/crt/stdc/__stdc_taskerrno.c'
accessor = root / 'compiler/crt/stdc/__stdc_geterrnoptr.c'
actual = owner.read_text()
actual = re.sub(r'^#include .*\n|^#define __TASK_NOLIBBASE__\n|^ADD2[^\n]*\n', '', actual, flags=re.M)
access = accessor.read_text()
start = access.index('    struct StdCBase *base =')
end = access.index('\n#endif', start)
body = '{\n' + access[start:end] + '\n}'
client = 'static int *client_errno(void)\n' + body
module = 'static int *module_errno(void)\n' + body
startup_source = root / 'compiler/crt/stdc/__stdc_startup.c'
startup_text = startup_source.read_text()
i = startup_text.index('        void __stdc_program_startup(')
j = startup_text.index('\n{', i)
k = startup_text.index('\n}', j) + 2
startup = 'static void __stdc_program_startup(jmp_buf exitjmp,int *errorptr)' + startup_text[j:k]
text = (here / 'host.c').read_text().replace('/* OWNER */', actual).replace('/* CLIENT_ACCESSOR */', client).replace('/* MODULE_ACCESSOR */', module).replace('/* PROGRAM_STARTUP */', startup)
controls = {
    'program-entry-reset-lost': ('    errno = 0;', '    /* lost program reset */', '*client_errno()==0 && a.startup_errorptr==&exit_status'),
    'module-private-cell': (module, 'static int *module_errno(void) { static int private_error;return &private_error; }', '*client_errno()==EINVAL && client_errno()==module_errno()'),
    'foreign-close-admitted': ('state->owner_id == GetETaskID(FindTask(NULL))', 'TRUE', '!error_can_close(&a)&&!error_can_close(&b)'),
    'opener-reference-lost': ('        state->references++;', '        /* lost opener reference */', 'task_slot&&*client_errno()==77'),
    'stale-task-slot': ('        SetTaskStorageSlot(error_slot, 0);', '        /* lost slot retirement */', 'clear binding before final disposal'),
    'reference-wrap': ('state->references == (ULONG)-1', 'FALSE', '!error_open(&refused)&&!refused.errno_state'),
}
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
rows = []
for name, before, after, cause in [('production', '', '', None)] + [(n, *v) for n, v in controls.items()]:
    if before:
        assert before in text, name
    source = out / (name + '.c')
    source.write_text(text.replace(before, after) if before else text)
    cmd = ['clang', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g', '-pthread', '-fsanitize=' + ('thread' if a.tsan else 'address,undefined'), str(source), '-o', str(out / name)]
    with (out / (name + '-compile.log')).open('w') as log:
        compiled = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=30)
    row = {'name': name, 'command': cmd, 'compile_exit': compiled.returncode, 'run_exit': None, 'expected_cause': cause, 'pass': False}
    if not compiled.returncode:
        with (out / (name + '.log')).open('w') as log:
            try:
                result = subprocess.run([str(out / name)], stdout=log, stderr=subprocess.STDOUT, timeout=15)
                row['run_exit'] = result.returncode
            except subprocess.TimeoutExpired:
                row['run_exit'] = 124
        row['pass'] = (row['run_exit'] not in (0, 124) and cause in (out / (name + '.log')).read_text()) if cause else row['run_exit'] == 0
    rows.append(row)
r = {'result': 'PASS' if all(x['pass'] for x in rows) else 'FAIL', 'records': rows,
     'inputs': {str(f.relative_to(root)): hashlib.sha256(f.read_bytes()).hexdigest() for f in [owner, accessor, startup_source, here / 'host.c', Path(__file__)]},
     'scope': 'Actual shared owner/provider functions and two actual accessor bodies. Modeled task storage, registry, native allocation and library open/close. No generated-module, static bootstrap, whole registry admission, linked guest or hardware acceptance.'}
(out / 'result.json').write_text(json.dumps(r, indent=2) + '\n')
print(r['result'])
for row in rows:
    if not row['pass']: print(row['name'], row['compile_exit'], row['run_exit'])
raise SystemExit(r['result'] != 'PASS')
