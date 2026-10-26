#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Real GNU make timestamp propagation through the actual genmodule rules."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--make', default=shutil.which('gmake') or 'make')
    args = parser.parse_args()
    version = subprocess.check_output([args.make, '--version'], text=True, timeout=10)
    if not version.startswith('GNU Make '):
        parser.error('--make must select GNU make')
    args.output.mkdir(parents=True, exist_ok=False)
    source = (ROOT / 'config/make.tmpl').read_text()
    records = []
    for flag in ['GENFILEFLAG', 'GENINCFLAG']:
        anchor = '$(%(mmake)_TARGETS) : $(%(mmake)_DEPS) $(%(mmake)_' + flag + ')'
        assert source.count(anchor) == 1
        at = source.index(anchor)
        rule = source[at:source.index('\n\n', at)]
        rule = rule.replace('$(%(mmake)_TARGETS)', 'generated.h')
        rule = rule.replace('$(%(mmake)_DEPS)', '')
        rule = rule.replace('$(%(mmake)_' + flag + ')', 'generation.flag')
        for variant in ['actual', 'missing-recipe']:
            directory = args.output / (flag + '-' + variant)
            directory.mkdir()
            tested = rule if variant == 'actual' else rule.splitlines()[0]
            # Bind only template variables; GNU make itself owns the update
            # traversal and timestamp cache. The generator changes its output
            # as a side effect of updating the generation flag.
            (directory / 'Makefile').write_text('Q :=\nNOP := :\nall: copied.h\n' + tested + '''
generation.flag: input.h
\tcp input.h generated.h
\ttouch generation.flag
copied.h: generated.h
\tcp generated.h copied.h
''')
            for name, content, stamp in [('generated.h', 'old\n', 1000),
                                         ('generation.flag', '', 1000),
                                         ('copied.h', 'old\n', 2000),
                                         ('input.h', 'updated\n', 3000)]:
                path = directory / name
                path.write_text(content)
                os.utime(path, (stamp, stamp))
            command = [args.make, '--no-print-directory', '-f', 'Makefile', 'all']
            run = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=10)
            (directory / 'make.log').write_text(run.stdout + run.stderr)
            assert run.returncode == 0, run.stderr
            propagated = (directory / 'copied.h').read_text() == 'updated\n'
            records.append(dict(rule=flag, variant=variant, propagated=propagated))
    passed = all(r['propagated'] == (r['variant'] == 'actual') for r in records)
    (args.output / 'result.json').write_text(json.dumps(dict(result='PASS' if passed else 'FAIL',
        cases=records, template_sha256=hashlib.sha256(source.encode()).hexdigest(),
        make_version=version.splitlines()[0], scope='Actual genmodule output rule text with a controlled generator; real GNU make traversal and dependent copy, no full OS build'), indent=2) + '\n')
    print(records)
    raise SystemExit(not passed)


if __name__ == '__main__':
    main()
