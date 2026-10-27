#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""CANCLOSELIB: genmodule's generated per-task OpenLib/CloseLib and the
actual rellibrary/libfunc set code, under host Exec and task-storage mocks.

Builds genmodule from tools/genmodule, generates the per-task, two-rellib
test library developer/debug/test/library/userel.conf, extracts its
__GM_DupBase helpers, OpenLib and CloseLib, and links them with the real
_set_open_rellibraries_list, _set_close_rellibraries_list and
set_call_libfuncs. Mutants remove each part of the rollback.
"""
import argparse, hashlib, json, re, subprocess
from pathlib import Path

p = argparse.ArgumentParser(); p.add_argument('--output', type=Path, required=True); a = p.parse_args()
here = Path(__file__).resolve().parent; root = here.parents[4]
out = a.output.resolve(); out.mkdir(parents=True, exist_ok=False)
gm_src = sorted((root / 'tools/genmodule').glob('*.c'))
genmodule = out / 'genmodule'
subprocess.run(['clang', '-O0', '-w', '-o', str(genmodule)] + [str(f) for f in gm_src], check=True, timeout=60)
gen = out / 'gen'; gen.mkdir()
conf = root / 'developer/debug/test/library/userel.conf'
(gen / 'userel.conf').write_text(conf.read_text())
subprocess.run([str(genmodule), '-c', 'userel.conf', '-d', '.', 'writefiles', 'userel', 'library'], cwd=gen, check=True, capture_output=True, timeout=30)
start = (gen / 'userel_start.c').read_text()

# Exercise the generated empty-set definitions too: extracting OpenLib and
# CloseLib alone misses undefined weak sets in modules without these hooks.
header = (root / 'compiler/include/aros/symbolsets.h').read_text()
macros = '\n'.join(re.search(r'^#define ' + name + r'\([^\n]*?(?:\\\n[^\n]*)?$',
                              header, re.M).group() for name in
                   ('SETNAME', 'DECLARESET', 'DEFINESET'))
definitions = '\n'.join(re.findall(r'^DEFINESET\([^\n]+', start, re.M))
for missing in (False, True):
    tested = definitions
    if missing:
        tested = re.sub(r'^DEFINESET\((?:ABORTOPENLIB|CANCLOSELIB)\)\n?', '', tested, flags=re.M)
    source = out / ('empty-sets-missing.c' if missing else 'empty-sets.c')
    source.write_text(macros + '\nDECLARESET(ABORTOPENLIB)\nDECLARESET(CANCLOSELIB)\n' + tested + '''
int main(void)
{
    const void * const *abortset = SETNAME(ABORTOPENLIB);
    const void * const *closeset = SETNAME(CANCLOSELIB);
    return !abortset || !closeset || abortset[0] || abortset[1]
        || closeset[0] || closeset[1];
}
''')
    binary = source.with_suffix('')
    compiled = subprocess.run(['clang', '-std=gnu11', '-Wall', '-Werror', str(source), '-o', str(binary)],
                              capture_output=True, text=True, timeout=30)
    source.with_suffix('.log').write_text(compiled.stdout + compiled.stderr)
    if compiled.returncode:
        # Mach-O refuses the missing weak definitions at link time; ELF may
        # allow them and report a null weak address when the probe runs.
        assert missing and all(name in compiled.stderr for name in
                               ('__ABORTOPENLIB_LIST__', '__CANCLOSELIB_LIST__'))
        continue
    checked = subprocess.run([str(binary)], timeout=10)
    assert checked.returncode == (1 if missing else 0), 'generated empty hook sets'

def between(text, a, b, include_b=False):
    i = text.index(a); j = text.index(b, i)
    return text[i:j + (len(b) if include_b else 0)]

dupbase = between(start, '#ifndef GM_ROOTBASE_FIELD', '#define LIBBASESIZE')
dupbase += start[start.index('#define LIBBASESIZE'):].split('\n', 1)[0] + '\n'
openlib = between(start, 'AROS_LH1 (LIBBASETYPEPTR, GM_UNIQUENAME(OpenLib),', 'AROS_LH0 (BPTR, GM_UNIQUENAME(CloseLib),')
openlib = 'static struct Library *OpenLib(ULONG version, struct Library *libbase)\n' + openlib[openlib.index('{'):]
closelib = between(start, 'AROS_LH0 (BPTR, GM_UNIQUENAME(CloseLib),', "((struct Library *)LIBBASE)->lib_OpenCnt--;\n", True)
closelib = 'static BPTR CloseLib(struct Library *libbase)\n' + closelib[closelib.index('{'):] + '    return 0;\n}\n'

libraries = (root / 'compiler/autoinit/libraries.c').read_text()
libinit = (root / 'compiler/libinit/libinit.c').read_text()
sets = between(libinit, 'int set_call_libfuncs', 'int set_call_devfuncs')
sets = 'static ' + sets.replace('const void * const set[]', 'const void * const set[]')
sets += between(libraries, 'int _set_open_rellibraries_list(', 'void _set_close_rellibraries_list(')
close_rel = libraries[libraries.index('void _set_close_rellibraries_list('):]
depth = 0; end = close_rel.index('{')
for k in range(end, len(close_rel)):
    depth += close_rel[k] == '{'; depth -= close_rel[k] == '}'
    if depth == 0: end = k + 1; break
sets += close_rel[:end] + '\n'

guard = "    if (!set_call_libfuncs(SETNAME(CANCLOSELIB), 1, 1, LIBBASE))\n        return BNULL;\n"
late_anchor = "    __aros_setoffsettable(LIBBASE);\n"
count_anchor = "    dupbase->taskopencount--;\n"
mutations = {
    'positive': None,
    'guard-omitted': ('closelib', guard, ''),
    'guard-after-offset-table': ('closelib', guard + '\n    LIBBASETYPEPTR rootbase', '\n    LIBBASETYPEPTR rootbase'),
    'guard-after-count': ('closelib', guard + '\n    LIBBASETYPEPTR rootbase', '\n    LIBBASETYPEPTR rootbase'),
    'refusal-ignored': ('closelib', guard, '    (void)set_call_libfuncs(SETNAME(CANCLOSELIB), 1, 1, LIBBASE);\n'),
}
rows = []
for name, m in mutations.items():
    lane = out / name; lane.mkdir()
    parts = {'openlib': openlib, 'sets': sets, 'closelib': closelib}
    if m:
        where, before, after = m
        assert parts[where].count(before) == 1, name
        parts[where] = parts[where].replace(before, after)
        if name == 'guard-after-offset-table':
            parts['closelib'] = parts['closelib'].replace(late_anchor, late_anchor + guard, 1)
        if name == 'guard-after-count':
            parts['closelib'] = parts['closelib'].replace(count_anchor, count_anchor + guard, 1)
    (lane / 'dupbase.c').write_text(dupbase)
    (lane / 'actual_sets.c').write_text(parts['sets'])
    (lane / 'openclose.c').write_text(parts['openlib'] + '\n' + parts['closelib'] + '\nstatic struct Library **rootbase_field = &GM_UNIQUENAME(rootbase);\n')
    cmd = ['clang', '-std=gnu11', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter',
           '-fsanitize=address,undefined', '-g', '-I' + str(lane), str(here / 'host.c'), '-o', str(lane / 'host')]
    c = subprocess.run(cmd, capture_output=True, text=True, timeout=60); (lane / 'compile.log').write_text(c.stdout + c.stderr)
    r = subprocess.run([str(lane / 'host')], capture_output=True, text=True, timeout=30) if c.returncode == 0 else None
    if r: (lane / 'run.log').write_text(r.stdout + r.stderr)
    rows.append({'name': name, 'compile_exit': c.returncode, 'run_exit': None if r is None else r.returncode})
inputs = [root / 'tools/genmodule/writestart.c', root / 'compiler/autoinit/libraries.c', root / 'compiler/libinit/libinit.c',
          root / 'compiler/include/aros/symbolsets.h', conf, here / 'host.c', Path(__file__).resolve()]
record = {'scope': 'CANCLOSELIB: genmodule-generated empty hook sets and per-task OpenLib/CloseLib for userel.conf and the actual rellibrary and libfunc set code, host Exec/task-storage/library mocks. No native module build or guest.',
          'empty_hook_sets': 'PASS: generated definitions link and contain two zero entries; missing-definition mutant rejected',
          'inputs': {str(v.relative_to(root)): hashlib.sha256(v.read_bytes()).hexdigest() for v in inputs}, 'checks': rows}
(out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
print(rows)
assert all(r['compile_exit'] == 0 and (r['run_exit'] == 0 if r['name'] == 'positive' else r['run_exit'] not in (0, None)) for r in rows)
print('canclose-host result=PASS')
