#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Run production langinfo entry points with immutable explicit locales."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[5]
    crt = root / 'compiler/crt/posixc'
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    inc = out / 'include'
    (inc / 'aros/types').mkdir(parents=True)
    definitions = []
    for name in [crt / 'include/aros/posixc/langinfo.h',
                 root / 'compiler/include/libraries/localestd.h']:
        for line in name.read_text(encoding='latin1').splitlines():
            if re.match(r'#define\s+[A-Z_0-9]+\s+(?:0x[0-9A-Fa-f]+|[0-9]+)\b', line):
                definitions.append(line)
    (inc / 'langinfo.h').write_text('typedef int nl_item;\n' + '\n'.join(definitions) +
        '\nchar *aros_nl_langinfo(nl_item);\nchar *aros_nl_langinfo_l(nl_item, test_locale_t);\n')
    (inc / 'locale.h').write_text('#define LC_ALL 0\n#define LC_CTYPE 2\n#define LC_TIME 5\n#define LC_GLOBAL_LOCALE ((test_locale_t)-1)\n')
    (inc / '__locale.h').write_text('/* internal getter/setter adapters declared in fixture */\n')
    (inc / 'aros/types/locale_s.h').write_text('/* fixture locale declared before production includes */\n')
    fixture = r'''
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
struct test_locale { const char *__lc_name; };
typedef struct test_locale *test_locale_t;
static _Atomic(const char *) current="C";
static atomic_int queries, mutations;
static struct test_locale c={"C"}, utf8={"C.UTF-8"};
static test_locale_t __get_current_locale(void) {
 atomic_fetch_add(&queries,1);
 return !strcmp(atomic_load(&current),"C.UTF-8") ? &utf8 : &c;
}
static test_locale_t __get_setlocale_internal(const char *name) {
 atomic_fetch_add(&mutations,1);
 if(name && strcmp(name,"C") && strcmp(name,"C.UTF-8")) return NULL;
 atomic_store(&current,name && !strcmp(name,"C.UTF-8") ? "C.UTF-8" : "C");
 return !strcmp(atomic_load(&current),"C.UTF-8") ? &utf8 : &c;
}
#define locale_t test_locale_t
#define nl_langinfo aros_nl_langinfo
#define nl_langinfo_l aros_nl_langinfo_l
#define setlocale aros_setlocale
'''
    implementation = (root / 'compiler/crt/stdc/setlocale.c').read_text() + '\n' + (crt / 'nl_langinfo.c').read_text() + '\n' + (crt / 'nl_langinfo_l.c').read_text()
    fixture += implementation
    fixture += r'''
static void *reader(void *arg) {
 test_locale_t loc=arg; const char *want=loc==&c ? "US-ASCII" : "UTF-8";
 for(int i=0;i<100000;i++) {
  assert(!strcmp(nl_langinfo_l(CODESET,loc),want));
  assert(!strcmp(nl_langinfo_l(DAY_7,loc),"Saturday"));
  assert(!strcmp(nl_langinfo_l(ABDAY_2,loc),"Mon"));
  assert(!strcmp(nl_langinfo_l(MON_12,loc),"December"));
  assert(!strcmp(nl_langinfo_l(D_FMT,loc),"%m/%d/%y"));
  assert(!strcmp(nl_langinfo_l(T_FMT_AMPM,loc),"%I:%M:%S %p"));
  assert(!strcmp(nl_langinfo_l(RADIXCHAR,loc),"."));
  assert(!strcmp(nl_langinfo_l(THOUSEP,loc),""));
 }
 return NULL;
}
static void *writer(void *unused) {
 (void)unused;
 for(int i=0;i<100000;i++) atomic_store(&current,i&1 ? "C" : "C.UTF-8");
 return NULL;
}
int main(void) {
 pthread_t threads[5];
 for(int i=0;i<4;i++) assert(!pthread_create(&threads[i],NULL,reader,i&1 ? &c : &utf8));
 assert(!pthread_create(&threads[4],NULL,writer,NULL));
 for(int i=0;i<5;i++) assert(!pthread_join(threads[i],NULL));
 assert(atomic_load(&queries)==0); /* _l never even reads the global locale */
 assert(atomic_load(&mutations)==0);
 assert(!strcmp(setlocale(LC_CTYPE,"C.UTF-8"),"C.UTF-8"));
 int writes=atomic_load(&mutations);
 assert(!strcmp(setlocale(LC_CTYPE,NULL),"C.UTF-8"));
 assert(atomic_load(&mutations)==writes);
 assert(!strcmp(nl_langinfo(CODESET),"UTF-8"));
 assert(!strcmp(nl_langinfo_l(CODESET,NULL),"UTF-8"));
 errno=0; assert(!strcmp(nl_langinfo_l(CODESET,LC_GLOBAL_LOCALE),"")); assert(errno==EINVAL);
 assert(!strcmp(nl_langinfo_l(-42,&c),""));
 puts("PASS 400000 reader iterations, no explicit-locale global lookup or mutation");
}
'''
    helper = (crt / '__langinfo.h').read_text()
    marker = 'return __posixc_langinfo(item, loc->__lc_name);'
    date = 'case D_FMT: return "%m/%d/%y";'
    assert fixture.count(marker) == 1 and helper.count(date) == 1
    query = 'loc = locale ? __get_setlocale_internal(locale) : __get_current_locale();'
    assert fixture.count(query) == 1
    variants = {
        'current': (fixture, helper),
        'global-explicit-query': (fixture.replace(marker, 'return nl_langinfo(item);'), helper),
        'query-resets-current': (fixture.replace(query, 'loc = __get_setlocale_internal(locale);'), helper),
        'date-instead-of-format': (fixture, helper.replace(date, 'case D_FMT: return "06/22/25";')),
    }
    result = {'runs': [], 'sources': {str(x.relative_to(root)): hashlib.sha256(x.read_bytes()).hexdigest()
        for x in (crt/'nl_langinfo.c', crt/'nl_langinfo_l.c', crt/'__langinfo.h',
                  crt/'include/aros/posixc/langinfo.h', root/'compiler/crt/stdc/setlocale.c', root/'compiler/include/libraries/localestd.h', Path(__file__).resolve())}}
    with tempfile.TemporaryDirectory(prefix='langinfo-host-') as temporary:
        for name, (unit, table) in variants.items():
            variant = out / name
            variant.mkdir()
            (variant/'fixture.c').write_text(unit)
            (variant/'__langinfo.h').write_text(table)
            binary = Path(temporary)/name
            cmd = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-pthread',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I'+str(inc), str(variant/'fixture.c'), '-o', str(binary)]
            build = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            (variant/'build.log').write_text(build.stdout+build.stderr)
            build.check_returncode()
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            (variant/'run.log').write_text(run.stdout+run.stderr)
            expected = run.returncode == 0 if name == 'current' else run.returncode != 0 and 'Assertion failed:' in run.stderr
            result['runs'].append({'variant': name, 'build_exit': build.returncode, 'run_exit': run.returncode, 'expected': expected})
    result['result'] = 'PASS' if all(x['expected'] for x in result['runs']) else 'FAIL'
    (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))
    raise SystemExit(result['result'] != 'PASS')


if __name__ == '__main__':
    main()
