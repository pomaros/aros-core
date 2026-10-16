#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Check generated MCC shutdown when library opening fails before class init."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--genmodule', type=Path, required=True)
a = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    config = root / 'probe.conf'
    config.write_text('##begin config\nbasename Probe\nversion 1.0\nsuperclass MUIC_Group\n##end config\n')
    subprocess.run([str(a.genmodule.resolve()), '-c', str(config), '-d', str(root),
                    'writefiles', 'Probe', 'mcc'], check=True)
    generated = (root / 'Probe_start.c').read_text()
    match = re.search(r'static void MCC_Probe_Shutdown\([^\n]+\)\n\{.*?\n\}', generated, re.S)
    if not match:
        raise SystemExit('missing generated MCC shutdown')
    source = root / 'shutdown.c'
    source.write_text('''#include <assert.h>
#include <stddef.h>
struct State { void *cl; };
typedef struct State *LIBBASETYPEPTR;
#define Probe_CLASSPTR_FIELD(b) ((b)->cl)
static unsigned calls;
static void MUI_DeleteCustomClass(void *cl) { assert(cl); ++calls; }
''' + match.group(0) + '''
int main(void) {
 struct State state = {0};
 MCC_Probe_Shutdown(&state); assert(calls == 0);
 state.cl = &state;
 MCC_Probe_Shutdown(&state); assert(calls == 1 && state.cl == NULL);
 MCC_Probe_Shutdown(&state); assert(calls == 1);
 return 0;
}
''')
    binary = root / 'shutdown'
    subprocess.run(['clang', '-fsanitize=address,undefined', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('genmodule-mui result=PASS failed-open=0 live-class=1 repeat-shutdown=0')
