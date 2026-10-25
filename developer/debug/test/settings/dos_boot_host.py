#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual DOS boot body and Apple policy, with external OS services bounded."""
from pathlib import Path
import subprocess,tempfile
core=Path(__file__).resolve().parents[4]
source=(core/'rom/dos/boot.c').read_text()
actual=source[source.index('void __dos_Boot('):]
policy=(core/'arch/aarch64-apple/dos/dos_platform.h').read_text()
make=(core/'arch/aarch64-apple/dos/mmakefile.src').read_text()
assert 'modname=dos pri=10 arch=apple-aarch64' in make
assert 'mainmmake=kernel-dos maindir=rom/dos' in make
assert 'DOS_REUSE_BOOT_PROCESS' not in policy
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
typedef uintptr_t IPTR,BPTR;typedef uint32_t ULONG;typedef int32_t LONG;
typedef uint8_t UBYTE;typedef char BYTE;typedef char *STRPTR;typedef const char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define BNULL 0
#define BF_NO_DISPLAY_DRIVERS 1
#define BF_NO_COMPOSITION 2
#define BF_NO_STARTUP_SEQUENCE 4
#define BF_EMERGENCY_CONSOLE 8
#define BF_NO_BOOT_REQUESTERS 16
#define EBF_SILENTSTART 1
#define MODE_OLDFILE 1
#define MODE_NEWFILE 2
#define BUF_FULL 1
#define BUF_LINE 2
#define AROS_STACKSIZE 32768
#define TAGGEDOPEN_INTUITION 1
#define TAGGEDOPEN_GRAPHICS 2
#define AT_DeadEnd 1
#define AN_BootStrap 2
#define AN_NoWindow 4
#define SECURITY_ACTIVE 0
#define D(x)
#define BADDR(x) ((void *)(x))
enum {TAG_END=0,NP_Name,NP_WindowPtr,SYS_Background,SYS_Asynch,SYS_Input,SYS_Output,SYS_ScriptInput};
struct DosLibrary {int unused;};struct Library{int unused;};struct IntuitionBase{int unused;};
struct Screen{int unused;};struct Window{int unused;};struct Hook{int unused;};struct BootScrData{int unused;};
struct FileHandle{ULONG fh_Flags;int console;};static struct FileHandle handles[16];
static unsigned used,visible,prefs,cli_calls,serials,alerts;
static int native,interactive,fail_cli,script_missing;
static BPTR Open(CONST_STRPTR name,int mode){
    (void)mode;
    if(!strcmp(name,"S:Security-Startup"))return 0;
    if(!strcmp(name,"S:Startup-Sequence")&&script_missing)return 0;
    assert(used<16);
    handles[used].console=!strncmp(name,"CON:",4)||!strncmp(name,"ECON:",5);
    return (BPTR)&handles[used++];
}
static BPTR DupLockFromFH(BPTR h){return h;}
static BPTR OpenFromLock(BPTR h){return h;}
static int Close(BPTR h){assert(h);return 1;}
static BPTR SelectInput(BPTR h){return h;}static BPTR SelectOutput(BPTR h){return h;}
static BPTR LoadSeg(CONST_STRPTR name){(void)name;return 0;}
static void UnLoadSeg(BPTR h){(void)h;}
static void RunCommand(BPTR h,int stack,STRPTR args,size_t len){(void)h;(void)stack;(void)args;(void)len;}
static void load_system_configuration(struct DosLibrary *base){(void)base;}
static struct Library *TaggedOpenLibrary(int type){(void)type;return NULL;}
static struct Screen *OpenBootScreen(struct IntuitionBase *a,struct Library *b,struct Window **c,struct Hook *d,struct BootScrData *e){(void)a;(void)b;(void)c;(void)d;(void)e;abort();}
static void CloseBootScreen(struct DosLibrary *a,struct IntuitionBase *b,struct Library *c,struct Screen *d,struct Window *e,struct BootScrData *f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;abort();}
static void CloseWorkBench(void){}static void CloseLibrary(struct Library *base){(void)base;}
static void Alert(unsigned code){(void)code;alerts++;}
static void bug(const char *format,...){if(strstr(format,"DOS-BOOT"))serials++;}
static char *generate_banner(void){char *s=malloc(8);strcpy(s,"AROS\n");return s;}
static void FreeVec(void *p){free(p);}
static int SetVBuf(BPTR h,void *p,int mode,size_t n){(void)h;(void)p;(void)mode;(void)n;return 0;}
static void FPuts(BPTR h,const char *s){assert(s);if(((struct FileHandle*)h)->console)visible++;}
static LONG SystemTags(void *command,...){
    va_list ap;int tag;BPTR input=0,output=0,script=0;
    (void)command;va_start(ap,command);
    while((tag=va_arg(ap,int))!=TAG_END){
        switch(tag){
        case NP_Name:assert(!strcmp(va_arg(ap,const char*),"Initial CLI"));break;
        case NP_WindowPtr:(void)va_arg(ap,IPTR);break;
        case SYS_Input:input=va_arg(ap,BPTR);break;
        case SYS_Output:output=va_arg(ap,BPTR);break;
        case SYS_ScriptInput:script=va_arg(ap,BPTR);break;
        case SYS_Background:case SYS_Asynch:assert(va_arg(ap,int)==FALSE);break;
        default:abort();
        }
    }
    va_end(ap);cli_calls++;assert(input&&output);
    if(native&&!interactive){assert(visible==0);assert(!((struct FileHandle*)input)->console);assert(!((struct FileHandle*)output)->console);}
    if(script) {
        /* External script dispatch. Production IPrefs ordering is exercised
           independently by iprefs_initial_host.py; this does not emulate OS. */
        prefs=200;
        assert(prefs==200);
    }
    return fail_cli?-1:0;
}
'''
suffix=r'''
static void run_case(int interactive_mode,int failure,int missing){
    struct DosLibrary base={0};
    used=visible=prefs=cli_calls=serials=alerts=0;
    interactive=interactive_mode;fail_cli=failure;script_missing=missing;
    __dos_Boot(&base,BF_NO_DISPLAY_DRIVERS|BF_NO_COMPOSITION|(interactive_mode==1?BF_NO_STARTUP_SEQUENCE:0)|(interactive_mode==2?BF_EMERGENCY_CONSOLE:0),0);
    assert(cli_calls==1);
    assert(alerts==(unsigned)failure);
    if(native&&!interactive)assert(visible==0);
    else assert(visible>0);
    if(!interactive&&!missing)assert(prefs==200);
    if(native&&!interactive)assert(serials==3);
}
int main(void){
    native=NATIVE;
    run_case(0,0,0);run_case(1,0,0);run_case(2,0,0);run_case(0,1,0);run_case(0,0,1);
    /* Ordinary post-bootstrap CON is unaffected by the platform hook. */
    BPTR shell=Open("CON:40/60/1100/620/AROS Shell/CLOSE",MODE_NEWFILE);
    unsigned before=visible;FPuts(shell,"ready");assert(visible==before+1);Close(shell);
    puts("dos-boot-host PASS actual-full-boot-body native-quiet=1 generic-preserved=1 interactive-emergency=1 cli-launch-failure=1 missing-script=1 ordinary-CON=1 serial=1");
}
'''
variants=[('apple',policy,actual,1,True),('generic','',actual,0,True),
('policy-ignored','',actual,1,False),
('early-console',policy,actual.replace('cis = Open(name, MODE_OLDFILE);','cis = Open("CON:////AROS/AUTO/CLOSE/SMART/BOOT", MODE_OLDFILE);'),1,False),
('native-default-console',policy.replace(': "NIL:"',': "CON:////AROS/AUTO/CLOSE/SMART/BOOT"'),actual,1,False),
('missing-serial',policy,actual.replace('bug("[DOS-BOOT] initial-cli returned=%ld\\n", shellrc);',''),1,False)]
with tempfile.TemporaryDirectory(prefix='dos-boot-') as temp:
    path=Path(temp);unit=path/'fixture.c';binary=path/'fixture'
    for name,header,body,native,success in variants:
        unit.write_text(prefix+header+body+suffix.replace('NATIVE',str(native)))
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
        r=subprocess.run([str(binary)],text=True,capture_output=True)
        if success:
            assert r.returncode==0,(name,r.stderr)
            print(name,r.stdout.strip())
        else:assert r.returncode!=0,name+' escaped'
    print('dos-boot-host mutants=4 rejected=4; OS script/GUI adapters are not full-startup runtime proof')

# The second real early-open source: pre-IPrefs FontDiag must not invoke
# LockPubScreen's OpenWorkBench fallback for an absent/private screen.
fontdiag=(core/'arch/aarch64-apple/c/FontDiag.c').read_text()
assert 'wb = FontDiag_LockExistingWorkbench();' in fontdiag
assert 'wb = LockPubScreen("Workbench");' not in fontdiag
lookup=(core/'arch/aarch64-apple/c/fontdiag_workbench.h').read_text()
lookup_prefix=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define NULL ((void*)0)
#define PSNF_PRIVATE 1
struct Node {struct Node *ln_Succ;char *ln_Name;};
struct List {struct Node *lh_Head;};
struct Screen {int unused;};
struct PubScreenNode {struct Node psn_Node;struct Screen *psn_Screen;unsigned psn_Flags;};
static struct Node tail={0};static struct Screen wb;
static struct PubScreenNode entry={{&tail,"Workbench"},&wb,0};
static struct List list;
static int depth,visitors,opens,attempts,removed,race;
static void try_close(void){attempts++;if(!depth&&!visitors){list.lh_Head=&tail;removed=1;}}
static struct List *LockPubScreenList(void){depth++;return &list;}
static void UnlockPubScreenList(void){assert(depth>0);depth--;if(race)try_close();}
static struct Screen *LockPubScreen(const char *name){
    assert(!strcmp(name,"Workbench"));
    if(race)try_close();
    if(list.lh_Head==&tail||entry.psn_Flags&PSNF_PRIVATE){opens++;return &wb;}
    visitors++;return &wb;
}
static void UnlockPubScreen(void){assert(visitors==1);visitors--;if(race)try_close();}
'''
lookup_suffix=r'''
int main(void){
    list.lh_Head=&tail;
    assert(!FontDiag_LockExistingWorkbench());assert(!opens&&!depth);
    list.lh_Head=&entry.psn_Node;entry.psn_Flags=PSNF_PRIVATE;
    assert(!FontDiag_LockExistingWorkbench());assert(!opens&&!depth);
    entry.psn_Flags=0;
    assert(FontDiag_LockExistingWorkbench()==&wb);assert(visitors==1&&!opens&&!depth);
    UnlockPubScreen();
    race=1;
    assert(FontDiag_LockExistingWorkbench()==&wb);
    assert(attempts&&visitors==1&&!opens&&!removed&&!depth);
    UnlockPubScreen();assert(removed);
    puts("fontdiag-existing-host PASS absent=1 private=1 visitor-pin=1 close-race=1 no-OpenWorkbench=1");
}
'''
lookup_variants=[('production',lookup,True),
    ('open-absent',lookup.replace('struct Screen *screen = NULL;', 'struct Screen *screen = LockPubScreen("Workbench");'),False),
    ('unlock-before-pin',lookup.replace('screen = LockPubScreen("Workbench");', 'UnlockPubScreenList(); screen = LockPubScreen("Workbench"); LockPubScreenList();'),False),
    ('private-screen',lookup.replace('&& !(node->psn_Flags & PSNF_PRIVATE)', ''),False)]
with tempfile.TemporaryDirectory(prefix='fontdiag-existing-') as temp:
    path=Path(temp);unit=path/'fixture.c';binary=path/'fixture'
    for name,body,success in lookup_variants:
        unit.write_text(lookup_prefix+body+lookup_suffix)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-macro-redefined','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
        r=subprocess.run([str(binary)],capture_output=True,text=True)
        if success:
            assert r.returncode==0,r.stderr
            print(r.stdout.strip())
        else:assert r.returncode!=0,name+' escaped'
    print('fontdiag-existing-host mutants=3 rejected=3')
