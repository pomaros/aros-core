#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL), Version 1.1.
"""Build DLTest and its module against a posix-check.py output, for a guest run.

The output holds DLTest, dltest.so, libvulkan.so (the Vulkan loader as a
module) with DLVulkan, and the posixc.library of the posix-check output, which the guest bundle must use as its boot component. Nothing is
installed; the guest run is a separate qualification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
for name in ('posix-check', 'sdk', 'generated', 'crosstools', 'collect-aros-dir', 'output'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
posix, sdk, generated = args.posix_check.resolve(), args.sdk.resolve(), args.generated.resolve()
tools, ld_dir, output = args.crosstools.resolve() / 'bin', args.collect_aros_dir.resolve(), args.output.resolve()
if output.exists():
    parser.error('output must not exist')
if json.loads((posix / 'result.json').read_text()).get('posix_library_link') != 'PASS':
    parser.error('the posix-check output did not pass')
output.mkdir(parents=True)
digest = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
commands = []


def run(command):
    commands.append(command)
    result = subprocess.run([str(c) for c in command], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit(' '.join(map(str, command)) + '\n' + result.stdout)
    return result.stdout


target = [tools / 'clang', '--target=aarch64-unknown-aros', '--sysroot=' + str(sdk), '-B' + str(ld_dir)]
flags = ['-mcmodel=large', '-fno-pic', '-O2', '-Wall', '-Werror']
# Import stubs for the dlfcn vectors, which the SDK predates.
stub_flags = ['-I' + str(posix / 'include'), '-I' + str(posix / 'module/include'),
              '-I' + str(ROOT / 'compiler/crt/posixc/include/aros/posixc'),
              '-I' + str(generated / 'include'), '-I' + str(posix / 'module'),
              '-isystem', str(sdk / 'include/aros/posixc'), '-isystem', str(sdk / 'include/aros/stdc')]
stubs = []
for name in ('dlopen', 'dlsym', 'dlclose', 'dlerror', 'dladdr'):
    obj = output / ('stub-' + name + '.o')
    run(target + ['-mcmodel=large', '-fno-pic', '-O2'] + stub_flags +
        ['-c', posix / 'module' / ('posixc_' + name + '_stub.c'), '-o', obj])
    stubs.append(obj)
archive = output / 'libaros_dlfcn.a'
run([tools / 'llvm-ar', 'rcs', archive] + stubs)

startup = output / 'dlmodule-startup.o'
run(target + flags + ['-I' + str(posix / 'include'), '-c',
    ROOT / 'compiler/startup/dlmodule-startup.c', '-o', startup])
module_obj = output / 'dltest-module.o'
run(target + flags + ['-c', HERE / 'dltest-module.c', '-o', module_obj])
module = output / 'dltest.so'
link = [ROOT / 'tools/dlmodule/dlmodule-link', '--cc', tools / 'clang']
link += ['--cc-arg=' + str(a) for a in target[1:]]
run(['python3'] + link + ['--startup', startup, '--nm', tools / 'llvm-nm', '-o', module, module_obj])

program = output / 'DLTest'
run(target + flags + ['-isystem', str(ROOT / 'compiler/crt/posixc/include/aros/posixc'),
    HERE / 'DLTest.c', archive, '-o', program])
# The Vulkan loader as a module, as wgpu and ash load it on Linux.
vulkan_inc = output / 'vulkan-include/hidd'
vulkan_inc.mkdir(parents=True)
shutil.copyfile(ROOT / 'workbench/hidds/vulkan/include/vulkan_hidd.h', vulkan_inc / 'vulkan_hidd.h')
vulkan_obj = output / 'vulkan_loader.o'
run(target + ['-mcmodel=large', '-fno-pic', '-O2', '-I' + str(vulkan_inc.parent), '-c',
    ROOT / 'workbench/hidds/vulkan/vulkan_loader.c', '-o', vulkan_obj])
vulkan = output / 'libvulkan.so'
run(['python3'] + link + ['--startup', startup, '--nm', tools / 'llvm-nm', '--export-prefix', 'vk',
    '-o', vulkan, vulkan_obj])
dlvulkan = output / 'DLVulkan'
run(target + flags + ['-isystem', str(ROOT / 'compiler/crt/posixc/include/aros/posixc'),
    HERE / 'DLVulkan.c', archive, '-o', dlvulkan])
for binary in (module, program, vulkan, dlvulkan):
    if run([tools / 'llvm-nm', '--undefined-only', binary]).strip():
        raise SystemExit(f'{binary} has undefined symbols')
shutil.copyfile(posix / 'posixc.library', output / 'posixc.library')
exports = run([tools / 'llvm-nm', module]).count(' T dltest_')
(output / 'result.json').write_text(json.dumps({
    'build': 'PASS', 'runtime': 'UNTESTED',
    'files': {p.name: digest(p) for p in (program, module, vulkan, dlvulkan, output / 'posixc.library')},
    'posix_check': str(posix), 'commands': [[str(c) for c in cmd] for cmd in commands]},
    indent=2) + '\n')
print(f'PASS: DLTest, dltest.so ({exports} text exports) and posixc.library in {output}')
