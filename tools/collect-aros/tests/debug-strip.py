#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER.
# SPDX-License-Identifier: APL-1.1
"""Exercise final debug stripping with real AArch64 ELF and embedded bitcode."""
import argparse
import json
from pathlib import Path
import struct
import subprocess


def sections(path):
    data = path.read_bytes()
    assert data[:6] == b'\x7fELF\x02\x01', 'expected little-endian ELF64'
    assert struct.unpack_from('<HH', data, 16) == (1, 183), 'expected AArch64 ET_REL'
    assert data[7:9] == bytes((15, 1)), 'expected AROS ABI'
    offset = struct.unpack_from('<Q', data, 40)[0]
    size, count, names_index = struct.unpack_from('<HHH', data, 58)
    rows = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * size)
            for i in range(count)]
    names = rows[names_index]
    strings = data[names[4]:names[4] + names[5]]
    return {strings[r[0]:].split(b'\0', 1)[0].decode():
            (r, data[r[4]:r[4] + r[5]] if r[1] != 8 else b'') for r in rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--collect-aros', type=Path, required=True)
    parser.add_argument('--clang', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(exist_ok=False, parents=True)
    source = output / 'probe.c'
    source.write_text('volatile long value = 42; volatile long *reference = &value;\n'
                      'int main(void) { return (int)*reference; }\n')
    subprocess.run([str(args.clang.resolve()), '--target=aarch64-unknown-aros',
                    '-g', '-fembed-bitcode', '-fno-pic', '-c', str(source),
                    '-o', str(output / 'probe.o')], check=True)
    reference = None
    results = []
    for name, flags, stripped in (
            ('unstripped', [], False), ('long', ['--strip-debug'], True),
            ('short', ['-S'], True), ('incremental', ['-r', '--strip-debug'], True)):
        binary = output / name
        subprocess.run([str(args.collect_aros.resolve()), *flags, '-o', str(binary),
                        str(output / 'probe.o')], check=True)
        table = sections(binary)
        debug = any(n.startswith(('.debug_', '.rela.debug_')) for n in table)
        assert debug != stripped, f'{name}: incorrect debug section retention'
        assert '.symtab' in table, f'{name}: lost symbol table'
        assert any(r[0][1] == 4 and r[0][5] for r in table.values()), 'lost relocations'
        allocated = {n: (r[0][1], r[0][5], r[1]) for n, r in table.items()
                     if r[0][2] & 2}
        if reference is None:
            reference = allocated
        elif name != 'incremental':
            assert allocated == reference, f'{name}: changed allocated data'
        results.append({'case': name, 'debug_present': debug, 'verdict': 'PASS'})
    (output / 'result.json').write_text(json.dumps({'verdict': 'PASS', 'cases': results,
        'scope': 'host cross-linking; target runtime untested'}, indent=2) + '\n')
    print('PASS: four real AArch64 debug-strip links; symbols and relocations retained')


if __name__ == '__main__':
    main()
