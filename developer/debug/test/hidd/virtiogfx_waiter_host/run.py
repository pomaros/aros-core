#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Run the production IRQ callback and waiter join with concurrent host threads."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--tsan', action='store_true')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
source = (here.parents[4] / 'rom/hidds/virtiogfx/transport.c').read_text()
body = source[source.index('static void gpu_wait_lock('):source.index('struct GPUHeader ')]
body = body.replace('__asm__ volatile("dsb sy" ::: "memory");',
                    'atomic_thread_fence(memory_order_seq_cst);')
file = a.output / 'host.c'
file.write_text((here / 'host.c.in').read_text().replace('@PRODUCTION@', body))
binary = a.output / 'host'
command = ['clang', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-pthread',
           '-fsanitize=' + ('thread' if a.tsan else 'address,undefined'),
           '-g', str(file), '-o', str(binary)]
subprocess.run(command, check=True, timeout=30)
r = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
(a.output / 'run.log').write_text(r.stdout + r.stderr)
print(r.stdout + r.stderr, end='')
record = {'result': 'PASS' if r.returncode == 0 else 'FAIL', 'exit': r.returncode,
          'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
          'scope': 'Production IRQ and waiter helpers with concurrent threads; Exec lock resource, Signal and IRQ masking modeled. No DMA/reset/interrupt removal or guest qualification.',
          'sanitizer': 'TSan' if a.tsan else 'ASan/UBSan'}
(a.output / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
raise SystemExit(r.returncode)
