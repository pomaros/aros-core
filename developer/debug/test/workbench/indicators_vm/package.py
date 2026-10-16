#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# SPDX-License-Identifier: APL-1.1
"""Package indicator programs into an explicit existing UP desktop base."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys
from types import SimpleNamespace
p=argparse.ArgumentParser()
for name in ['base','vm','build','sdk','output']:p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--runtime-from',type=Path,help='Prior package with hash-verified frozen runtime libraries')
p.add_argument('--control',type=Path,help='Built TilesControl installer for real Control icon integration witness')
a=p.parse_args()
for name in ['base','vm','build','sdk','output']:setattr(a,name,getattr(a,name).resolve())
base_provenance=json.loads((a.base/'provenance.json').read_text())
expected_sdk=Path(base_provenance['build'])/'bin/apple-aarch64/AROS'
if a.sdk!=expected_sdk.resolve():
    p.error('SDK must match the desktop base build provenance')
native_header=a.build/'bin/apple-aarch64/AROS/Developer/include/workbench/workbench.h'
runtime_header=a.sdk/'Developer/include/workbench/workbench.h'
def layout(path,name):
    match=re.search(rb'struct '+name.encode()+rb'\s*\{[^}]*\}',path.read_bytes())
    return match.group() if match else None
for name in ['AppIconQuery','AppIconGeometry']:
    if not layout(native_header,name) or layout(native_header,name)!=layout(runtime_header,name):
        p.error(name+' native/runtime headers differ; rebuild a matching program')
a.output.mkdir(parents=True,exist_ok=False)
shutil.copy2(Path(__file__),a.output/'package.py')
program=a.build/'bin/apple-aarch64/AROS/System/Indicator'
battery=a.build/'bin/apple-aarch64/AROS/System/BatterySource'
sound=a.build/'bin/apple-aarch64/AROS/System/SoundSource'
probe=a.build/'bin/apple-aarch64/AROS/Developer/Debug/Tests/workbench/indicator_probe'
esp=a.output/'esp';esp.mkdir()
for name in ['EFI','AROS']:shutil.copytree(a.base/name,esp/name)
startup=a.output/'Startup-Sequence'
original=(a.base/'Startup-Sequence').read_text()
assert original.count('G4Ready\n')==1
startup.write_text(original.replace('G4Ready\n','G4Ready\nMakeDir RAM:ENV\nAssign ENV: RAM:ENV\nRun >NIL: SYS:INDPROBE\n'))
files={'S/Startup-Sequence':startup,'System/Indicator':program,'INDPROBE':probe,'System/SoundSource':sound,'System/BatterySource':battery}
if a.control:
    control=a.control.resolve()
    assert control.is_file(),control
    files['System/TilesControl']=control
if a.runtime_from:
    prior=a.runtime_from.resolve()
    evidence=json.loads((prior/'esp/provenance.json').read_text())
    assert evidence['base_sha256']==hashlib.sha256((a.base/'AROS/GEN0.BND').read_bytes()).hexdigest()
    libraries={}
    for path,digest in evidence['program_inputs'].items():
        lib=prior/'inputs'/Path(path).name
        if not lib.name.endswith('.library'):continue
        assert hashlib.sha256(lib.read_bytes()).hexdigest()==digest
        name=re.sub(r'^(?:[0-9]+-)+','',lib.name)
        assert name not in libraries
        libraries[name]=lib
    assert 'workbench.library' in libraries and 'muimaster.library' in libraries
    for name,lib in sorted(libraries.items()):files['Libs/'+name]=lib
else:
    for lib in sorted((a.sdk/'Libs').glob('*.library')):files['Libs/'+lib.name]=lib
inputs=a.output/'inputs';inputs.mkdir()
for index,(dest,src) in enumerate(list(files.items())):
    kept=inputs/(str(index)+'-'+Path(dest).name);shutil.copy2(src,kept);files[dest]=kept
command=[sys.executable,str(a.vm/'tools/expand-system-image.py'),'--seed',str(a.base/'SYS.img'),'--output',str(esp/'SYS.img')]
for dest,src in files.items():command.extend(['--file',dest+'='+str(src)])
(a.output/'expand-command.json').write_text(json.dumps(command,indent=2)+'\n')
with (a.output/'expand.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
bundle=runpy.run_path(str(a.vm/'tools/arosbundle'))
old=(a.base/'AROS/GEN0.BND').read_bytes(); entries=bundle['verify_bundle'](old)
parts=a.output/'components';parts.mkdir()
args=SimpleNamespace(kernel=None,kernel_name=entries[0]['name'],kernel_memory_size=entries[0]['memory_size'],component=[],ramdisk=esp/'SYS.img',ramdisk_name='SYS')
for entry in entries:
    if entry['type']==bundle['TYPE']['ramdisk']:continue
    path=parts/str(entry['index']);path.write_bytes(old[entry['offset']:entry['offset']+entry['size']])
    if entry['type']==bundle['TYPE']['kernel']:args.kernel=path
    else:args.component.append((entry['name'],path))
data=bundle['create_bundle'](args);bundle['verify_bundle'](data)
(esp/'AROS/GEN0.BND').write_bytes(data);(esp/'AROS/BOOT0.ID').unlink()
with (a.output/'generation.log').open('w') as log:
    subprocess.run([sys.executable,str(a.vm/'tools/boot_generation.py'),'create','--bundle',str(esp/'AROS/GEN0.BND'),'--sequence','1','--slot','0','--state','accepted','--output',str(esp/'AROS/BOOT0.ID')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
manifest={'base':str(a.base),'base_sha256':hashlib.sha256(old).hexdigest(),
          'program_inputs':{str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in files.values()},
          'files':{str(path.relative_to(esp)):hashlib.sha256(path.read_bytes()).hexdigest() for path in esp.rglob('*') if path.is_file()},
          'scope':'UNRUN: borrowed UP desktop substrate, indicator executable from separate native build; no Apple hardware evidence'}
(esp/'provenance.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(esp)
