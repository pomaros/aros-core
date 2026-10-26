#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise actual RUST_CARGO with real offline Cargo and a fixed custom sysroot."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--std-build', type=Path, required=True)
p.add_argument('--cargo', type=Path, required=True)
p.add_argument('--make', default=shutil.which('gmake') or shutil.which('make'))
p.add_argument('--output', type=Path)
a = p.parse_args()
core = Path(__file__).resolve().parents[4]
std, cargo = a.std_build.resolve(), a.cargo.resolve()
if json.loads((std/'result.json').read_text()).get('std_compile') != 'PASS':
    p.error('std-build must record std_compile PASS')
if a.output and a.output.exists(): p.error('output must be new')
with tempfile.TemporaryDirectory(prefix='cargo-sdk-generation-') as tmp:
    out = a.output.resolve() if a.output else Path(tmp)
    out.mkdir(parents=True, exist_ok=True)
    sdk = out/'sdk'; (sdk/'bin').mkdir(parents=True); (sdk/'share').mkdir()
    target = sdk/'share/aarch64-unknown-aros.json'
    shutil.copyfile(std/'aarch64-unknown-aros.json', target)
    for name, command in [('rustc', [str(std/'rustc')]), ('cargo', [str(cargo)])]:
        script = sdk/'bin'/name
        script.write_text('#!/bin/sh\nexec '+shlex.join(command)+' "$@"\n')
        script.chmod(0o755)
    project = out/'project'; (project/'src').mkdir(parents=True)
    (project/'Cargo.toml').write_text('[package]\nname="sdk_generation_fixture"\nversion="0.1.0"\nedition="2021"\n[lib]\npath="src/lib.rs"\n')
    (project/'src/lib.rs').write_text('''#![no_std]
#[cfg(not(app_config_loaded))] compile_error!("application config was lost");
#[cfg(not(any(aros_sdk_generation="alpha",aros_sdk_generation="beta")))]
compile_error!("installed SDK generation was not forwarded");
#[cfg(aros_sdk_generation="alpha")] pub const SDK_GENERATION: u8 = 1;
#[cfg(aros_sdk_generation="beta")] pub const SDK_GENERATION: u8 = 2;
''')
    crates = out/'crates.toml'; crates.write_text('[patch.crates-io]\n')
    app = out/'application.toml'; app.write_text('[target.aarch64-unknown-aros]\nrustflags=["--cfg=app_config_loaded"]\n')
    makefile = out/'Makefile'
    makefile.write_text('''TARGET_RUST_VER := nightly-2026-08-04
AROS_TARGET_CPU := aarch64
AROS_TOOLCHAIN := llvm
PYTHON := python3
include '''+str(core/'tools/crosstools/rust.cfg')+'''
check:
\t@$(RUST_CARGO) --config "'''+str(app)+'''" check --offline --message-format=json --manifest-path "'''+str(project/'Cargo.toml')+'''" $(RUST_CARGO_TARGET_FLAGS)
''')
    env = dict(os.environ)
    for key in ['RUSTFLAGS','CARGO_ENCODED_RUSTFLAGS','RUSTC','RUSTC_WRAPPER','RUSTC_WORKSPACE_WRAPPER','MAKEFLAGS','CARGO_BUILD_TARGET']:
        env.pop(key, None)
    env['CARGO_TARGET_DIR'] = str(out/'target')
    def install_generation(generation):
        (sdk/'cargo-config.toml').write_text('[target.aarch64-unknown-aros]\nrustflags=['+json.dumps('--cfg=aros_sdk_generation="'+generation+'"')+']\n')
    base = [a.make, '-f', str(makefile), 'SRCDIR='+str(core),
            'RUST_INSTALL_PREFIX='+str(sdk), 'RUST_CRATES_CONFIG='+str(crates),
            'PORTSDIR='+str(out/'ports'), 'PORTSSOURCEDIR='+str(out/'downloads'), 'check']
    rows, commands = [], []
    for label, generation, omitted in [('negative','alpha',True), ('alpha','alpha',False),
                                        ('alpha-cached','alpha',False), ('beta','beta',False)]:
        install_generation(generation)
        command = base + (['RUST_CARGO=$(RUST_CARGO_ENV) $(HOST_CARGO) --config $(RUST_CRATES_CONFIG)'] if omitted else [])
        commands.append({'case': label, 'command': command})
        run = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True, timeout=45)
        (out/(label+'.log')).write_text(run.stdout+run.stderr)
        if omitted:
            assert run.returncode != 0 and 'installed SDK generation was not forwarded' in run.stdout+run.stderr
            rows.append(dict(case=label, result='PASS_REJECTION', exit=run.returncode)); continue
        assert run.returncode == 0, (run.stdout+run.stderr)[-3000:]
        artifacts = [json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
        artifact = next(row for row in artifacts if row.get('reason')=='compiler-artifact' and row['target']['name']=='sdk_generation_fixture')
        metadata = next(Path(name) for name in artifact['filenames'] if name.endswith('.rmeta'))
        rows.append(dict(case=label, result='PASS', fresh=artifact['fresh'],
                         metadata_sha256=hashlib.sha256(metadata.read_bytes()).hexdigest()))
    assert not rows[1]['fresh'] and rows[2]['fresh'] and not rows[3]['fresh'], rows
    assert rows[1]['metadata_sha256']==rows[2]['metadata_sha256']!=rows[3]['metadata_sha256'], rows
    inputs = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in [Path(__file__).resolve(), core/'tools/crosstools/rust.cfg', std/'rustc', std/'result.json', std/'aarch64-unknown-aros.json', cargo]}
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    result=dict(result='PASS',cases=rows,inputs=inputs,scope='Actual core RUST_CARGO; real Cargo/rustc with fixed qualified std and synthetic SDK generation flags; no link or native runtime')
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
