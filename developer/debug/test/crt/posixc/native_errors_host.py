#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER.
# Licensed under the AROS Public License (APL), version 1.1.
"""Run actual POSIX rename/remove bodies with separate delegated error state."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[5]
folder=root/'compiler/crt/posixc'
def extract(name):
    text=(folder/(name+'.c')).read_text()
    return text[text.index('\n{',text.index('int '+name)):text.index('} /* '+name+' */')+1]
helper=(folder/'__dos_error.h').read_text().split('static int __posixc_dos_error',1)[1].split('#endif',1)[0]
prefix=r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef long LONG;
#define ERROR_ACTION_NOT_KNOWN 209
#define ERROR_NOT_IMPLEMENTED 236
static int native_error,delegate_error,delegate_return,calls,conversion,fail_conversion,fail_duplicate,owned;
static char pathbuf[128];
static LONG IoErr(void){return native_error;}
static void SetIoErr(LONG n){native_error=n;}
static int __stdc_ioerr2errno(LONG n){native_error=999;return n==202?ENOENT:(n==215?EXDEV:EINVAL);}
static const char *__path_u2a(const char *s){
    if(++conversion==fail_conversion){errno=ENAMETOOLONG;return NULL;}
    snprintf(pathbuf,sizeof(pathbuf),"%s",s);return pathbuf;
}
static char *test_strdup(const char *s){
    if(fail_duplicate){errno=ENOMEM;return NULL;}
    char *p=malloc(strlen(s)+1);assert(p);strcpy(p,s);owned++;return p;
}
static void test_free(void *p){assert(p&&owned==1);owned--;free(p);errno=888;native_error=999;}
static int __stdcio_rename(const char *a,const char *b){assert(!strcmp(a,"from")&&!strcmp(b,"to"));calls++;native_error=delegate_error;return delegate_return;}
static int __stdcio_remove(const char *a){assert(!strcmp(a,"from"));calls++;native_error=delegate_error;return delegate_return;}
#define strdup test_strdup
#define free test_free
"""
suffix=r"""
static void reset(void){assert(owned==0);errno=0;native_error=0;delegate_error=0;delegate_return=-1;calls=conversion=fail_conversion=fail_duplicate=0;}
int main(void){
    int errors[]={202,0,209,236,215};int expected[]={ENOENT,EIO,ENOSYS,ENOSYS,EXDEV};
    for(unsigned i=0;i<5;i++){
        reset();delegate_error=errors[i];assert(actual_rename("from","to")==-1);
        assert(errno==expected[i]&&native_error==errors[i]&&calls==1&&owned==0);
        reset();delegate_error=errors[i];assert(actual_remove("from")==-1);
        assert(errno==expected[i]&&native_error==errors[i]&&calls==1);
    }
    reset();delegate_return=0;assert(!actual_rename("from","to")&&calls==1&&owned==0);
    reset();delegate_return=0;assert(!actual_remove("from")&&calls==1);
    reset();fail_conversion=1;assert(actual_rename("from","to")==-1&&errno==ENAMETOOLONG&&calls==0);
    reset();fail_conversion=2;assert(actual_rename("from","to")==-1&&errno==ENAMETOOLONG&&calls==0&&owned==0);
    reset();fail_duplicate=1;assert(actual_rename("from","to")==-1&&errno==ENOMEM&&calls==0);
    reset();assert(actual_rename("from",".")==-1&&errno==EEXIST&&calls==0&&owned==0);
    reset();assert(actual_rename("from","..")==-1&&errno==EEXIST&&calls==0&&owned==0);
    reset();fail_conversion=1;assert(actual_remove("from")==-1&&errno==ENAMETOOLONG&&calls==0);
    puts("POSIX native error ASan/UBSan PASS 18 cases; separate delegate context, cleanup clobber, conversion and allocation failures");
}
"""
with tempfile.TemporaryDirectory(prefix='posix-native-errors-') as d:
    p=Path(d);source=prefix+'\nstatic int __posixc_dos_error'+helper+'\nstatic int actual_rename(const char *oldpath,const char *newpath)'+extract('rename')+'\nstatic int actual_remove(const char *pathname)'+extract('remove')+suffix
    (p/'fixture.c').write_text(source)
    subprocess.run(['clang','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'fixture.c'),'-o',str(p/'fixture')],check=True)
    subprocess.run([str(p/'fixture')],check=True,timeout=10)
