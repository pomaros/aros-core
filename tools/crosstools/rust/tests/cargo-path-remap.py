#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual genmf Cargo config, offline Cargo/rustc and an absolute include path."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tomllib

CORE = Path(__file__).resolve().parents[4]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--std-build', type=Path, required=True)
    p.add_argument('--cargo', type=Path, required=True)
    p.add_argument('--make', default=shutil.which('gmake') or 'make')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out, std = a.output.resolve(), a.std_build.resolve()
    out.mkdir(parents=True, exist_ok=False)
    sdk = out / 'sdk'
    (sdk / 'bin').mkdir(parents=True)
    (sdk / 'share').mkdir()
    for name, original in [('cargo', a.cargo.resolve()), ('rustc', std / 'rustc')]:
        (sdk / 'bin' / name).symlink_to(original)
    shutil.copyfile(std / 'aarch64-unknown-aros.json', sdk / 'share/aarch64-unknown-aros.json')
    (sdk / 'cargo-config.toml').write_text('[target.aarch64-unknown-aros]\nrustflags=["--cfg=sdk_generation_present"]\n')
    project = out / 'project'
    (project / 'src').mkdir(parents=True)
    location = project / 'src/location.rs'
    location.write_text('''pub const SOURCE: &str = file!();
const _: () = assert!(SOURCE.as_bytes()[0] != b'/', "build path was not remapped");
#[cfg(not(sdk_generation_present))] compile_error!("SDK generation flags were lost");
''')
    (project / 'src/lib.rs').write_text('#![no_std]\ninclude!(' + json.dumps(str(location)) + ');\n')
    (project / 'Cargo.toml').write_text('[package]\nname="source_location_fixture"\nversion="0.1.0"\nedition="2021"\n')
    (out / 'fixture').mkdir()
    (out / 'fixture/mmakefile').touch()
    app = out / 'application.toml'
    template_input = out / 'input.mmake'
    template_input.write_text('%rust_cargo_config mmake=fixture-config file=' + str(app) + ' targetdir=' + str(out / 'target') + '\n')
    generated = out / 'rules.mk'
    command = [sys.executable, str(CORE / 'tools/genmf/genmf.py'), str(CORE / 'config/make.tmpl'), str(template_input), str(generated)]
    subprocess.run(command, check=True, capture_output=True, timeout=20)
    makefile = out / 'Makefile'
    makefile.write_text('''TARGET_RUST_VER := nightly-2026-08-04
AROS_TARGET_CPU := aarch64
AROS_TOOLCHAIN := llvm
PYTHON := python3
CURDIR := fixture
ECHO := echo
Q := @
FOR := for
IF := if
TEST := test
MKDIR := mkdir -p
NOP := :
include ''' + str(CORE / 'tools/crosstools/rust.cfg') + '\ninclude ' + str(generated) + '\n')
    command = [a.make, '-f', str(makefile), 'TOP=' + str(out), 'SRCDIR=' + str(CORE),
               'RUST_INSTALL_PREFIX=' + str(sdk), 'PORTSDIR=' + str(out / 'ports'),
               'PORTSSOURCEDIR=' + str(out / 'downloads'), str(app)]
    run = subprocess.run(command, cwd=out, capture_output=True, text=True, timeout=20)
    (out / 'generate.log').write_text(run.stdout + run.stderr)
    run.check_returncode()
    original = app.read_text()
    config = tomllib.loads(original)
    flags = config['target']['aarch64-unknown-aros']['rustflags']
    assert len(flags) == 5 and all(f.startswith('--remap-path-prefix=') for f in flags)
    env = dict(os.environ)
    for key in ['RUSTFLAGS', 'CARGO_ENCODED_RUSTFLAGS', 'RUSTC_WRAPPER', 'RUSTC_WORKSPACE_WRAPPER', 'CARGO_BUILD_TARGET']:
        env.pop(key, None)
    env.update(RUSTC=str(std / 'rustc'), CARGO_HOME=str(out / 'cargo-home'))
    records = []
    for name in ['actual', 'without-remapping']:
        if name == 'without-remapping':
            start = original.index('rustflags = [')
            end = original.index(']', start) + 1
            app.write_text(original[:start] + 'rustflags = []' + original[end:])
        command = [str(a.cargo.resolve()), '--config', str(sdk / 'cargo-config.toml'), '--config', str(app),
                   'check', '--offline', '--manifest-path', str(project / 'Cargo.toml')]
        run = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True, timeout=40)
        (out / (name + '.log')).write_text(run.stdout + run.stderr)
        assert (run.returncode == 0) == (name == 'actual'), run.stderr
        if name != 'actual':
            assert 'build path was not remapped' in run.stderr
            assert 'error: SDK generation flags were lost' not in run.stderr
        records.append(dict(case=name, exit=run.returncode, argv=command))
    app.write_text(original)
    (out / 'result.json').write_text(json.dumps(dict(result='PASS', cases=records,
        scope='Actual generated config and real offline custom-target Cargo/rustc; absolute include file! remapping with SDK generation flags retained; no link/runtime'), indent=2) + '\n')
    print('PASS generated Cargo path remapping and SDK flag merge; missing-remap compile rejection')


if __name__ == '__main__':
    main()
