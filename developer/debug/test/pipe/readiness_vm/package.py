#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Put the candidate handler/test into an existing headless SMP0 command fixture.

The baseline kernel must include the real DOS RunCommand("SYS:SMP0") hook.
Its source revision is recorded independently from the candidate's revision.
"""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import shutil
import subprocess
from types import SimpleNamespace


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base-esp', type=Path, required=True)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--vm-tools', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    source = Path(__file__).resolve().parents[5]
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no'], text=True)
    if dirty: p.error('commit candidate sources before packaging')
    if a.output.exists(): p.error('output must be new')
    sdk = a.build / 'bin/apple-aarch64/AROS'
    program = sdk / 'Developer/Debug/Tests/pipe/readiness'
    handler = sdk / 'L/pipe-handler'
    baseline = json.loads((a.base_esp / 'provenance.json').read_text())
    for path in (program, handler, a.base_esp/'ABIPROBE', a.base_esp/'AROS/GEN0.BND'):
        if not path.is_file(): p.error(f'missing input: {path}')
    a.output.mkdir(parents=True)
    shutil.copytree(a.base_esp/'EFI', a.output/'EFI')
    (a.output/'AROS').mkdir()
    subprocess.run(['python3', str(a.vm_tools/'make-fat12-image.py'),
                    '--abi-probe', str(a.base_esp/'ABIPROBE'), '--file', f'SMP0={program}',
                    '--file', f'PIPEHDLR={handler}', str(a.output/'SYS.img')], check=True)
    bundle = runpy.run_path(str(a.vm_tools/'arosbundle'))
    original = (a.base_esp/'AROS/GEN0.BND').read_bytes()
    entries = bundle['verify_bundle'](original)
    components = a.output/'components'; components.mkdir()
    args = SimpleNamespace(kernel=None, kernel_name=entries[0]['name'],
                           kernel_memory_size=entries[0]['memory_size'], component=[],
                           ramdisk=a.output/'SYS.img', ramdisk_name='SYS')
    for entry in entries:
        if entry['type'] == bundle['TYPE']['ramdisk']: continue
        path = components/str(entry['index'])
        path.write_bytes(original[entry['offset']:entry['offset']+entry['size']])
        if entry['type'] == bundle['TYPE']['kernel']: args.kernel = path
        else: args.component.append((entry['name'], path))
    data = bundle['create_bundle'](args)
    bundle['verify_bundle'](data)
    (a.output/'AROS/GEN0.BND').write_bytes(data)
    subprocess.run(['python3', str(a.vm_tools/'boot_generation.py'), 'create', '--bundle',
                    str(a.output/'AROS/GEN0.BND'), '--sequence', '1', '--slot', '0',
                    '--state', 'accepted', '--output', str(a.output/'AROS/BOOT0.ID')], check=True)
    inputs = [program, handler, a.base_esp/'AROS/GEN0.BND', a.output/'SYS.img',
              a.output/'AROS/GEN0.BND', a.output/'EFI/BOOT/BOOTAA64.EFI']
    provenance = {'candidate_revision': revision, 'baseline_guest': baseline,
                  'command_transport': 'SMP0 RunCommand slot; this run makes no fairness claim',
                  'inputs': {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}}
    (a.output/'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')
    print(json.dumps({'candidate_revision':revision, 'baseline_revision':baseline['source_revision']}, indent=2))

if __name__ == '__main__': main()
