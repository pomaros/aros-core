#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER.
# Licensed under the AROS Public License (APL), version 1.1.
"""Run actual POSIX rename/remove/rmdir bodies with separate delegated error state."""
from pathlib import Path
import subprocess
import tempfile
import re
root=Path(__file__).resolve().parents[5]
folder=root/'compiler/crt/posixc'
def extract(name):
    text=(folder/(name+'.c')).read_text()
    return text[text.index('\n{',text.index('int '+name)):text.index('} /* '+name)+1]
helper=(folder/'__dos_error.h').read_text().split('static int __posixc_dos_error',1)[1].split('#endif',1)[0]
# stdc owns the fallback mapping; execute its actual body with DOS constants.
stdc=(root/'compiler/crt/stdc/ioerr2errno.c').read_text()
mapper=stdc[stdc.index('\n{',stdc.index('int ioerr2errno')):stdc.index('} /* ioerr2errno */')+1]
dos='\n'.join((root/'compiler/include/dos'/name).read_text(encoding='latin1') for name in ('dos.h','dosasl.h'))
defines=[]
for name in sorted(set(re.findall(r'\bERROR_[A-Z_]+\b', mapper+helper+Path(__file__).read_text()))):
    match=re.search(r'^#define\s+'+name+r'\s+([^\n]+)',dos,re.M)
    assert match,name
    defines.append('#define '+name+' '+match[1])
prefix=r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef long LONG;
#define MAX_ERRNO 1000
static int stdc_mapper(int);
static int native_error,delegate_error,delegate_return,calls,conversion,fail_conversion,fail_duplicate,owned;
static char pathbuf[128];
static LONG IoErr(void){return native_error;}
static void SetIoErr(LONG n){native_error=n;}
static int __stdc_ioerr2errno(LONG n){native_error=999;return stdc_mapper(n);}
static const char *__path_u2a(const char *s){
    if(++conversion==fail_conversion){errno=ENAMETOOLONG;return NULL;}
    assert(strlen(s)<sizeof(pathbuf));memmove(pathbuf,s,strlen(s)+1);return pathbuf;
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
    int errors[]={ERROR_OBJECT_NOT_FOUND,0,ERROR_ACTION_NOT_KNOWN,ERROR_NOT_IMPLEMENTED,ERROR_RENAME_ACROSS_DEVICES,ERROR_DIRECTORY_NOT_EMPTY,ERROR_DELETE_PROTECTED,ERROR_WRITE_PROTECTED,ERROR_READ_PROTECTED,ERROR_DISK_FULL,ERROR_DISK_WRITE_PROTECTED,ERROR_TOO_MANY_LEVELS,400};
    int expected[]={ENOENT,EIO,ENOSYS,ENOSYS,EXDEV,ENOTEMPTY,EACCES,EACCES,EACCES,ENOSPC,EROFS,ELOOP,MAX_ERRNO+400};
    for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++){
        reset();delegate_error=errors[i];assert(actual_rename("from","to")==-1);
        assert(errno==expected[i]&&native_error==errors[i]&&calls==1&&owned==0);
        reset();delegate_error=errors[i];assert(actual_remove("from")==-1);
        assert(errno==expected[i]&&native_error==errors[i]&&calls==1);
        reset();delegate_error=errors[i];assert(actual_rmdir("from")==-1);
        assert(errno==expected[i]&&native_error==errors[i]&&calls==1&&conversion==1);
    }
    reset();delegate_return=0;assert(!actual_rename("from","to")&&calls==1&&owned==0);
    reset();delegate_return=0;assert(!actual_remove("from")&&calls==1);
    reset();fail_conversion=1;assert(actual_rename("from","to")==-1&&errno==ENAMETOOLONG&&calls==0);
    reset();fail_conversion=2;assert(actual_rename("from","to")==-1&&errno==ENAMETOOLONG&&calls==0&&owned==0);
    reset();fail_duplicate=1;assert(actual_rename("from","to")==-1&&errno==ENOMEM&&calls==0);
    reset();assert(actual_rename("from",".")==-1&&errno==EEXIST&&calls==0&&owned==0);
    reset();assert(actual_rename("from","..")==-1&&errno==EEXIST&&calls==0&&owned==0);
    reset();fail_conversion=1;assert(actual_remove("from")==-1&&errno==ENAMETOOLONG&&calls==0);
    reset();delegate_return=0;assert(!actual_rmdir("from")&&calls==1&&conversion==1);
    reset();fail_conversion=1;assert(actual_rmdir("from")==-1&&errno==ENAMETOOLONG&&calls==0&&conversion==1);
    puts("POSIX native error ASan/UBSan PASS 49 cases; separate delegate context, cleanup clobber, conversion and allocation failures");
}
"""
with tempfile.TemporaryDirectory(prefix='posix-native-errors-') as d:
    p=Path(d);source=prefix+'\n'+'\n'.join(defines)+'\nstatic int stdc_mapper(int ioerr)'+mapper+'\nstatic int __posixc_dos_error'+helper+'\nstatic int actual_rename(const char *oldpath,const char *newpath)'+extract('rename')+'\nstatic int actual_remove(const char *pathname)'+extract('remove')+'\n#define remove actual_remove\nstatic int actual_rmdir(const char *pathname)'+extract('rmdir')+suffix
    (p/'fixture.c').write_text(source)
    subprocess.run(['clang','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'fixture.c'),'-o',str(p/'fixture')],check=True)
    subprocess.run([str(p/'fixture')],check=True,timeout=10)
