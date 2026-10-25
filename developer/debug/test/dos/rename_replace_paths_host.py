#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Execute actual Rename/ReplaceObject with resolved assigns and BSTR failures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]


def body(text):
    return text.split('AROS_LIBFUNC_INIT', 1)[1].split('AROS_LIBFUNC_EXIT', 1)[0]


replacement = (root/'rom/dos/replaceobject.c').read_text()
rename = (root/'rom/dos/rename.c').read_text()
helper = rename[rename.index('static BOOL has_parent_reference('):rename.index('/*****************************************************************************')]
classic = (root/'arch/m68k-all/dos/bstr_helper.c').read_text()
classic = classic[classic.index('BSTR C2BSTR('):classic.index('char *BSTR2C(')]
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int BOOL; typedef uintptr_t BPTR; typedef uintptr_t IPTR;
typedef uintptr_t BSTR; typedef int32_t LONG;
typedef char *STRPTR; typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define DOSFALSE 0
#define BNULL 0
#define MEMF_ANY 0
#define SHARED_LOCK -2
#define ERROR_NO_FREE_STORE 103
#define ERROR_RENAME_ACROSS_DEVICES 215
#define ACTION_RENAME_OBJECT 17
#define ACTION_REPLACE_OBJECT 1037
#define D(x)
#define bug(...) ((void)0)
struct DosLibrary { int unused; };
struct MsgPort { int id; };
struct DevProc { struct MsgPort *dvp_Port; BPTR dvp_Lock; };
struct Process { BPTR pr_CurrentDir; };
static struct Process process={11};
#define ASSERT_VALID_PROCESS(p) assert(p==&process)
static void *FindTask(void *p) { (void)p; return &process; }
static LONG SplitName(const char *p,char sep,char *out,int at,int n) { (void)out;(void)at;(void)n;const char *colon=strchr(p,sep);return colon?(LONG)(colon-p+1):-1; }
static BPTR Lock(const char *p,int access) { (void)p;(void)access;return 0; }
static void UnLock(BPTR p) { (void)p; }
static int NameFromLock(BPTR lock,char *p,int n) { (void)lock;(void)p;(void)n;return 0; }
#define CopyMem(src,dst,n) memcpy(dst,src,n)
static struct MsgPort port={1},other={2};
static unsigned procs,names,allocs,device_calls,packets;
static int error,fail_device,fail_alloc,fail_packet,capture_only;
static const char *expected_source,*expected_target;
static char got_source[128],got_target[128];
static LONG IoErr(void) { return error; }
static void SetIoErr(LONG n) { error=n; }
static struct DevProc *GetDeviceProc(const char *p,void *unused) {
    (void)unused;device_calls++;
    assert(!strcmp(p,device_calls==1?expected_source:expected_target));
    if(fail_device==(int)device_calls){error=218;return NULL;}
    struct DevProc *d=malloc(sizeof(*d));assert(d);procs++;
    d->dvp_Port=!strncmp(p,"OTHER:",6)?&other:&port;
    d->dvp_Lock=!strncmp(p,"DST:",4)?22:(!strncmp(p,"SYS:",4)||*p==':'?0:11);
    return d;
}
static void FreeDeviceProc(struct DevProc *p) { if(p){assert(procs);procs--;free(p);}error=998; }
#ifndef FAST
#define MKBADDR(p) ((BSTR)((uintptr_t)(p)>>2))
#define BADDR(p) ((void *)((uintptr_t)(p)<<2))
static void *AllocVec(size_t n,int flags) { (void)flags;allocs++;if(fail_alloc==(int)allocs)return NULL;names++;return malloc(n); }
static void FreeVec(void *p) { assert(p&&names);names--;free(p);error=997; }
#define FREEC2BSTR(p) FreeVec(BADDR(p))
#define TEXT(p) ((const char *)BADDR(p)+1)
#else
#define C2BSTR(p) ((BSTR)(uintptr_t)(p))
#define FREEC2BSTR(p) ((void)(p))
#define TEXT(p) ((const char *)(uintptr_t)(p))
#endif
struct Entry { BPTR parent; const char *name; int value; };
static struct Entry entries[]={{11,"file",1},{11,"target",2},{22,"target",2},{0,"file",91},{0,"target",92}};
static struct Entry *entry(BPTR parent,const char *name) { for(unsigned i=0;i<5;i++)if(entries[i].parent==parent&&!strcmp(entries[i].name,name))return &entries[i];return NULL; }
static LONG dopacket4(struct DosLibrary *base,void *unused,struct MsgPort *p,int action,BPTR source_parent,BSTR source,BPTR target_parent,BSTR target) {
    (void)base;(void)unused;assert(p==&port);packets++;
    const char *from=TEXT(source),*to=TEXT(target);strcpy(got_source,from);strcpy(got_target,to);
    if(capture_only){error=0;return TRUE;}
    if(fail_packet){error=224;return FALSE;}
    /* A handler receiving a colon-qualified path starts at its volume root.
     * This separate namespace model makes the old code touch real shadows. */
    const char *colon=strchr(from,':');if(colon){source_parent=0;from=colon+1;}
    colon=strchr(to,':');if(colon){target_parent=0;to=colon+1;}
    struct Entry *src=entry(source_parent,from),*dst=entry(target_parent,to);
    if(!src||!dst||!src->value){error=205;return FALSE;}
    if(action==ACTION_RENAME_OBJECT&&dst->value){error=203;return FALSE;}
    dst->value=src->value;src->value=0;error=0;return TRUE;
}
static void reset(const char *from,const char *to,int replacing) {
    assert(!procs&&!names);error=fail_device=fail_alloc=fail_packet=capture_only=0;
    device_calls=allocs=packets=0;expected_source=from;expected_target=to;
    got_source[0]=got_target[0]=0;
    entries[0].value=1;entries[1].value=2;entries[2].value=replacing?2:0;entries[3].value=91;entries[4].value=92;
}
'''
suffix = r'''
static BOOL operation(int replacing,struct DosLibrary *base) { return replacing?ReplaceObject(expected_source,expected_target,base):Rename(expected_source,expected_target,base); }
int main(int argc,char **argv) {
    (void)argv;struct DosLibrary base={0};
    for(int replacing=0;replacing<2;replacing++) {
        reset("SRC:file","DST:target",replacing);
        assert(operation(replacing,&base));
        assert(entries[2].value==1&&entries[0].value==0&&entries[3].value==91&&entries[4].value==92);
        assert(!procs&&!names&&packets==1&&error==0);
        if(argc>1)continue;
        reset("file","target",replacing);entries[1].value=replacing?2:0;
        assert(operation(replacing,&base)&&entries[1].value==1&&entries[0].value==0);
        assert(!strcmp(got_source,"file")&&!strcmp(got_target,"target"));
        reset("SYS:file","SYS:target",replacing);entries[4].value=replacing?92:0;
        assert(operation(replacing,&base)&&entries[4].value==91&&entries[3].value==0);
        reset("SRC:dir:child","DST:",replacing);capture_only=1;
        assert(operation(replacing,&base));assert(!strcmp(got_source,"dir:child")&&!strcmp(got_target,""));
        reset(":","target",replacing);capture_only=1;
        assert(operation(replacing,&base));assert(!strcmp(got_source,""));
        reset("/file","target",replacing);capture_only=1;
        assert(operation(replacing,&base));assert(!strcmp(got_source,"/file"));
        reset("SRC:file","DST:target",replacing);fail_device=1;
        assert(!operation(replacing,&base)&&error==218&&!procs&&!names&&!packets&&!allocs);
        reset("SRC:file","DST:target",replacing);fail_device=2;
        assert(!operation(replacing,&base)&&error==218&&!procs&&!names&&!packets&&!allocs);
        reset("SRC:file","OTHER:target",replacing);
        assert(!operation(replacing,&base)&&error==215&&!procs&&!names&&!packets&&!allocs);
        reset("SRC:file","DST:target",replacing);fail_packet=1;
        assert(!operation(replacing,&base)&&error==224&&!procs&&!names&&packets==1);
        assert(entries[0].value==1&&entries[2].value==(replacing?2:0)&&entries[4].value==92);
#ifndef FAST
        for(int at=1;at<=2;at++) {
            reset("SRC:file","DST:target",replacing);fail_alloc=at;
            assert(!operation(replacing,&base)&&error==103&&!procs&&!names&&!packets);
            assert(entries[0].value==1&&entries[2].value==(replacing?2:0)&&entries[4].value==92);
        }
#endif
    }
    puts("PASS actual Rename/ReplaceObject rooted assigns and preserved wrong-root shadows, relative/root paths and balanced FAST/classic failure ownership");
}
'''

def code(replace_text,rename_text):
    return prefix + '\n#ifndef FAST\n'+classic+'\n#endif\n'+helper+'\nBOOL ReplaceObject(CONST_STRPTR source, CONST_STRPTR target, struct DosLibrary *DOSBase) {\n'+body(replace_text)+'\n}\nLONG Rename(CONST_STRPTR oldName, CONST_STRPTR newName, struct DosLibrary *DOSBase) {\n'+body(rename_text)+'\n}\n'+suffix

with tempfile.TemporaryDirectory(prefix='rename-replace-paths-') as temporary:
    work=Path(temporary)
    for profile,flags in [('classic',[]),('fast',['-DFAST'])]:
        source=work/(profile+'.c');source.write_text(code(replacement,rename))
        binary=work/profile
        command=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-g','-fsanitize=address,undefined',*flags,str(source),'-o',str(binary)]
        subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
        # Mutate only prefix transport back to the former full-name pattern.
        negative_replace=replacement.replace('C2BSTR(source_relative ? source_relative + 1 : source)','C2BSTR(source)').replace('C2BSTR(target_relative ? target_relative + 1 : target)','C2BSTR(target)')
        negative_rename=rename.replace('C2BSTR(new_relative ? new_relative + 1 : newName)','C2BSTR(newName)').replace('C2BSTR(old_relative ? old_relative + 1 : oldName)','C2BSTR(oldName)')
        for label,old_replace,old_rename in [('replace',negative_replace,rename),('rename',replacement,negative_rename)]:
            negative=work/(profile+'-'+label+'.c');negative.write_text(code(old_replace,old_rename))
            exe=work/(profile+'-'+label)
            subprocess.run(command[:-3]+['-Wno-unused-but-set-variable',str(negative),'-o',str(exe)],check=True)
            result=subprocess.run([str(exe),'negative'],capture_output=True)
            assert result.returncode, 'full-prefix negative control must lose the resolved assign/shadow oracle'
            print('PASS negative full-prefix control rejected:',profile,label)
