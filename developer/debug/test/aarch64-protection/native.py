#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Boot the controlled Exec MP1 proof using an already built Apple UP SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True,
                    help='external Apple UP build with apple-boot-payloads completed')
parser.add_argument('--integration', type=Path, required=True,
                    help='aros-apple-silicon checkout (read only)')
parser.add_argument('--toolchain', type=Path, required=True,
                    help='AROS clang prefix including working compiler-rt builtins')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--qemu', default='qemu-system-aarch64')
parser.add_argument('--make', default='gmake')
parser.add_argument('--firmware', type=Path,
                    default=Path('/opt/homebrew/share/qemu/edk2-aarch64-code.fd'))
args = parser.parse_args()
build, integration, toolchain, out = [p.resolve() for p in
    (args.build, args.integration, args.toolchain, args.output)]
core = Path(__file__).resolve().parents[4]
firmware = args.firmware.resolve()
qemu = shutil.which(args.qemu)
if not qemu:
    parser.error('QEMU executable not found')
if build == core or core in build.parents:
    parser.error('build output must be outside the source tree (MetaMake scans it)')
out.mkdir(parents=True, exist_ok=True)
commands = []


def run(command):
    command = [str(x) for x in command]
    commands.append(command)
    result = subprocess.run(command, capture_output=True, text=True)
    with (out / 'prepare.log').open('a') as log:
        log.write(shlex.join(command) + '\n' + result.stdout + result.stderr)
    result.check_returncode()


(out / 'prepare.log').write_text('')
efi = out / 'uefi/AROSBOOTAA64.EFI'
run([args.make, '-j2', '-C', integration / 'boot/arosboot',
     'BUILD=' + str(efi.parent), efi])
# Native SDK producer: this is not the separately locked hosted ABI corpus.
run([integration / 'tools/build-abi-probe', '--sdk', build / 'bin/apple-aarch64',
     '--sdk-tools', build / 'bin/darwin-aarch64/tools', '--crosstools', toolchain,
     out / 'ABIPROBE'])
run([integration / 'tools/make-fat12-image.py', '--abi-probe', out / 'ABIPROBE',
     out / 'sys.fixture'])
run([integration / 'tools/make-fat12-image.py', '--verify', '--abi-probe',
     out / 'ABIPROBE', out / 'sys.fixture'])
# Same component order as integration/tests/aros-core-bundle.sh, base profile.
components = [
    ('kernel.resource', 'boot/apple/Devs/kernel.resource'),
    ('exec.library', 'boot/apple/Libs/exec.library'),
    ('expansion.library', 'boot/apple/Libs/expansion.library'),
    ('pmgr.resource', 'Devs/pmgr.resource'),
    ('utility.library', 'Libs/utility.library'),
    ('rtkit.resource', 'Devs/rtkit.resource'),
    ('aros.library', 'Libs/aros.library'),
    ('bootloader.resource', 'Devs/bootloader.resource'),
    ('task.resource', 'Devs/task.resource'),
    ('FileSystem.resource', 'Devs/FileSystem.resource'),
    ('timer.device', 'boot/apple/Devs/timer.device'),
    ('partition.library', 'Libs/partition.library'),
    ('locale.library', 'Libs/locale.library'),
    ('stdc.library', 'Libs/stdc.library'),
    ('stdcio.library', 'Libs/stdcio.library'),
    ('posixc.library', 'Libs/posixc.library'),
    ('fat-handler', 'L/fat-handler'),
    ('appleboot.device', 'Devs/appleboot.device'),
    ('dos.library', 'Libs/dos.library'),
    ('iffparse.library', 'Libs/iffparse.library'),
]
root = build / 'bin/apple-aarch64'
stage2 = root / 'gen/arch/aarch64-apple/bootstrap/aros-apple-stage2-test.bin'
command = [integration / 'tools/arosbundle', 'create', '--output', out / 'AROS.BND',
           '--kernel', stage2, '--kernel-name', 'aros-apple-stage2',
           '--kernel-memory-size', '16777216']
for name, path in components:
    command += ['--component', name + '=' + str(root / 'AROS' / path)]
run(command + ['--ramdisk', out / 'sys.fixture'])
run([qemu, '-M', 'virt,acpi=off,dumpdtb=' + str(out / 'virt.dtb'),
     '-cpu', 'cortex-a72', '-m', '512', '-bios', firmware,
     '-device', 'ramfb', '-display', 'none'])
run(['fdtput', '-t', 's', out / 'virt.dtb', '/chosen', 'aros,mp1-proof', '1'])
wrapper = out / 'qemu-mp1'
wrapper.write_text('#!/bin/sh\nexec ' + shlex.quote(qemu) + ' -dtb ' +
                   shlex.quote(str(out / 'virt.dtb')) + ' "$@"\n')
wrapper.chmod(0o755)
files = [stage2, efi, firmware, toolchain / 'lib/generic/libclang_rt.builtins-aarch64.a',
         build / 'config/make.cfg', root / 'gen/config/target.cfg', out / 'ABIPROBE', out / 'sys.fixture',
         out / 'AROS.BND', out / 'virt.dtb']
files += [root / 'AROS' / path for _, path in components]
(out / 'inputs.json').write_text(json.dumps({str(p): hashlib.sha256(
    p.read_bytes()).hexdigest() for p in files}, indent=2) + '\n')
# UNIX socket paths have a small platform limit; copy durable evidence out
# of a short temporary run directory before removing it.
with tempfile.TemporaryDirectory(prefix='aros-mp1-', dir='/tmp') as temporary:
    env = os.environ.copy()
    env.update(RUNDIR=temporary, QEMU=str(wrapper), EXPECTED_MODULES='22',
               EXPECTED_ENTRY_MARKER='[B2Q] AROS startup contract PASS',
               EXPECTED_ENTRY_FAILURE_MARKER='[B2Q] FAIL:', TIMEOUT='30',
               UEFI_FIRMWARE=str(firmware), BUNDLE_IMAGE=str(out / 'AROS.BND'),
               EFI_IMAGE=str(efi))
    process = subprocess.Popen([str(integration / 'boot/arosboot/test-qemu.sh')],
                               env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True,
                               start_new_session=True)
    try:
        output, _ = process.communicate(timeout=60)
        exit_code = process.returncode
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            output, _ = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            output, _ = process.communicate()
        exit_code = 124
    (out / 'runner.log').write_text(output)
    shutil.copytree(temporary, out / 'run', dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('qmp.sock'))
# The guard proof reports through the kernel debug UART (serial.log) and the
# startup contract through semihosting; the proof precedes the contract.
semihost = ((out / 'run/serial.log').read_text(errors='replace') +
            (out / 'run/semihost.log').read_text(errors='replace'))
trace = (out / 'run/qemu.log').read_bytes()
markers = ['[MP1] (control) no-guard: access completed',
           '[MP1] PASS guard hit: task=mp1-guard',
           '[MP1] continuation: child resumed, parked, removed; parent running',
           '[B2A] portable ABI + emulated TLS + streams + W^X/unload PASS',
           '[B2Q] AROS startup contract PASS']
positions = [semihost.find(m) for m in markers]
passed = (exit_code == 0 and not trace and all(p >= 0 for p in positions) and
          positions == sorted(positions) and
          all(semihost.count(m) == 1 for m in markers) and
          not re.search(r'\b(ERROR|FAIL|PANIC|CRASH|FATAL)\b', semihost, re.I))
source_files = [Path(__file__).resolve()]
source_files += [core / 'arch/aarch64-all/kernel' / name for name in
                 ('setprotection.c', 'protection.h', 'guarded_stack.c',
                  'guarded_stack.h', 'guard_fault.h')]
source_files += [core / 'arch/aarch64-apple/kernel' / name for name in
                 ('setprotection.c', 'guarded_stack.c', 'guard_proof.c',
                  'guard_proof.h', 'guard_probe.S', 'runtime.c',
                  'runtime_vectors.S', 'coldstart_proof.c', 'page_allocator.h',
                  'freepages.c', 'mmakefile.src')]
source_files += [core / 'rom/dos/internalloadseg_elf.c']
result = {'pass': passed, 'exit_code': exit_code, 'trace_bytes': len(trace),
          'markers': dict(zip(markers, positions)),
          'core_commit': subprocess.check_output(
              ['git', '-C', str(core), 'rev-parse', 'HEAD'], text=True).strip(),
          'integration_commit': subprocess.check_output(
              ['git', '-C', str(integration), 'rev-parse', 'HEAD'], text=True).strip(),
          'commands': commands,
          'source_sha256': {str(p.relative_to(core)): hashlib.sha256(
              p.read_bytes()).hexdigest() for p in source_files},
          'scope': 'controlled Exec task, UP QEMU; every-task coverage remains open',
          'limits': 'no hardware, SMP, timer fairness or hosted ABI parity claim'}
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({key: result[key] for key in
                  ('pass', 'exit_code', 'trace_bytes', 'markers', 'scope')}, indent=2))
raise SystemExit(0 if passed else 1)
