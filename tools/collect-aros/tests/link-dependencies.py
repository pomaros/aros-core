#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER.
# SPDX-License-Identifier: APL-1.1
"""Exercise real ELF links and generated make rules after archive changes."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys

sys.dont_write_bytecode = True


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--env-header', type=Path, required=True)
    ap.add_argument('--clang', type=Path, required=True)
    ap.add_argument('--make', default='make')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    core = Path(__file__).resolve().parents[3]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    lib = out / 'lib'
    lib.mkdir()
    bindir = out / 'bin'
    bindir.mkdir()
    log = (out / 'commands.log').open('w')

    def run(command, *, env=None, success=True):
        log.write(shlex.join(map(str, command)) + '\n')
        log.flush()
        p = subprocess.run(list(map(str, command)), cwd=out, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log.write(p.stdout)
        log.flush()
        if success and p.returncode:
            raise RuntimeError(p.stdout)
        if not success and not p.returncode:
            raise RuntimeError('negative control unexpectedly succeeded')
        return p

    source = core / 'tools/collect-aros'
    collector = bindir / 'collect-aros'
    run(['cc', '-Wall', '-O2', '-I', args.env_header.resolve().parent,
         '-I', source, f'-DOBJLIBDIR="{lib}"', '-o', collector,
         *(source / name for name in ('collect-aros.c', 'gensets.c', 'misc.c',
                                     'backend-generic.c', 'docommand-exec.c'))])
    clang = args.clang.resolve()
    ar = clang.parent / 'llvm-ar'

    def compile_c(name, body):
        src = out / (name + '.c')
        obj = out / (name + '.o')
        src.write_text(body)
        run([clang, '--target=aarch64-unknown-aros', '-fno-pic', '-c', src, '-o', obj])
        return obj

    def archive(name, symbol, value):
        obj = compile_c(name, f'int {symbol} = {value};\n')
        run([ar, 'rcs', lib / f'lib{name}.a', obj])

    archive('implicit', 'answer', 17)
    archive('arch', 'arch_answer', 23)
    archive('flags', 'flags_answer', 31)
    archive('unused', 'unused_answer', 71)
    obj = compile_c('probe', 'extern int answer, arch_answer, flags_answer;\n'
                    'int main(void) { return answer + arch_answer + flags_answer; }\n')
    driver = out / 'driver'
    # A compiler driver can add defaults absent from uselibs, just as AROS's
    # real driver adds arossupport. The ELF compilation and all links are real.
    driver.write_text('#!/bin/sh\nexec ' + shlex.join([
        str(clang), '--target=aarch64-unknown-aros', '-B' + str(bindir),
        '-nostdlib', '-nostartfiles', '-L' + str(lib), '-limplicit']) + ' "$@"\n')
    driver.chmod(0o755)
    (out / 'obj').mkdir()
    (out / 'dest').mkdir()
    shutil.copyfile(obj, out / 'obj/multi.o')
    template = (core / 'config/make.tmpl').read_text()
    # genmf normally appends the full build configuration. The fixture supplies
    # its own variables but expands the production link macros unchanged.
    template += '\n%define common\n%end\n'
    (out / 'make.tmpl').write_text(template)
    for included in (core / 'config').glob('make-*.tmpl'):
        shutil.copyfile(included, out / included.name)
    env = ("$(if $(filter yes,$(AROS_LINK_DEPS)),"
           "COLLECT_AROS_DEPENDENCIES='$@.ld.d' COLLECT_AROS_DEPENDENCY_TARGET='$@')")
    rules = f'''all: module dest/single dest/multi
AROS_LINK_DEPS = yes
AROS_LINK_DEP_ENV = {env}
COLLECT-AROS = {collector}
AROS_CC = {driver}
AROS_LIB = {lib}
GENDIR = {out}
Q = @
ECHO = echo
IF = if
FOR = for
TEST = test
CP = cp
RM = rm -f
CAT = cat
NOP = :
TARGET_STRIP =
STRIP =
GENMAP = -Wl,-Map -Xlinker
MODULE = module
%rule_link_prog prog=dest/single objs=probe.o objdir=obj usestartup=no ldflags="-L{lib} -lflags" archlibs="-larch"
%rule_link_progs progs=multi targetdir=dest objdir=obj usestartup=no ldflags="-L{lib} -lflags" archlibs="-larch"
%rule_linkmodule module=module objs=probe.o endobj= err=module.err objdir=obj ldflags="-L{lib} -lflags" archlibs="-larch"
'''
    (out / 'input.make').write_text(rules)
    run([sys.executable, core / 'tools/genmf/genmf.py', out / 'make.tmpl',
         out / 'input.make', out / 'Makefile'])
    outputs = [out / x for x in ('module', 'dest/single', 'dest/multi')]
    spec = importlib.util.spec_from_file_location('elf_sections', source / 'tests/debug-strip.py')
    elf = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(elf)

    def values():
        return [sorted(struct.unpack('<3i', elf.sections(p)['.data'][1])) for p in outputs]

    def make(*options):
        return run([args.make, '--no-print-directory', *options, 'all'])

    # Compiling negative control: the former explicit-only prerequisites leave
    # all three output kinds stale after changing the implicit archive.
    make('AROS_LINK_DEPS=no')
    assert values() == [[17, 23, 31]] * 3
    archive('implicit', 'answer', 19)
    make('AROS_LINK_DEPS=no')
    assert values() == [[17, 23, 31]] * 3, 'negative control did not retain old code'
    make()
    assert values() == [[19, 23, 31]] * 3, 'missing depfiles must migrate old outputs'
    for name, symbol, value, expected in (
            ('implicit', 'answer', 29, [23, 29, 31]),
            ('arch', 'arch_answer', 37, [29, 31, 37]),
            ('flags', 'flags_answer', 41, [29, 37, 41])):
        archive(name, symbol, value)
        make()
        assert values() == [expected] * 3, f'{name}: stale linked data'
    before = [p.stat().st_mtime_ns for p in outputs]
    make()
    assert before == [p.stat().st_mtime_ns for p in outputs], 'unchanged inputs relinked'
    archive('unused', 'unused_answer', 73)
    make()
    assert before == [p.stat().st_mtime_ns for p in outputs], 'unrelated archive relinked'
    for p in outputs:
        deps = Path(str(p) + '.ld.d').read_text()
        assert 'libimplicit.a' in deps and 'libarch.a' in deps and 'libflags.a' in deps
        assert 'catmp' not in deps and 'libunused.a' not in deps
    # Inputs discovered only during collect-aros's final link must also count.
    extra = compile_c('extra', 'void __cxa_pure_virtual(void) {}\n')
    shutil.copyfile(extra, lib / 'static-cxx-cxa-pure-virtual.o')
    weak = compile_c('weak', 'extern void __cxa_pure_virtual(void) __attribute__((weak));\n'
                     'void (*handler)(void) = __cxa_pure_virtual;\n')
    depenv = dict(os.environ, COLLECT_AROS_DEPENDENCIES=str(out / 'extra.ld.d'))
    run([collector, '-o', out / 'extra', weak], env=depenv)
    assert 'static-cxx-cxa-pure-virtual.o' in (out / 'extra.ld.d').read_text()
    # An unsuccessful final link must not publish a new depfile/output.
    missing = compile_c('missing', 'extern void absent(void); void call(void) { absent(); }\n')
    previous = (out / 'extra.ld.d').read_bytes()
    run([collector, '-o', out / 'extra', missing], env=depenv, success=False)
    assert not (out / 'extra').exists()
    assert (out / 'extra.ld.d').read_bytes() == previous
    # Both partial-link modes retain dependency tracking as well.
    for flag, name in (('-r', 'partial'), ('-Ur', 'sets')):
        depenv['COLLECT_AROS_DEPENDENCIES'] = str(out / (name + '.ld.d'))
        run([collector, flag, '-o', out / name, obj, '-L' + str(lib),
             '-limplicit', '-larch', '-lflags'], env=depenv)
        assert 'libimplicit.a' in (out / (name + '.ld.d')).read_text()
        assert elf.sections(out / name)['.data'][1]
    # Check make escaping using real filenames, not a parser-only fixture.
    strange = out / 'space $ cash # input.o'
    shutil.copyfile(out / 'implicit.o', strange)
    depenv['COLLECT_AROS_DEPENDENCIES'] = str(out / 'escaped.ld.d')
    run([collector, '-o', 'escaped', strange], env=depenv)
    (out / 'escaped.make').write_text('include escaped.ld.d\nescaped: ; @:\n')
    run([args.make, '-f', 'escaped.make', '-q', 'escaped'])
    strange.touch()
    assert run([args.make, '-f', 'escaped.make', '-q', 'escaped'], success=False).returncode == 1
    result = {'result': 'PASS', 'scope': 'host AArch64 cross-linking and real generated make rules',
              'cases': ['explicit-only stale-code negative control', 'missing-sidecar migration',
                        'implicit driver archive', 'archlibs archive', 'ldflags archive',
                        'no-change no relink', 'unrelated archive no relink',
                        'final-stage extra object', 'failed-link no publication',
                        'partial and symbol-set links', 'make path escaping']}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
