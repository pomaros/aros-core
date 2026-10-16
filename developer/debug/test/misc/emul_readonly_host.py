#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1
"""Exercise production Unix errno mapping/fallback against a read-only host file.

Usage: python3 emul_readonly_host.py /mounted/read-only/AROS.boot
Requires macOS clang and a fixture whose O_RDWR open fails with EPERM.
"""
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[4]
source = (root / 'arch/all-unix/filesys/emul_handler/emul_host.c').read_text()
dos = (root / 'compiler/include/dos/dos.h').read_text(encoding='latin-1')
constants = '\n'.join(re.findall(r'^#define\s+ERROR_\w+\s+\d+.*$', dos, re.M))
start = source.index('static LONG u2a[][2]')
end = source.index('/********', source.index('static inline LONG err_u2a', start))
mapping = source[start:end]
start = source.index('        if (r < 0 && err_u2a(emulbase) == ERROR_WRITE_PROTECTED)')
end = source.index('        if (r >= 0)', start)
fallback = source[start:end]
fixture = str(Path(sys.argv[1]).resolve())
prelude = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef int32_t LONG;
typedef uint32_t ULONG;
struct LibCInterface { int (*open)(const char *, int, ...); };
struct emulbase { struct { int *errnoPtr; struct LibCInterface *SysIFace; } pdata; };
struct filehandle { const char *hostname; };
#define AROS_HOST_BARRIER
'''
main = r'''
int main(int argc, char **argv) {
    struct LibCInterface iface = { open };
    struct emulbase base = {{ &errno, &iface }};
    struct emulbase *emulbase = &base;
    struct filehandle handle = { argv[1] }, *fh = &handle;
    int r = open(fh->hostname, O_RDWR), native_error = errno;
    assert(r < 0 && native_error == EPERM);
    assert(errno_u2a(EACCES) == ERROR_WRITE_PROTECTED);
    assert(errno_u2a(EROFS) == ERROR_WRITE_PROTECTED);
    assert(errno_u2a(EINVAL) == ERROR_UNKNOWN);
    errno = native_error;
'''
finish = r'''
    if (NEGATIVE) {
        assert(errno_u2a(EPERM) == ERROR_UNKNOWN);
        assert(r < 0);
        puts("NEGATIVE_CONTROL_PASS");
    } else {
        char text[32] = {0};
        assert(errno_u2a(EPERM) == ERROR_WRITE_PROTECTED);
        assert(r >= 0);
        assert(read(r, text, sizeof(text)-1) > 0);
        assert(strstr(text, "aarch64"));
        assert(write(r, "X", 1) < 0 && errno == EBADF);
        close(r);
        printf("PRODUCTION_READONLY_PASS native_errno=%d signature=%s", native_error, text);
    }
    return 0;
}
'''
# Removing only the new table entry recreates the production baseline failure.
baseline = re.sub(r'^\s*\{ EPERM\s*, ERROR_WRITE_PROTECTED\s*\},\n', '', mapping, flags=re.M)
assert baseline != mapping
outputs = {}
with tempfile.TemporaryDirectory(prefix='emul-readonly-') as directory:
    for name, table, negative in [('production', mapping, 0), ('baseline', baseline, 1)]:
        c = Path(directory) / (name + '.c')
        binary = c.with_suffix('')
        c.write_text(prelude + constants + '\n' + table + '\n#define NEGATIVE ' + str(negative) + '\n' + main + fallback + finish)
        subprocess.run(['clang', '-Wall', '-Wextra', '-Wno-unused-parameter', '-O2', str(c), '-o', str(binary)], check=True)
        outputs[name] = subprocess.check_output([str(binary), fixture], text=True).strip()
assert 'PRODUCTION_READONLY_PASS' in outputs['production']
assert outputs['baseline'] == 'NEGATIVE_CONTROL_PASS'
print(json.dumps({'fixture': fixture, 'results': outputs}, indent=2))
