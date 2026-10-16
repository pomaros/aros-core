#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>. SPDX-License-Identifier: APL-1.1
"""Compile actual IconList snippets; retain sanitizer and mutation evidence."""
import argparse, hashlib, json, pathlib, re, subprocess
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[4]
p = argparse.ArgumentParser(); p.add_argument('--output', required=True); args = p.parse_args()
out = pathlib.Path(args.output); out.mkdir(parents=True, exist_ok=False)
source = ROOT / 'workbench/system/Wanderer/Classes/iconlist.c'
s = source.read_text(encoding='latin1')
def between(start, end):
    assert s.count(start) == 1, ('nonunique start', start)
    a = s.index(start); b = s.index(end, a)
    return s[a:b]
lasso = between('                iconrect.MinX = node->ie_IconX;', '\n            }\n        }\n        if (update_entry)')
clear = between('                rect.MinX += _mleft(obj) + (data->update_entry->ie_IconX', '\n                clip = MUI_AddClipping')
neighbor = between('                        rect2.MinX += _mleft(obj) - data->icld_ViewX', '\n                        if (RectAndRect(&rect, &rect2))')
prefix = (HERE / 'fixture.c').read_text()
parts = {'lasso': lasso, 'clear': clear, 'neighbor': neighbor}
results = {}
for name in ['production', 'lasso', 'clear', 'neighbor']:
    snippets = dict(parts)
    if name != 'production':
        snippets[name], count = re.subn(r'!\s*(?:node|entry|data->update_entry)->ie_GridWidth\s*&&\s*', '', snippets[name])
        assert count == 1, (name, 'missing unique guard', count)
    code = prefix
    for key, body in snippets.items(): code = code.replace('/* EXTRACT_' + key + ' */', body)
    path = out / (name + '.c'); path.write_text(code)
    command = ['clang', '-std=c99', '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(path), '-o', str(out/name)]
    build = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (out/(name+'.build.log')).write_text(build.stdout+build.stderr)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(out/name)], capture_output=True, text=True, timeout=20)
    (out/(name+'.run.log')).write_text(run.stdout+run.stderr)
    if name == 'production': assert run.returncode == 0 and run.stdout == 'tiles-slot-bounds PASS\n' and not run.stderr, run
    else: assert run.returncode != 0 and 'Assertion failed:' in run.stderr, run
    results[name] = {'exit':run.returncode,'source_sha256':hashlib.sha256(code.encode()).hexdigest(),'command':command}
(out/'iconlist.c').write_bytes(source.read_bytes())
(out/'result.json').write_text(json.dumps({'result':'PASS','production_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'runs':results},indent=2)+'\n')
print('PASS: actual lasso/clear/neighbor snippets; three independent old-centering mutants rejected')
