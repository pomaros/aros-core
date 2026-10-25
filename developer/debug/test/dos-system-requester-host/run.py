#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual tag-value expression and utility bodies; no DOS/native execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess

HERE = Path(__file__).resolve().parent
CORE = HERE.parents[3]

def body(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

def expression(text):
    found = re.findall(r'\{\s*NP_WindowPtr\s*,(.*?)\}\s*,\s*/\*\s*9\s*\*/', text, re.S)
    assert len(found) == 1
    return found[0].strip()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--before-ref', required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    paths = [CORE / 'rom/dos/systemtaglist.c', CORE / 'rom/utility/applytagchanges.c',
             *(CORE / 'compiler/arossupport' / name for name in
               ('libnexttagitem.c', 'libfindtagitem.c', 'libgettagdata.c')),
             CORE / 'compiler/include/utility/tagitem.h',
             CORE / 'compiler/include/dos/dostags.h',
             CORE / 'rom/dos/createnewproc.c', CORE / 'compiler/pthread/pthread_create.c',
             HERE / 'test.c', Path(__file__).resolve()]
    inputs = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    source = paths[0].read_text()
    before = subprocess.check_output(['git', 'show', args.before_ref + ':rom/dos/systemtaglist.c'], cwd=CORE, text=True)
    assert source.index('{ NP_WindowPtr') < source.index('ti_Tag  = TAG_MORE')
    assert 'if (me->pr_WindowPtr == (APTR)-1)' in paths[7].read_text()
    assert 'NP_WindowPtr' not in paths[8].read_text()
    utilities = '\n'.join(body(paths[i].read_text(), marker) for i, marker in
        [(2, 'struct TagItem *LibNextTagItem('), (3, 'struct TagItem *LibFindTagItem('),
         (4, 'IPTR LibGetTagData(')])
    apply = paths[1].read_text().split('AROS_LIBFUNC_INIT', 1)[1].split('AROS_LIBFUNC_EXIT', 1)[0]
    constants = []
    for path, names in [(paths[5], ['TAG_END', 'TAG_IGNORE', 'TAG_MORE', 'TAG_SKIP', 'TAG_USER']),
                        (paths[6], ['NP_Dummy', 'NP_WindowPtr'])]:
        text = path.read_bytes().decode('latin-1')
        for name in names:
            matches = re.findall(r'^#define[ \t]+' + name + r'[ \t]+[^\n]+', text, re.M)
            assert len(matches) == 1
            constants.append(matches[0].split('/*')[0])
    fixture = (HERE / 'test.c').read_text()
    template = fixture.replace('/* ACTUAL_CONSTANTS */', '\n'.join(constants)).replace(
        '/* ACTUAL_UTILITIES */', utilities).replace('/* ACTUAL_APPLY */', '{\n' + apply + '\n}')
    rows = []
    for name, value, sanitized, good in [('plain', expression(source), False, True),
            ('sanitized', expression(source), True, True),
            ('predecessor', expression(before), False, False)]:
        c = args.out / (name + '.c')
        c.write_text(template.replace('/* ACTUAL_WINDOW_VALUE */', value))
        binary = args.out / name
        command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g']
        if sanitized:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        command += [str(c), '-o', str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        (args.out / (name + '-compile.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stderr
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10,
            env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0:halt_on_error=1',
                 'UBSAN_OPTIONS': 'halt_on_error=1'})
        (args.out / (name + '.log')).write_text(run.stdout + run.stderr)
        if good:
            assert run.returncode == 0 and not run.stderr, (run.returncode, run.stderr)
        else:
            assert run.returncode == -signal.SIGABRT and 'policy(1, 0x88, suppress) == (IPTR)-1' in run.stderr, run.stderr
        rows.append({'name': name, 'argv': command, 'returncode': run.returncode})
    assert inputs == {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    (args.out / 'result.json').write_text(json.dumps({'result': 'PASS_HOST_ONLY',
        'inputs': inputs, 'records': rows,
        'scope': 'Actual NP_WindowPtr value expression and tag utility bodies; process creation, DOS requester and pthread inheritance are source/model checks, not native execution'}, indent=2) + '\n')
    print('PASS 12 tag-policy checks plain/sanitized plus exact predecessor refusal')

if __name__ == '__main__':
    main()
