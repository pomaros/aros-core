#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Exercise the actual CRT registry header at modeled Exec/library boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--sanitizer', choices=['address,undefined', 'thread'], default='address,undefined')
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[4]
header = root / 'compiler/include/aros/crt_thread_context.h'
source = header.read_text()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
stubs = output / 'stub'
for name in re.findall(r'^#include <([^>]+)>', source, re.MULTILINE):
    if name == 'string.h':
        continue
    path = stubs / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('/* Host boundary types/functions are declared by host.c. */\n')
controls = {
    'borrowed-name': ('candidate->tc_Sem.ss_Link.ln_Name = candidate->tc_Name;',
                       'candidate->tc_Sem.ss_Link.ln_Name = (char *)CRT_THREAD_CONTEXTS_SEMNAME;',
                       'Assertion failed: (reg->tc_Sem.ss_Link.ln_Name==reg->tc_Name)'),
    'duplicate-publication': ('    if (!reg)\n    {\n        /* AddSemaphore',
                              '    if (TRUE)\n    {\n        /* AddSemaphore',
                              'Assertion failed: (ra && ra==rb && atomic_load(&allocations)==1)'),
    'duplicate-provider': ('if (current == hooks || __crt_tc_names_equal(current->tc_LibName, hooks->tc_LibName))',
                           'if (FALSE)',
                           'Assertion failed: (!CRTThreadContextRegister(&providers[0].hooks))'),
    'stale-hooks': ('stale = reg->tc_Generation != generation;',
                     'stale = FALSE; (void)generation;',
                     'Assertion failed: (p->instance == expected_instance)'),
    'lost-partial-capture': ('if (captured || e->ce_Context)', 'if (captured)',
                             'Assertion failed: (providers[0].cancelled==1 && providers[1].cancelled==1 && !providers[1].contexts)'),
    'inactive-captured': ('if (!h->tc_Active())\n                continue;',
                           'if (FALSE)\n                continue;',
                           'Assertion failed: (providers[n].opens == (success && providers[n].active ? 1 : 0))'),
    'missing-failure-leave': ('if (set->cs_Entries[j].ce_State == CRT_ENTRY_ENTERED)',
                              'if (FALSE)',
                              'Assertion failed: (set->cs_Entries[i].ce_State==CRT_ENTRY_NONE && !set->cs_Entries[i].ce_Context)'),
    'up-lookup-unprotected': ('    Disable();', '    /* missing namespace exclusion */',
                              'Assertion failed: (namespace_depth)'),
}
cases = [('up-production', source, False, None), ('smp-production', source, True, None)]
for name, (before, after, expected) in controls.items():
    assert source.count(before) == 1, (name, source.count(before))
    cases.append((name, source.replace(before, after), name != 'up-lookup-unprotected', expected))
records = []
for name, actual, smp, expected in cases:
    lane = output / name
    (lane / 'aros').mkdir(parents=True)
    (lane / 'aros/crt_thread_context.h').write_text(actual)
    binary = lane / 'host'
    command = ['clang', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-function', '-pthread', '-g', '-fsanitize=' + args.sanitizer,
               '-I' + str(lane), '-I' + str(stubs), str(here / 'host.c'), '-o', str(binary)]
    if smp:
        command.append('-DTEST_SMP')
    with (lane / 'compile.log').open('w') as log:
        process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    row = {'name': name, 'command': command, 'compile_exit': process.returncode,
           'run_exit': None, 'expected_cause': expected, 'pass': False}
    if process.returncode == 0:
        with (lane / 'run.log').open('w') as log:
            try:
                process = subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, timeout=20)
                row['run_exit'] = process.returncode
            except subprocess.TimeoutExpired:
                row['run_exit'] = 124
        row['pass'] = ((row['run_exit'] not in (0, 124) and expected in (lane / 'run.log').read_text())
                       if expected else row['run_exit'] == 0)
    records.append(row)
result = {
    'result': 'PASS' if all(record['pass'] for record in records) else 'FAIL',
    'sanitizer': args.sanitizer,
    'records': records,
    'inputs': {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
               for path in [header, here / 'host.c', Path(__file__)]},
    'scope': ('Actual public registry header with host-thread Tasks and modeled '
              'Exec namespace/semaphore/library boundaries. Concurrent publishers, '
              'creator/worker code pins, stale module replacement, cancellation, '
              'partial capture failure, reverse unwind, inactive providers, overflow '
              'and final library withdrawal. No pthread/posixc integration or guest claim.'),
}
(output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(result['result'])
for row in records:
    if not row['pass']:
        print(row['name'], 'compile', row['compile_exit'], 'run', row['run_exit'])
raise SystemExit(result['result'] != 'PASS')
