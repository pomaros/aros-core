#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Execute actual packethelper code with FAST/classic ownership and path cases."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[4]
source=(root/'rom/dos/packethelper.c').read_text()
body=source[source.index('BOOL getpacketinfo('):]
prelude=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int BOOL;
typedef int LONG;
typedef uintptr_t BPTR;
typedef uintptr_t BSTR;
typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define DOSFALSE 0
#define BNULL 0
#define ERROR_NO_FREE_STORE 103
#define ERROR_DEVICE_NOT_MOUNTED 218
#define DLT_DEVICE 0
#define BADDR(v) ((void *)(uintptr_t)(v))
#define ASSERT_VALID_PROCESS(v) assert(v==&process)
struct MsgPort { int id; };
struct FileLock { struct MsgPort *fl_Task; };
struct Process { BPTR pr_CurrentDir; };
struct DosList { int dol_Type; };
struct DevProc { struct MsgPort *dvp_Port; BPTR dvp_Lock; struct DosList *dvp_DevNode; };
struct Root { struct MsgPort *rn_BootProc; };
struct DosLibrary { struct Root *dl_Root; };
struct PacketHelperStruct { BSTR name; struct MsgPort *port; BPTR lock; struct DevProc *dp; };
static struct Process process;
static struct MsgPort parent_port={1},boot_port={2};
static struct DosList device={DLT_DEVICE};
static unsigned names,procs,conversions;
static int fail_name,fail_device,error;
static char original[128];
static void *FindTask(void *unused) {assert(unused==NULL);return &process;}
static void SetIoErr(int value) {error=value;}
static int IoErr(void) {return error;}
#ifdef CLASSIC
static BSTR C2BSTR(CONST_STRPTR text) {
 unsigned char *p; size_t n=strlen(text); conversions++;
 if(fail_name) return 0;
 p=malloc(n+2);assert(p);p[0]=(unsigned char)n;memcpy(p+1,text,n+1);names++;return (BSTR)(uintptr_t)p;
}
static void FREEC2BSTR(BSTR name) {if(name){assert(names);names--;free(BADDR(name));}}
static const char *text_of(BSTR name) {return (const char *)BADDR(name)+1;}
#else
#define C2BSTR(v) ((BSTR)(uintptr_t)(v))
#define FREEC2BSTR(v) ((void)(v))
static const char *text_of(BSTR name) {return (const char *)BADDR(name);}
#endif
static struct DevProc *GetDeviceProc(CONST_STRPTR name,struct DevProc *old) {
 struct DevProc *p;assert(old==NULL);strcpy(original,name);
 if(fail_device){error=205;return NULL;}
 p=malloc(sizeof(*p));assert(p);procs++;
 *p=(struct DevProc){&parent_port,!strncmp(name,"ASSIGN:",7)?123:0,&device};return p;
}
static void FreeDeviceProc(struct DevProc *p) {assert(procs);procs--;free(p);error=0;}
'''
harness=r'''
static void check(struct DosLibrary *base,const char *input,const char *expected,BPTR parent) {
 struct PacketHelperStruct helper;
 assert(getpacketinfo(base,input,&helper));assert(!strcmp(text_of(helper.name),expected));
 assert(helper.lock==parent && helper.port==&parent_port);
 if(strchr(input,':'))assert(helper.dp && !strcmp(original,input));else assert(!helper.dp);
 error=224;freepacketinfo(base,&helper);assert(error==224 && names==0 && procs==0);
}
int main(void) {
 struct Root boot={&boot_port};struct DosLibrary base={&boot};
 struct FileLock current={&parent_port};struct PacketHelperStruct helper;
 unsigned before;
 fail_name=0; process.pr_CurrentDir=(BPTR)(uintptr_t)&current;
 check(&base,"ASSIGN:Sys/font","Sys/font",123);
 check(&base,"ASSIGN:Sys","Sys",123);
 check(&base,"SYS:object","object",0);
 check(&base,"SYS:","",0);
 check(&base,":object","object",0);
 check(&base,":","",0);
 check(&base,"ASSIGN:part:tail","part:tail",123);
 check(&base,"relative/name","relative/name",process.pr_CurrentDir);
 process.pr_CurrentDir=0;
 assert(getpacketinfo(&base,"relative",&helper));assert(helper.port==&boot_port && helper.lock==0);freepacketinfo(&base,&helper);
 process.pr_CurrentDir=(BPTR)-1;
 assert(getpacketinfo(&base,"",&helper));assert(helper.port==&boot_port && helper.lock==0);freepacketinfo(&base,&helper);
 fail_device=1;before=conversions;
 assert(!getpacketinfo(&base,"ASSIGN:Sys",&helper));assert(error==205 && !helper.name && !helper.dp && !helper.port && !helper.lock);
 assert(before==conversions && !names && !procs);fail_device=0;
#ifdef CLASSIC
 fail_name=1;
 assert(!getpacketinfo(&base,"ASSIGN:Sys",&helper));assert(error==103 && !names && !procs && !helper.dp);
 assert(!getpacketinfo(&base,"relative",&helper));assert(error==103 && !names && !procs);
 fail_name=0;
#endif
 puts("packethelper-paths result=PASS suffix,parent,original-resolution,root,relative,ownership,failures");
}
'''
with tempfile.TemporaryDirectory(prefix='packethelper-paths-') as directory:
    folder=Path(directory)
    for mode in ('fast','classic'):
        for name,code in [('current',body),('full-prefix-negative',body.replace('C2BSTR(colon + 1)','C2BSTR(name)')),('cleanup-error-negative',body.replace('SetIoErr(error);','SetIoErr(error & 0);'))]:
            src=folder/(mode+'-'+name+'.c');binary=src.with_suffix('')
            src.write_text(prelude+code+harness)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-Wno-unused-parameter','-fsanitize=address,undefined',*(['-DCLASSIC'] if mode=='classic' else []),
                            str(src),'-o',str(binary)],check=True)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            if name=='current':
                if result.returncode:raise RuntimeError(result.stderr)
                print(mode+' '+result.stdout.strip())
            else:
                assert result.returncode!=0,'full prefix escaped regression'
                print(mode+' negative='+name+' result=REJECTED')
