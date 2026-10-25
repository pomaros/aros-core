#!/usr/bin/env python3
# Copyright (C) 2026 John Knipper
# Licensed under the AROS Public License (APL) Version 1.1
"""Run actual HOME binding functions with a narrow DOS adapter under sanitizers."""
from pathlib import Path
import subprocess
import os
import tempfile

SOURCE = Path(__file__).resolve().parents[4] / 'workbench/libs/keyring/keyring.c'
ADAPTER = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void *APTR;
typedef uintptr_t BPTR;
#define ACCESS_READ 0
#define LOCK_SAME 0
#define KR_ERR_CONTEXT -21
#define KR_ERR_IO -19
struct Process { APTR pr_WindowPtr; } current;
struct FileInfoBlock { int fib_DirEntryType; };
struct kr_engine { int locked; unsigned char bytes[32]; } engine;
struct KeyringBase {
    struct kr_engine *engine;
    int prompted;
    BPTR home_lock;
    char home[1024],vault[1024],pending[1024],backup[1024];
};
struct directory { const char *path; int identity,refs,isdir,badname,badexamine; };
static struct directory a={"Data:Users/A",1,0,1,0,0};
static struct directory b={"Data:Users/B",2,0,1,0,0};
static struct directory sys={"Data:System",3,0,1,0,0};
static struct directory *home=&a,*system_dir=&sys;
static int is_process=1;
static int process(void) { return is_process; }
static void *FindTask(void *unused) { (void)unused; return &current; }
static void wipe(void *p,size_t n) { volatile unsigned char *b=p; while(n--) *b++=0; }
static void kr_engine_lock(struct kr_engine *e) { e->locked=1; wipe(e->bytes,sizeof(e->bytes)); }
static BPTR Lock(const char *path,int mode)
{
    struct directory *d;
    (void)mode;
    assert(is_process && current.pr_WindowPtr==(APTR)-1);
    if(!strcmp(path,"HOME:")) d=home;
    else { assert(!strcmp(path,"SYS:")); d=system_dir; }
    if(!d) return 0;
    ++d->refs; return (BPTR)d;
}
static void UnLock(BPTR value)
{
    struct directory *d=(struct directory *)value;
    assert(is_process && current.pr_WindowPtr==(APTR)-1 && d->refs>0);
    --d->refs;
}
static int Examine(BPTR value,struct FileInfoBlock *fib)
{
    struct directory *d=(struct directory *)value;
    assert(current.pr_WindowPtr==(APTR)-1);
    fib->fib_DirEntryType=d->isdir?2:-3; return !d->badexamine;
}
static int NameFromLock(BPTR value,char *out,size_t cap)
{
    struct directory *d=(struct directory *)value;
    assert(current.pr_WindowPtr==(APTR)-1);
    if(d->badname || strlen(d->path)>=cap) return 0;
    strcpy(out,d->path); return 1;
}
static int SameLock(BPTR left,BPTR right)
{
    return ((struct directory *)left)->identity==((struct directory *)right)->identity?0:-1;
}
static int AddPart(char *path,const char *part,size_t cap)
{
    size_t n=strlen(path),extra=n && path[n-1]!=':' && path[n-1]!='/';
    if(n+extra+strlen(part)+1>cap) return 0;
    if(extra) path[n++]='/';
    strcpy(path+n,part); return 1;
}
'''
TESTS = r'''
static void reset_fixture(struct KeyringBase *base)
{
    struct kr_quiet scope=quiet_begin(); reset_home(base); quiet_end(scope);
}
static void unlock_fixture(struct KeyringBase *base)
{
    engine.locked=0; memset(engine.bytes,0x71,sizeof(engine.bytes)); base->prompted=1;
}
static void assert_wiped(struct KeyringBase *base)
{
    size_t i;
    assert(engine.locked && base->prompted==0);
    for(i=0;i<sizeof(engine.bytes);i++) assert(engine.bytes[i]==0);
}
int main(void)
{
    struct KeyringBase base={0}; char huge[1024]; APTR prior=(APTR)(uintptr_t)42;
    base.engine=&engine; engine.locked=1; current.pr_WindowPtr=prior;
    assert(bind_home(&base)==0 && current.pr_WindowPtr==prior);
    assert(a.refs==1 && sys.refs==0 && !strcmp(base.vault,"Data:Users/A/Keyring.vault"));
    assert(!strcmp(base.pending,"Data:Users/A/Keyring.vault.new"));
    assert(!strcmp(base.backup,"Data:Users/A/Keyring.vault.old"));
    unlock_fixture(&base);
    assert(bind_home(&base)==0 && !engine.locked && engine.bytes[0]==0x71 && base.prompted==1);
    assert(a.refs==1 && current.pr_WindowPtr==prior);
    home=&b; /* Reassigning HOME alone cannot redirect the pinned I/O paths. */
    assert(!strcmp(base.vault,"Data:Users/A/Keyring.vault"));
    assert(bind_home(&base)==0); assert_wiped(&base);
    assert(a.refs==0 && b.refs==1 && !strcmp(base.vault,"Data:Users/B/Keyring.vault"));
    unlock_fixture(&base); home=NULL;
    assert(bind_home(&base)==KR_ERR_CONTEXT); assert_wiped(&base);
    assert(!base.home_lock && !base.vault[0] && !base.pending[0] && !base.backup[0] && b.refs==0);
    assert(current.pr_WindowPtr==prior);
    home=&a; a.path="Data:System";
    assert(bind_home(&base)==KR_ERR_CONTEXT && a.refs==0);
    a.path="data:system/private";
    assert(bind_home(&base)==KR_ERR_CONTEXT && a.refs==0);
    a.path="Data:Systematic";
    assert(bind_home(&base)==0); reset_fixture(&base);
    a.path="Data:Users/A"; sys.path="Data:";
    assert(bind_home(&base)==KR_ERR_CONTEXT && a.refs==0);
    a.path="UsersVolume:A";
    assert(bind_home(&base)==0); reset_fixture(&base);
    sys.path="Data:System"; a.path="Data:Users/A";
    assert(bind_home(&base)==0); unlock_fixture(&base); system_dir=NULL;
    assert(bind_home(&base)==KR_ERR_CONTEXT); assert_wiped(&base);
    assert(a.refs==0 && current.pr_WindowPtr==prior); system_dir=&sys;
    a.isdir=0; assert(bind_home(&base)==KR_ERR_IO); a.isdir=1;
    a.badname=1; assert(bind_home(&base)==KR_ERR_IO); a.badname=0;
    a.badexamine=1; assert(bind_home(&base)==KR_ERR_IO); a.badexamine=0;
    memset(huge,'x',sizeof(huge)-1); huge[sizeof(huge)-1]=0; a.path=huge;
    assert(bind_home(&base)==KR_ERR_IO && a.refs==0 && !base.vault[0]);
    a.path="Data:Users/A"; assert(bind_home(&base)==0); unlock_fixture(&base);
    is_process=0;
    assert(bind_home(&base)==KR_ERR_CONTEXT); assert_wiped(&base);
    assert(a.refs==1 && base.home_lock && !base.home[0]);
    is_process=1; home=&b;
    assert(bind_home(&base)==0 && a.refs==0 && b.refs==1);
    unlock_fixture(&base); b.path="Data:Users/Renamed";
    assert(bind_home(&base)==0); assert_wiped(&base);
    assert(!strcmp(base.vault,"Data:Users/Renamed/Keyring.vault") && b.refs==1);
    unlock_fixture(&base); sys.path="Data:";
    assert(bind_home(&base)==KR_ERR_CONTEXT); assert_wiped(&base);
    assert(b.refs==0 && sys.refs==0 && current.pr_WindowPtr==prior);
    puts("keyring HOME binding host PASS: actual binding/rebinding/missing/SYS/bounds/requester paths");
    return 0;
}
'''

def main():
    source = SOURCE.read_text()
    extracted = source[source.index('struct kr_quiet {'):source.index('static int random_bytes(')]
    with tempfile.TemporaryDirectory(prefix='keyring-home-host-') as temporary:
        root = Path(temporary)
        unit = root / 'home.c'
        binary = root / 'home'
        def compile_case(body):
            unit.write_text(ADAPTER + body + TESTS)
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            str(unit), '-o', str(binary)], check=True)
        environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=1',
                           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        compile_case(extracted)
        subprocess.run([str(binary)], check=True, env=environment)
        controls = [
            ('kr_engine_lock(base->engine);', 'if(0) kr_engine_lock(base->engine);'),
            ('if(under_system(canonical,sys)) goto fail;',
             'if(0 && under_system(canonical,sys)) goto fail;'),
            ('scope.process->pr_WindowPtr=(APTR)-1;',
             '/* deliberately allow volume requesters */'),
        ]
        for original, replacement in controls:
            assert extracted.count(original) == 1
            compile_case(extracted.replace(original, replacement, 1))
            result = subprocess.run([str(binary)], capture_output=True, env=environment)
            if result.returncode == 0:
                raise AssertionError('HOME binding negative control escaped rejection')
        print('keyring HOME binding negative controls rejected=3')

if __name__ == '__main__':
    main()
