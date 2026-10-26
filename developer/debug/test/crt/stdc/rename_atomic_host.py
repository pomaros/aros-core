#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER.
# Licensed under the AROS Public License (APL), version 1.1.
"""Exercise the actual CRT rename body with failure-preserving DOS operations."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[5]
source = (root / 'compiler/crt/stdc/rename.c').read_text()
body = source[source.index('\n{', source.index('int rename')):]
prefix = r"""
#include <assert.h>
#include <stdio.h>
#include <errno.h>
typedef long LONG; typedef const char *CONST_STRPTR;
#define ERROR_OBJECT_EXISTS 203
static int native_error, rename_ok, replace_ok, rename_error, replace_error;
static int source_value, target_value, rename_calls, replace_calls;
static int Rename(const char *a, const char *b) {
    (void)a; (void)b; rename_calls++;
    if (rename_ok) {target_value=source_value;source_value=0;return 1;}
    native_error=rename_error;return 0;
}
static int ReplaceObject(const char *a, const char *b) {
    (void)a;(void)b;replace_calls++;
    if (replace_ok) {target_value=source_value;source_value=0;return 1;}
    native_error=replace_error;return 0;
}
static LONG IoErr(void) {return native_error;}
static void SetIoErr(LONG value) {native_error=value;}
static int __stdc_ioerr2errno(LONG value) {native_error=999;return value?1000+value:0;}
static int actual_rename(const char *oldpath, const char *newpath)
"""
suffix = r"""
int main(void) {
    int cases=0;
    for (int mode=0;mode<8;mode++) {
        source_value=42;target_value=99;rename_calls=replace_calls=0;
        rename_ok=mode==0;replace_ok=mode==1;
        rename_error=mode<=4?ERROR_OBJECT_EXISTS:(mode==5?0:215);
        replace_error=mode==2?209:(mode==3?0:223);errno=0;
        int rc=actual_rename("source","target");
        assert(rename_calls==1);
        assert(replace_calls==(mode>=1&&mode<=4));
        if (mode<=1) {assert(rc==0&&target_value==42&&source_value==0);}
        else {
            int expected=mode<=4?replace_error:rename_error;
            assert(rc==-1&&target_value==99&&source_value==42);
            assert(errno==(expected?expected+1000:EIO));
            assert(native_error==expected);
        }
        cases++;
    }
    printf("rename actual-body ASan/UBSan PASS cases=%d; failed replacement preserves both files\n",cases);
}
"""
with tempfile.TemporaryDirectory(prefix='rename-atomic-') as d:
    p=Path(d);(p/'fixture.c').write_text(prefix+body+suffix)
    subprocess.run(['clang','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'fixture.c'),'-o',str(p/'fixture')],check=True)
    subprocess.run([str(p/'fixture')],check=True)
