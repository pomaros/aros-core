#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Exercise the production Storage report and snapshot requests with sanitizers."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='storage-report-') as temp:
    binary = Path(temp) / 'report'
    subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I'+str(here.parents[2] / 'compiler/include'),
                    str(here / 'report.c'), str(here / 'report_test.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
