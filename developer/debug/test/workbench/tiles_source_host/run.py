#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
"""Compile the actual asynchronous source module with message ownership fixtures."""
from pathlib import Path
import os, re, subprocess, sys, tempfile
here=Path(__file__).resolve().parent
root=here.parents[4]
source=root/'workbench/system/Tiles'
with tempfile.TemporaryDirectory(prefix='tiles-sources-') as directory:
    out=Path(directory)
    code=(source/'source.h').read_text()+'\n'+(source/'source.c').read_text()
    code=re.sub(r'^#include .*$', '', code, flags=re.M)
    (out/'test.c').write_text((here/'fixture.h').read_text()+'\n'+code+'\n'+(here/'test.c').read_text())
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(out/'test.c'),'-o',str(out/'test')],check=True)
    subprocess.run([str(out/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks='+('0' if sys.platform=='darwin' else '1')})
