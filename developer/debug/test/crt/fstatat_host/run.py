#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Distributed under the AROS Public License 1.1.
"""Compile actual narrow/wide fstatat sources with modeled metadata providers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
here=Path(__file__).resolve().parent
root=here.parents[4]
folder=root/'compiler/crt/posixc'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
source=(folder/'fstatat.c').read_text();wide=(folder/'fstatat64.c').read_text()
flags='    if (flags & ~AT_SYMLINK_NOFOLLOW)\n    {\n        errno = EINVAL;\n        return -1;\n    }\n'
assert source.count(flags)==1
variants={'current':(source,wide), 'unknown-flags':(source.replace(flags,''),wide),
          'narrow-wide-dispatch':(source.replace(': stat(pathname, statbuf)',': fixture_narrow_fill(pathname, statbuf)'),wide)}
rows=[]
with tempfile.TemporaryDirectory(prefix='fstatat-host-') as temp:
    temp=Path(temp);inc=temp/'include'
    for name in ('aros','sys','dos'):(inc/name).mkdir(parents=True,exist_ok=True)
    (inc/'aros/debug.h').write_text('')
    (inc/'dos/dos.h').write_text('#include <stdint.h>\ntypedef uintptr_t BPTR;\n#define BNULL 0\n')
    (inc/'fcntl.h').write_text('#define AT_SYMLINK_NOFOLLOW 256\n')
    (inc/'unistd.h').write_text('')
    (inc/'sys/stat.h').write_text('''#ifndef HOST_STAT
#define HOST_STAT
#include <stdint.h>
#define stat fixture_stat
#define stat64 fixture_stat64
#define lstat fixture_lstat
#define lstat64 fixture_lstat64
#define fstatat fixture_fstatat
#define fstatat64 fixture_fstatat64
struct stat { uint32_t size,marker; };
struct stat64 { uint64_t marker,size; };
int stat(const char *,struct stat *);
int lstat(const char *,struct stat *);
int stat64(const char *,struct stat64 *);
int lstat64(const char *,struct stat64 *);
int fstatat(int,const char *,struct stat *,int);
int fstatat64(int,const char *,struct stat64 *,int);
/* Give the intentionally wrong provider a compatible signature so its
 * layout error must be detected at runtime, not by a compile failure. */
static inline int fixture_narrow_fill(const char *p,void *b) {
 return stat(p,(struct stat *)b);
}
#endif
''')
    for name,(narrow,large) in variants.items():
        out=a.output/name;out.mkdir()
        (out/'fstatat.c').write_text(narrow);(out/'fstatat64.c').write_text(large)
        (out/'__at.h').write_bytes((folder/'__at.h').read_bytes())
        binary=temp/name
        cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-macro-redefined','-Wno-unused-function',
             '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(inc),
             str(out/'fstatat.c'),str(out/'fstatat64.c'),str(here/'test.c'),'-o',str(binary)]
        built=subprocess.run(cmd,capture_output=True,text=True,timeout=20)
        (out/'build.log').write_text(built.stdout+built.stderr);built.check_returncode()
        ran=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
        (out/'run.log').write_text(ran.stdout+ran.stderr)
        expected=ran.returncode==0 if name=='current' else ran.returncode!=0 and 'Assertion failed:' in ran.stderr
        rows.append({'variant':name,'exit':ran.returncode,'expected':expected})
result={'result':'PASS' if all(x['expected'] for x in rows) else 'FAIL','runs':rows,
        'scope':'Actual full entry points and include wrapper; modeled ABI layouts/directory/metadata, no filesystem or guest acceptance',
        'sources':{str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest()
                   for f in (folder/'fstatat.c',folder/'fstatat64.c',folder/'__at.h',here/'test.c',Path(__file__).resolve())}}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(result['result']!='PASS')
