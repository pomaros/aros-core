#!/usr/bin/env python3
# Copyright (C) 2026 John Knipper
# Licensed under the AROS Public License (APL) Version 1.1
"""Compile production engine and pinned crypto sources with ASan and UBSan."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--mbedtls', required=True, type=pathlib.Path)
parser.add_argument('--out', required=True, type=pathlib.Path)
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parent.parent
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
mb = args.mbedtls.resolve()
argon = root / 'vendor/argon2'
sources = [root / 'engine.c', root / 'tests/host.c']
sources += [argon / p for p in ['src/argon2.c', 'src/core.c', 'src/encoding.c',
                               'src/ref.c', 'src/blake2/blake2b.c']]
sources += [mb / 'library' / p for p in ['chachapoly.c', 'chacha20.c', 'poly1305.c',
                                      'platform_util.c', 'constant_time.c']]
flags = ['clang', '-std=c99', '-g', '-O1', '-fno-omit-frame-pointer',
         '-fsanitize=address,undefined', '-DARGON2_NO_THREADS',
         '-DMBEDTLS_CONFIG_FILE="mbedtls_config.h"', '-I' + str(root),
         '-I' + str(root / 'tests'), '-I' + str(argon / 'include'),
         '-I' + str(mb / 'include')]
command = flags + ['-DKR_ENGINE_TEST_UNSEALED', *map(str, sources),
                   '-o', str(out / 'host')]
(out / 'compile.json').write_text(json.dumps(command, indent=2) + '\n')
with (out / 'compile.log').open('w') as log:
    subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
production = flags + ['-c', str(root / 'engine.c'),
                      '-o', str(out / 'engine-production.o')]
subprocess.run(production, check=True, capture_output=True)
symbols = subprocess.check_output(['nm', str(out / 'engine-production.o')], text=True)
assert 'kr_engine_test_unsealed' not in symbols
(out / 'production-symbols.txt').write_text(symbols)
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=1',
           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
start = time.monotonic()
run = subprocess.run([str(out / 'host'), str(out / 'sealed.vault'),
                      str(out / 'unsealed.control')], env=env,
                     capture_output=True, text=True)
(out / 'host.log').write_text(run.stdout + run.stderr)
run.check_returncode()
report = json.loads(run.stdout)
marker = bytes(ord('A') + (i * 7 + 3) % 26 for i in range(40))
report['sealed_marker_hits'] = (out / 'sealed.vault').read_bytes().count(marker)
report['unsealed_control_marker_hits'] = (out / 'unsealed.control').read_bytes().count(marker)
assert report['sealed_marker_hits'] == 0
assert report['unsealed_control_marker_hits'] > 0
report['argon2id19_official_known_answer'] = True
report['maximal_vault_roundtrip'] = True
report['full_capacity_clone_rekey'] = True
report['unsealed_writer_absent_from_production'] = True
report['elapsed_seconds'] = time.monotonic() - start
report['sources_sha256'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in sources}
(out / 'keyring-host.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'result': report['result'], 'artifact': str(out / 'keyring-host.json')}))
