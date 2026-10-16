#!/usr/bin/env python3
# SPDX-License-Identifier: APL-1.1
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
"""Compile the production yield/scheduler against a small UP host fixture.

This is a deterministic regression, not guest or hardware qualification.
--baseline removes the yield fix and must demonstrate equal-priority starvation.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[4]

def source(name):
    return re.sub(r'^\s*#include[^\n]*', '', (root / name).read_text(), flags=re.M)

reschedule = source('rom/exec/reschedule.c')
if args.baseline:
    reschedule = reschedule.replace('    FLAG_SCHEDQUANTUM_SET;', '')
unit = ((here / 'fixture.h').read_text() + '\n' +
        source('rom/exec/exec_platform.h') + '\n' + reschedule + '\n' +
        source('rom/kernel/kernel_scheduler.c') + '\n' +
        (here / 'test.c').read_text())
with tempfile.TemporaryDirectory(prefix='aros-reschedule-') as tmp:
    c = Path(tmp) / 'test.c'
    binary = Path(tmp) / 'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-value',
                    '-DBASELINE=' + str(int(args.baseline)), str(c),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
