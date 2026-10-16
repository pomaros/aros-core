#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Compile unmodified parser/renderer with recording graphics calls, ASan/UBSan."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
here = Path(__file__).resolve().parent
root = here.parents[4]
source = root / 'workbench/system/Tiles'
for directory in ['graphics', 'proto']:
    (args.output / directory).mkdir()
for header in ['graphics/rastport.h', 'graphics/text.h', 'proto/graphics.h']:
    (args.output/header).write_text('#include "graphics_fixture.h"\n')
# Preserve the exact host fixture and production units used by this run.
for name in ['graphics_fixture.h', 'test.c', 'run.py']:
    (args.output/name).write_bytes((here/name).read_bytes())
for name in ['tiles_config.c', 'tiles_config.h', 'tiles_render.c', 'tiles_render.h']:
    (args.output/name).write_bytes((source/name).read_bytes())
command = ['clang', '-std=c99', '-Wall', '-Wextra', '-Werror', '-g', '-O1',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
           '-I'+str(args.output), str(args.output/'tiles_config.c'),
           str(args.output/'tiles_render.c'), str(args.output/'test.c'), '-o', str(args.output/'test')]
(args.output/'command.json').write_text(json.dumps(command, indent=2)+'\n')
compile_result = subprocess.run(command, capture_output=True, text=True, timeout=60)
(args.output/'compile.log').write_text(compile_result.stdout+compile_result.stderr)
if compile_result.returncode:
    print(compile_result.stderr)
    raise SystemExit(compile_result.returncode)
result = subprocess.run([str(args.output/'test')], capture_output=True, text=True, timeout=30)
(args.output/'host.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr, end='')
if result.returncode or result.stderr:
    raise SystemExit(result.returncode or 1)
verdict = json.loads(result.stdout)
assert verdict['result'] == 'PASS'
assert verdict['geometry_cases'] == 480 and verdict['parser_negatives'] >= 37
assert verdict['small_surface_cases'] == 3920 and verdict['fuzz_inputs'] == 10000
assert verdict['text_bearings_checked'] is True
assert verdict['hover_weather_checked'] is True
assert verdict['weather_surface_cases'] == 16200 and verdict['eyes_surface_cases'] == 5400
verdict['scope'] = 'Unmodified production parser/render code; host recording graphics fixture, not native pixels or guest proof'
verdict['source_sha256'] = {name: hashlib.sha256((args.output/name).read_bytes()).hexdigest()
                            for name in ['tiles_config.c', 'tiles_config.h', 'tiles_render.c', 'tiles_render.h']}
verdict['fixture_sha256'] = {name: hashlib.sha256((args.output/name).read_bytes()).hexdigest()
                             for name in ['run.py', 'test.c', 'graphics_fixture.h']}
(args.output/'parser-render.json').write_text(json.dumps(verdict, indent=2)+'\n')
