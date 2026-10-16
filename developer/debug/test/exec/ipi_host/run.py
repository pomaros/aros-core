#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Test freshly extracted production IPI lock/claim helpers with host mocks."""
import argparse
from pathlib import Path
import re
import resource
import subprocess
import tempfile

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core', type=Path, default=here.parents[4])
parser.add_argument('--cc', default='clang')
args = parser.parse_args()
core = args.core.resolve()
native = (core / 'arch/aarch64-native/kernel/kernel_ipi.c').read_text()


def between(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


locks = between(native, 'static inline uint64_t ipiq_lock(', '/* What the drain')
claims = between(native, 'struct CallIPIEntry *core_TryClaimCallIPI(',
                 'void core_CommitCallIPI(')
helpers = locks + claims
assert not re.search(r'\b(?:asm|__asm__)\b', helpers), 'privileged inline assembly in shared helpers'
assert 'core_FIQMasked' not in native, 'raw DAIF query returned'


def predicate(text):
    match = re.search(r'^#define CORE_IPI_MASKED\(mask\) .+$', text, re.M)
    assert match, 'transport predicate missing'
    return match.group(0) + '\n'


# Assertion failures in negative controls must not leave core dumps.
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
template = (here / 'harness.c.in').read_text()
with tempfile.TemporaryDirectory(prefix='aros-ipi-host-') as temporary:
    temp = Path(temporary)
    for backend, source, expected in [('native', native, '0x40')]:
        production = predicate(source) + helpers
        assert production.count('EXEC_IRQFIQ_RESTORE(daif);') == 1
        variants = {
            'production': production,
            # Regress the restore to lose the incoming masked state.
            'lost-mask': production.replace('EXEC_IRQFIQ_RESTORE(daif);',
                                           '(void)daif; EXEC_IRQFIQ_RESTORE(0);'),
        }
        for label, body in variants.items():
            cfile = temp / f'{backend}-{label}.c'
            binary = cfile.with_suffix('')
            cfile.write_text(template.replace('@PRODUCTION@', body))
            subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', f'-DEXPECTED_IPI_MASK={expected}',
                            str(cfile), '-o', str(binary)], check=True, timeout=30)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            if label == 'production':
                assert result.returncode == 0, result.stdout + result.stderr
                print(f'{backend}: production helpers PASS')
            else:
                assert result.returncode != 0 and 'Assertion' in result.stderr, (
                    f'{backend}: negative control {label} was not rejected\n' + result.stderr)
                print(f'{backend}: negative control {label} rejected')
print('PASS host contract only; no guest scheduling, real interrupts or inter-CPU concurrency exercised')
