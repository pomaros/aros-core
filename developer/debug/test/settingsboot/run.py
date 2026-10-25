#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Execute production SettingsBoot through a POSIX DOS shim with sanitizers."""
from pathlib import Path
import argparse
import subprocess
import tempfile

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--afsplus', type=Path, required=True)
args = parser.parse_args()
core = here.parents[3]
with tempfile.TemporaryDirectory(prefix='settingsboot-host-') as temp:
    work = Path(temp)
    for name in ['dos/dos.h', 'dos/dosextens.h', 'exec/memory.h', 'proto/dos.h', 'proto/exec.h']:
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('/* DOS declarations supplied by mock.h. */\n')
    (work / 'filesystems').symlink_to(args.afsplus.resolve() / 'api', target_is_directory=True)
    flags = ['clang', '-std=c11', '-D_DARWIN_C_SOURCE', '-g', '-O1', '-fsanitize=address,undefined',
             '-fno-sanitize-recover=all', '-Wall', '-Wextra', '-I'+str(work), '-I'+str(here)]
    obj = work / 'command.o'
    subprocess.run(flags + ['-include', str(here / 'mock.h'), '-Dmain=SettingsBootMain', '-c',
                           str(core / 'workbench/c/SettingsBoot.c'), '-o', str(obj)], check=True)
    subprocess.run(flags + [str(here / 'fixture.c'), str(obj), '-o', str(work / 'fixture')], check=True)
    subprocess.run([str(work / 'fixture')], cwd=work, check=True)
