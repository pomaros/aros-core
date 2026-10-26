#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Execute actual platform-selection fragments from source and generated configure."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    records, inputs = [], {}
    for name in ['configure.in', 'configure']:
        raw = (ROOT / name).read_text()
        inputs[name] = hashlib.sha256(raw.encode()).hexdigest()
        start = raw.index('if test "$aros_target_variant" = ""; then')
        end = raw.index('\nif ! test "$aros_target_cpu32"', start)
        block = raw[start:end]
        (a.output / (name + '.platform.sh')).write_text(block)
        cases = [('smp', 'raspi', 'raspi', 'aarch64', 'raspi-aarch64'),
                 ('smp', 'pc', 'pc', 'x86_64', 'pc-x86_64'),
                 ('smp', 'apple', 'apple', 'aarch64', 'apple-aarch64'),
                 ('smp', 'virt', 'virt', 'riscv64', 'virt-riscv64'),
                 ('2712', 'raspi', 'raspi', 'aarch64', '2712-aarch64'),
                 ('', 'raspi', 'raspi', 'aarch64', 'raspi-aarch64'),
                 ('other', 'pc', 'pc', 'x86_64', 'pc-x86_64')]
        for variant, target_os, arch, cpu, expected in cases:
            env = dict(os.environ, aros_target_variant=variant, target_os=target_os,
                       aros_target_arch=arch, aros_target_cpu=cpu)
            run = subprocess.run(['sh', '-c', block + '\nprintf "%s" "$aros_target_platform"'],
                                 env=env, capture_output=True, text=True, timeout=5)
            assert run.returncode == 0 and run.stdout == expected, (name, variant, run.stdout, run.stderr)
            records.append(dict(source=name, variant=variant, os=target_os, platform=run.stdout))
        anchor = 'case "$aros_target_variant" in\n        smp)'
        assert block.count(anchor) == 1
        mutant = block.replace(anchor, 'case "$target_os" in\n        smp)')
        env = dict(os.environ, aros_target_variant='smp', target_os='raspi',
                   aros_target_arch='raspi', aros_target_cpu='aarch64')
        run = subprocess.run(['sh', '-c', mutant + '\nprintf "%s" "$aros_target_platform"'],
                             env=env, capture_output=True, text=True, timeout=5)
        assert run.returncode == 0 and run.stdout == 'smp-aarch64'
        records.append(dict(source=name, control='SMP treated as platform', actual=run.stdout))
        # Hosted support must survive regeneration from configure.in. Execute
        # its assignments for both accepted spellings, not a textual marker.
        limit = raw.index('Unsupported target CPU for darwin hosted flavour')
        start = raw.rfind('            *aarch64*|*arm64*)', 0, limit)
        assert start >= 0, name + ' lacks the Hosted AArch64 source branch'
        end = raw.index('            *)', start)
        hosted = raw[start:end]
        (a.output / (name + '.hosted.sh')).write_text(hosted)
        for cpu in ['aarch64', 'arm64']:
            env = dict(os.environ, target_cpu=cpu, toolchain_ar='ar', toolchain_ld='ld', toolchain_as='as')
            command = 'case "$target_cpu" in\n' + hosted + '\nesac\nprintf "%s|%s|%s|%s" "$aros_target_cpu" "$llvm_target_cpu" "$aros_macosx_version" "$aros_kernel_ld"'
            run = subprocess.run(['sh', '-c', command], env=env, capture_output=True, text=True, timeout=5)
            assert run.returncode == 0 and run.stdout == 'aarch64|AArch64|11.0|ld -arch arm64', run.stderr
            records.append(dict(source=name, hosted_cpu=cpu, values=run.stdout))
    (a.output / 'result.json').write_text(json.dumps(dict(result='PASS', cases=records, inputs=inputs,
        scope='Actual shell-selection fragments and source/generated parity; no full cross configure, toolchain link or runtime acceptance'), indent=2) + '\n')
    print('PASS source/generated platform matrix, SMP misclassification controls and Hosted AArch64 assignments')


if __name__ == '__main__':
    main()
