#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Compile the settings.library schema parser and IFF codec with the host
compiler under ASan/UBSan and run the fixture over the real Font schema and
the default font.prefs, the items fixture over the Zune schema, the mask
fixture over the Asl schema, then the catalogue fixture over the shipped
Settings groups and rows."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core', type=Path, default=here.parents[3])
parser.add_argument('--cc', default='clang')
args = parser.parse_args()
core = args.core.resolve()
lib = core / 'workbench/libs/settings'
schema = core / 'workbench/prefs/Schemas/font.schema'
prefs = core / 'workbench/prefs/env-archive/default/SYS/font.prefs'
for path in (lib / 'settings_schema.c', lib / 'settings_iff.c', schema, prefs):
    if not path.is_file():
        sys.exit(f'settings-schema-host result=FAIL reason=missing {path}')
with tempfile.TemporaryDirectory(prefix='settings-host-') as work:
    binary = Path(work) / 'fixture'
    subprocess.run([args.cc, '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-Wall', '-Wextra', '-Werror',
                    '-I', str(lib), str(here / 'fixture.c'),
                    str(lib / 'settings_schema.c'), str(lib / 'settings_iff.c'),
                    '-o', str(binary)], check=True)
    run = subprocess.run([str(binary), str(schema), str(prefs)], capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr)
    if run.returncode != 0:
        sys.exit(run.returncode)
    # Items chunks: the real Zune schema over a dataspace image.
    items = Path(work) / 'items_fixture'
    subprocess.run([args.cc, '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-Wall', '-Wextra', '-Werror',
                    '-I', str(lib), str(here / 'items_fixture.c'),
                    str(lib / 'settings_schema.c'), str(lib / 'settings_iff.c'),
                    '-o', str(items)], check=True)
    run = subprocess.run([str(items), str(core / 'workbench/prefs/Schemas/zune.schema')],
                         capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr)
    if run.returncode != 0:
        sys.exit(run.returncode)
    # Masked records: two settings sharing the Asl ap_SizePosition byte.
    mask = Path(work) / 'mask_fixture'
    subprocess.run([args.cc, '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-Wall', '-Wextra', '-Werror',
                    '-I', str(lib), str(here / 'mask_fixture.c'),
                    str(lib / 'settings_schema.c'), str(lib / 'settings_iff.c'),
                    '-o', str(mask)], check=True)
    run = subprocess.run([str(mask), str(core / 'workbench/prefs/Schemas/asl.schema')],
                         capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr)
    if run.returncode != 0:
        sys.exit(run.returncode)
    # Screen scale: exact 25-percent grid and neighbouring mode fields.
    scale = Path(work) / 'scale_fixture'
    subprocess.run([args.cc, '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-Wall', '-Wextra', '-Werror',
                    '-I', str(lib), str(here / 'scale_fixture.c'),
                    str(lib / 'settings_schema.c'), str(lib / 'settings_iff.c'),
                    '-o', str(scale)], check=True)
    run = subprocess.run([str(scale), str(core / 'workbench/prefs/Schemas/screenmode.schema')],
                         capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr)
    if run.returncode != 0:
        sys.exit(run.returncode)
    # The Settings catalogue: the shipped groups and rows, page requests.
    window = core / 'workbench/prefs/settings'
    catalogue = Path(work) / 'catalogue_fixture'
    subprocess.run([args.cc, '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-Wall', '-Wextra', '-Werror',
                    '-I', str(window), str(here / 'catalogue_fixture.c'),
                    str(window / 'catalogue.c'), '-o', str(catalogue)], check=True)
    run = subprocess.run([str(catalogue), str(window)], capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr)
    sys.exit(run.returncode)
