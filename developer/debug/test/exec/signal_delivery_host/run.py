#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Execute Signal and its IPI callback with a deliberately delayed IPI queue."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
source = (here.parents[4] / 'rom/exec/signal.c').read_text()
callback = source[source.index('AROS_UFH3(IPTR, signal_hook,'):
                  source.index('/*\n * Called from RemTask()')]
body = source[source.index('#if defined(__mc68000)'):]
template = (here / 'host.c.in').read_text()
publish = '__AROS_ATOMIC_OR_L(task->tc_SigRecvd, signalSet);\n            if'
variants = {'production': body,
            'delayed-bits': body.replace(publish, 'if'),
            'redeliver-cleared-bits': body.replace('args[2] = 0;', 'args[2] = signalSet;')}
assert variants['production'] != variants['delayed-bits']
assert variants['production'] != variants['redeliver-cleared-bits']
rows = []
for name, tested in variants.items():
    file = args.output / (name + '.c')
    file.write_text(template.replace('@CALLBACK@', callback).replace('@SIGNAL@', tested))
    binary = file.with_suffix('')
    subprocess.run(['clang', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-variable',
                    '-fsanitize=address,undefined', '-g', str(file), '-o', str(binary)],
                   check=True, timeout=30)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    file.with_suffix('.log').write_text(result.stdout + result.stderr)
    rows.append({'variant': name, 'exit': result.returncode})
    if name == 'production':
        assert result.returncode == 0, result.stdout + result.stderr
    else:
        expected = ('task.tc_SigRecvd == mask' if name == 'delayed-bits'
                    else '!task.tc_SigRecvd && task.tc_State == TS_WAIT')
        assert result.returncode != 0 and expected in result.stderr, result.stderr
record = {'result': 'PASS', 'checks': rows,
          'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
          'scope': 'Actual Signal and signal_hook, deterministic delayed IPI queue and Exec mocks; ASan/UBSan, no real inter-CPU/IRQ/guest scheduling proof'}
(args.output / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
print('signal-delivery PASS:', rows)
