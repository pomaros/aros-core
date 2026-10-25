#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise actual template blitters against guarded input and pixel oracles."""
import argparse
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path,
                    default=HERE.parents[4] / 'rom/hidds/gfx/gfx_bitmapmemblit.c')
args = parser.parse_args()
source = args.source.read_text()
functions = []
for depth in (8, 16, 24, 32):
    begin = source.index(f'VOID BM__Hidd_BitMap__PutMemTemplate{depth}(')
    end = source.index('\n/****************************************************************', begin)
    functions.append(source[begin:end])
with tempfile.TemporaryDirectory(prefix='gfx-template-bounds-') as temp:
    temp = Path(temp)
    unit = temp / 'test.c'
    unit.write_text((HERE / 'harness.c').read_text().replace(
        '/* PRODUCTION_FUNCTIONS */', '\n'.join(functions)))
    executable = temp / 'test'
    subprocess.run(['clang', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(unit), '-o', str(executable)],
                   check=True)
    subprocess.run([str(executable)], check=True)
