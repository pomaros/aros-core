#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual DeviceTree command under HOST public-resource facades, not a guest."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import tempfile
import time


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_CPU, (15, 15))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))


def main():
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=here.parents[3])
    parser.add_argument('--output', type=Path)
    parser.add_argument('--ref')
    parser.add_argument('--allow-dirty-diagnostic', action='store_true')
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    core = args.source.resolve()
    temporary = tempfile.TemporaryDirectory(prefix='devicetree-') if args.output is None else None
    out = Path(temporary.name) if temporary else args.output.resolve()
    if out == core or core in out.parents:
        raise ValueError('output must be outside production source')
    if not temporary:
        out.mkdir(parents=True, exist_ok=False)
    command = core / 'workbench/c/DeviceTree/main.c'
    inputs = (command, here / 'command.c', here / 'shim.h', Path(__file__).resolve())
    result = dict(result='INCOMPLETE', scope='Actual DeviceTree command; HOST public OF and DOS facades; no native ABI, resource tree or guest qualification', cases=[])
    started = time.monotonic()
    env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0:abort_on_error=1',
           'UBSAN_OPTIONS': 'halt_on_error=1'}

    def run(name, argv):
        completed = subprocess.run(argv, capture_output=True, timeout=25,
                                   preexec_fn=limits, env=env)
        (out / (name + '.stdout')).write_bytes(completed.stdout)
        (out / (name + '.stderr')).write_bytes(completed.stderr)
        result['cases'].append(dict(name=name, argv=argv, exit=completed.returncode))
        return completed

    try:
        head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=core, text=True).strip()
        if args.ref:
            expected = subprocess.check_output(['git', 'rev-parse', args.ref], cwd=core, text=True).strip()
            if head != expected:
                raise ValueError('source HEAD differs from selected ref')
        dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--',
            'workbench/c/DeviceTree', 'developer/debug/test/openfirmware_host'], cwd=core, text=True))
        result.update(source_commit=head, dirty_source=dirty)
        if dirty and not args.allow_dirty_diagnostic:
            raise ValueError('dirty source requires diagnostic mode')
        data = {p: p.read_bytes() for p in inputs}
        result['inputs'] = {str(p): hashlib.sha256(value).hexdigest() for p, value in data.items()}
        compiler = Path(shutil.which(args.cc) or args.cc).resolve()
        result['compiler'] = dict(path=str(compiler), sha256=hashlib.sha256(compiler.read_bytes()).hexdigest())
        snapshots = out / 'source'
        snapshots.mkdir()
        for p, value in data.items():
            (snapshots / p.name).write_bytes(value)
        (out / 'shim.h').write_bytes(data[here / 'shim.h'])
        for name in ('exec/types.h', 'exec/resident.h', 'dos/dos.h',
                     'proto/exec.h', 'proto/dos.h', 'proto/openfirmware.h'):
            p = out / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('#include "shim.h"\n')

        def compile_variant(name, production):
            source = out / (name + '-command.c')
            source.write_text(production)
            generated = out / (name + '-fixture.c')
            include = str(source).replace('\\', '\\\\').replace('"', '\\"')
            generated.write_text(data[here / 'command.c'].decode().replace('COMMAND_SOURCE', include))
            binary = out / name
            argv = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', '-g', '-I' + str(out),
                    str(generated), '-o', str(binary)]
            compiled = run(name + '-compile', argv)
            if compiled.returncode:
                raise RuntimeError(name + ' compile failed')
            return binary

        production = data[command].decode()
        binary = compile_variant('production', production)

        def scenario(mode, code=0):
            completed = run('scenario-' + str(mode), [str(binary), str(mode)])
            assert completed.returncode == code, (mode, completed.returncode, code)
            assert not completed.stderr, (mode, completed.stderr)
            text = completed.stdout.decode()
            assert '\x1b' not in text and 'SECRET' not in text, mode
            return text

        text = scenario(0)
        assert '"one", "two"' in text and '0x0000002a' in text
        text = scenario(1)
        assert '<present>' in text and 'first 256 of 257 bytes' in text and 'unsafe\\x1b' in text
        assert '[62 61 64]' in text and 'withheld' in text
        assert '[6f 6e 65 00 74 77 6f 00]' in scenario(2)
        assert 'node not found' in scenario(3, 10)
        assert 'property not found' in scenario(4, 10)
        text = scenario(5)
        assert '1 node(s)' in text and '/pci@0' not in text
        assert 'DEPTH' in scenario(6, 10)
        assert 'withheld' in scenario(7)
        assert 'no OpenFirmware' in scenario(8, 20)
        assert '2 node(s)' in scenario(9)
        scenario(10, 5)
        assert '/pci@0\n' in scenario(11)
        scenario(12, 10)
        assert '0x0000002a' in scenario(13)
        needle = 'OpenFirmwareBase = OpenResource("openfirmware.resource");'
        assert production.count(needle) == 1
        mutant = compile_variant('rom-tag-control', production.replace(needle,
            needle + '\n    FindResident("openfirmware.resource");'))
        completed = run('rom-tag-control', [str(mutant), '9'])
        prefix = 'Assertion failed: (!"runtime resource needs no ROM tag"), function FindResident, file '
        text = completed.stderr.decode()
        assert completed.returncode == -signal.SIGABRT and len(text.splitlines()) == 1 and text.startswith(prefix)
        if any(p.read_bytes() != value for p, value in data.items()):
            raise RuntimeError('source changed during replay')
        result.update(result='DIAGNOSTIC_PASS' if dirty else 'PASS', scenarios=14,
                      semantic_mutants_rejected=1)
    except (OSError, ValueError, AssertionError, RuntimeError, subprocess.SubprocessError) as error:
        result.update(result='FAIL', error=str(error))
    finally:
        result['elapsed_seconds'] = time.monotonic() - started
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('result', 'scope', 'elapsed_seconds')}))
    if temporary:
        temporary.cleanup()
    return 0 if result['result'] in ('PASS', 'DIAGNOSTIC_PASS') else 1


if __name__ == '__main__':
    raise SystemExit(main())
