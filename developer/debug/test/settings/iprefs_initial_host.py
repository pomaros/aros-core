#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Actual IPrefs initial dispatch, notification registration and detach ordering."""
from pathlib import Path
import subprocess
import tempfile
core=Path(__file__).resolve().parents[4]
source=(core/'workbench/c/iprefs/main.c').read_text()
def function(marker):
    start=source.index(marker); opening=source.index('{',start); end=opening+1; depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
table=source[source.index('static struct prefinfo'):source.index('/*********************************************************************************************/',source.index('static struct prefinfo'))]
actual='\n'.join(function(name) for name in ['static void StartNotifications(void)','static void ReadInitialPreferences(void)','static void HandleNotify(void)','int main(void)']).replace('int main(void)','static int ProductionMain(void)')
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL; typedef char *STRPTR; typedef int16_t WORD; typedef uint32_t ULONG; typedef void *APTR;
#define TRUE 1
#define FALSE 0
#define NRF_SEND_MESSAGE 1
#define NRF_NOTIFY_INITIAL 2
#define D(x)
struct MsgPort { unsigned mp_SigBit; } port={3};
struct Message { int unused; };
struct NotifyRequest { STRPTR nr_Name; ULONG nr_UserData,nr_Flags; struct { struct {struct MsgPort *nr_Port;} nr_Msg;} nr_stuff; };
struct NotifyMessage { struct Message nm_ExecMessage; struct NotifyRequest *nm_NReq; } message;
struct Process { APTR pr_WindowPtr; } process={(APTR)42};
static char inputprefsname[256],localeprefsname[256],fontprefsname[256],paletteprefsname[256],patternprefsname[256],icontrolprefsname[256],screenmodeprefsname[256],serialprefsname[256],printerprefsname[256],pointerprefsname[256],overscanprefsname[256];
static char envname[256]="RAM:ENV";
static struct MsgPort *notifyport;
static ULONG notifysig;
static int watches,reads,applied,pending,detached,cleaned,initial_pass_done;
static void Cleanup(STRPTR reason){assert(!reason);cleaned=1;}
static struct MsgPort *CreateMsgPort(void){return &port;}
static int AddPart(char *to,const char *part,size_t size){assert(strlen(to)+strlen(part)+2<size);strcat(to,"/");strcat(to,part);return 1;}
static int StartNotify(struct NotifyRequest *request){assert(request->nr_Flags==NRF_SEND_MESSAGE);assert(request->nr_stuff.nr_Msg.nr_Port==&port);watches++;return TRUE;}
static void common(void){assert(watches==11);assert(process.pr_WindowPtr==(APTR)-1);assert(!detached);reads++;}
static void ScreenModePrefs_Handler(STRPTR file){assert(strstr(file,"screenmode.prefs"));common();applied=pending?250:200;}
static void FontPrefs_Handler(STRPTR file){(void)file;common();assert(applied==200);pending=1;}
#define HANDLER(name) static void name(STRPTR file){(void)file;common();assert(applied==200);}
HANDLER(InputPrefs_Handler) HANDLER(LocalePrefs_Handler) HANDLER(PalettePrefs_Handler)
HANDLER(WBPatternPrefs_Handler) HANDLER(IControlPrefs_Handler) HANDLER(SerialPrefs_Handler)
HANDLER(PrinterPrefs_Handler) HANDLER(PointerPrefs_Handler)
static void OpenLibs(void){} static void ReadTDPrefs(void){} static void GetENVName(void){}
static void PreparePatches(void){assert(watches==11);}
static struct Process *FindTask(void *unused){(void)unused;return &process;}
static void Detach(void){assert(watches==11&&reads==11&&applied==250&&initial_pass_done);assert(process.pr_WindowPtr==(APTR)42);detached=1;}
static void HandleAll(void){assert(detached);}
static void ReplyMsg(struct Message *msg){assert(msg==&message.nm_ExecMessage);}
static struct NotifyMessage *GetMsg(struct MsgPort *p);
'''
bridge=r'''
static struct NotifyMessage *GetMsg(struct MsgPort *p){
    assert(p==&port);
    initial_pass_done=1;
    if(pending==1){pending=2;message.nm_NReq=&preftable[0].nr;return &message;}
    return NULL;
}
'''
suffix=r'''
int main(void){assert(ProductionMain()==0);assert(cleaned&&detached&&applied==250);puts("iprefs-initial-host PASS actual-main/table/notifications initial-screenmode-first=1 watch-before-read=1 queued-write=1 detach-after-read=1");}
'''
variants=[('production',table+bridge+actual),
('early-detach',(table+bridge+actual).replace('    StartNotifications();','    StartNotifications();\n    Detach();')),
('skip-initial',(table+bridge+actual).replace('    ReadInitialPreferences();','')),
('initial-notify',(table+bridge+actual).replace('= NRF_SEND_MESSAGE;', '= NRF_SEND_MESSAGE | NRF_NOTIFY_INITIAL;')),
('lost-queued-change',(table+bridge+actual).replace('    HandleNotify();','')),
('late-screenmode',(table+bridge+actual).replace('    {"screenmode"   , screenmodeprefsname, ScreenModePrefs_Handler },','').replace('    {"font"', '    {"screenmode",screenmodeprefsname,ScreenModePrefs_Handler},\n    {"font"'))]
with tempfile.TemporaryDirectory(prefix='iprefs-initial-') as temp:
    path=Path(temp);unit=path/'fixture.c';binary=path/'fixture'
    for name,body in variants:
        unit.write_text(prefix+body+suffix)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(unit),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],text=True,capture_output=True)
        if name=='production':
            assert result.returncode==0,result.stderr
            print(result.stdout,end='')
        else: assert result.returncode!=0,name+' escaped'
    print('iprefs-initial-host mutants=5 rejected=5')
