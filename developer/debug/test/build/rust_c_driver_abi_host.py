#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Execute the emitted C-driver command with real Clang; inspect RISC-V ELF ABI."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path, required=True, help='Clang with the RISC-V backend')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    source = (ROOT / 'tools/crosstools/rust/mmakefile.src').read_text()
    commands = re.findall(r"\$\(ECHO\) '(exec \$\(RUST_TARGET_CC\)[^'\n]+)'", source)
    assert len(commands) == 1
    emitted = commands[0].replace('$$', '$')
    bindings = {'$(RUST_TARGET_CC)': shlex.quote(str(args.cc.resolve())) + ' --target=riscv64-none-elf',
                '$(RUST_TARGET_CCFLAGS)': '-march=rv64gc -mabi=lp64d',
                '$(TARGET_SYSROOT)': ''}
    variants = {'actual': emitted,
                'caller-overrides-abi': 'exec $(RUST_TARGET_CC) $(RUST_TARGET_CCFLAGS) $(TARGET_SYSROOT) "$@"'}
    records = []
    for name, command in variants.items():
        directory = args.output / name
        directory.mkdir()
        for key, value in bindings.items():
            command = command.replace(key, value)
        assert '$(' not in command
        driver = directory / 'aros-cc'
        driver.write_text('#!/bin/sh\n' + command + '\n')
        driver.chmod(0o755)
        unit = directory / 'unit.c'
        unit.write_text('double value(double input) { return input + 1.0; }\n')
        obj = directory / 'unit.o'
        argv = [str(driver), '-mabi=lp64', '-ffreestanding', '-c', str(unit), '-o', str(obj)]
        result = subprocess.run(argv, capture_output=True, text=True, timeout=20)
        (directory / 'compile.log').write_text(result.stdout + result.stderr)
        result.check_returncode()
        data = obj.read_bytes()
        assert data[:6] == b'\x7fELF\x02\x01'
        assert struct.unpack_from('<H', data, 18)[0] == 243
        flags = struct.unpack_from('<I', data, 48)[0]
        records.append(dict(variant=name, argv=argv, elf_flags=flags,
                            double_float_abi=(flags & 6) == 4,
                            object_sha256=hashlib.sha256(data).hexdigest()))
    passed = records[0]['double_float_abi'] and not records[1]['double_float_abi']
    (args.output / 'result.json').write_text(json.dumps(dict(result='PASS' if passed else 'FAIL',
        cases=records, recipe_sha256=hashlib.sha256(source.encode()).hexdigest(),
        scope='Actual emitted driver command with bound variables; real RISC-V C object ABI, not an AROS link or execution'), indent=2) + '\n')
    print(records)
    raise SystemExit(not passed)


if __name__ == '__main__':
    main()
