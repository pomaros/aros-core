#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Exercise the production disposable-device guard with a colliding volume."""
from pathlib import Path
import os
import subprocess
import tempfile

source = Path(__file__).resolve().parents[1] / 'afsprobe_device.h'
text = source.read_text()
function = text[text.index('static int afsprobe_bound_device('):text.rindex('#endif')]
prelude = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t IPTR;
typedef uintptr_t BPTR;
typedef const char *CONST_STRPTR;
#define BNULL 0
#define DLT_DEVICE 0
#define DLT_DIRECTORY 1
#define DLT_VOLUME 2
#define DVPF_ASSIGN 2
#define LDF_DEVICES 4
#define LDF_READ 1
#define BADDR(v) ((void *)(uintptr_t)(v))
#ifdef CLASSIC_BSTR
#define AROS_BSTR_strlen(v) (*(const unsigned char *)(uintptr_t)(v))
#define AROS_BSTR_ADDR(v) ((const char *)(uintptr_t)(v)+1)
#else
#define AROS_FAST_BSTR 1
#define AROS_BSTR_ADDR(v) ((const char *)(uintptr_t)(v))
#endif
static BPTR make_bstr(const char *text,unsigned char length) {
#ifdef CLASSIC_BSTR
 static unsigned char storage[16][32]; static unsigned index;
 unsigned char *p=storage[index++]; assert(index<=16); p[0]=length; strcpy((char *)p+1,text); return (BPTR)(uintptr_t)p;
#else
 (void)length; return (BPTR)(uintptr_t)text;
#endif
}
struct MsgPort { int unused; };
struct FileSysStartupMsg { IPTR fssm_Unit; BPTR fssm_Device; };
struct DosList { int dol_Type; struct MsgPort *dol_Task;
 union { struct { BPTR dol_Startup; } dol_handler; BPTR poisonous_volume_data; } dol_misc; };
struct DevProc { struct MsgPort *dvp_Port; BPTR dvp_Lock; unsigned dvp_Flags; struct DosList *dvp_DevNode; };
static struct DosList mount;
static int locks, calls;
static struct DosList *LockDosList(int flags) { assert(flags==(LDF_DEVICES|LDF_READ)); assert(locks==0); locks++; return &mount; }
static void UnLockDosList(int flags) { assert(flags==(LDF_DEVICES|LDF_READ)); assert(locks==1); locks--; }
static struct DosList *FindDosEntry(struct DosList *list, CONST_STRPTR name, int flags) {
 assert(list==&mount && locks==1 && flags==LDF_DEVICES); calls++;
 return !strcmp(name,"AFSPROBE")?&mount:NULL;
}
'''
harness = r'''
int main(void) {
 struct MsgPort port={0},other={0};
 struct FileSysStartupMsg startup={91,make_bstr("fdsk.device",11)};
 struct DosList volume={DLT_VOLUME,&port,{.poisonous_volume_data=0x123}};
 struct DevProc dp={&port,0,0,&volume};
 mount=(struct DosList){DLT_DEVICE,&port,{.dol_handler={(BPTR)(uintptr_t)&startup}}};
 assert(afsprobe_bound_device(&dp,91,"AFSPROBE")); assert(locks==0 && calls==1);
 dp.dvp_DevNode=&mount; assert(afsprobe_bound_device(&dp,91,"AFSPROBE"));
 dp.dvp_DevNode=&volume;
 startup.fssm_Unit=92; assert(!afsprobe_bound_device(&dp,91,"AFSPROBE")); startup.fssm_Unit=91;
 startup.fssm_Device=make_bstr("nvme.device",11); assert(!afsprobe_bound_device(&dp,91,"AFSPROBE"));
 startup.fssm_Device=make_bstr("fdsk.deviceX",12); assert(!afsprobe_bound_device(&dp,91,"AFSPROBE"));
 startup.fssm_Device=make_bstr("fdsk.devic",10); assert(!afsprobe_bound_device(&dp,91,"AFSPROBE"));
 startup.fssm_Device=make_bstr("fdsk.device",12); assert(afsprobe_bound_device(&dp,91,"AFSPROBE"));
 startup.fssm_Device=make_bstr("fdsk.device",11);
 mount.dol_Task=&other; assert(!afsprobe_bound_device(&dp,91,"AFSPROBE")); mount.dol_Task=&port;
 assert(!afsprobe_bound_device(&dp,91,"AFSDENY"));
 dp.dvp_Lock=1; assert(!afsprobe_bound_device(&dp,91,"AFSPROBE")); dp.dvp_Lock=0;
 dp.dvp_Flags=DVPF_ASSIGN; assert(!afsprobe_bound_device(&dp,91,"AFSPROBE")); dp.dvp_Flags=0;
 volume.dol_Type=DLT_DIRECTORY; assert(!afsprobe_bound_device(&dp,91,"AFSPROBE")); volume.dol_Type=DLT_VOLUME;
 mount.dol_misc.dol_handler.dol_Startup=0; assert(!afsprobe_bound_device(&dp,91,"AFSPROBE"));
 assert(!afsprobe_bound_device(NULL,91,"AFSPROBE")); assert(locks==0);
 puts("afssnapshot-identity result=PASS cases=14 lock-balance=0 volume-union=unread");
}
'''
with tempfile.TemporaryDirectory(prefix='afs-device-identity-') as temporary:
    folder = Path(temporary)
    for mode in ('fast', 'classic'):
        variants = [('current',function),('device-only-negative',function.replace(
            '(device->dvp_DevNode->dol_Type!=DLT_DEVICE && device->dvp_DevNode->dol_Type!=DLT_VOLUME)',
            '(device->dvp_DevNode->dol_Type!=DLT_DEVICE)'))]
        if mode == 'classic':
            variants.append(('nul-length-negative', function.replace(
                'if(length==12 && AROS_BSTR_ADDR(startup->fssm_Device)[11]==0)length=11;', '')))
        for name, body in variants:
            path = folder / (mode+'-'+name+'.c')
            path.write_text(prelude+body+harness)
            binary = folder / (mode+'-'+name)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined', *(['-DCLASSIC_BSTR'] if mode=='classic' else []),
                            str(path),'-o',str(binary)],check=True)
            result = subprocess.run([str(binary)],capture_output=True,text=True)
            if name=='current':
                if result.returncode:
                    raise RuntimeError(result.stderr)
                print(mode+' '+result.stdout.strip())
            else:
                assert result.returncode != 0, name+' escaped negative control'
                print('afssnapshot-identity mode='+mode+' negative='+name+' result=REJECTED')
