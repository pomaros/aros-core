#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>
# Licensed under the AROS Public License (APL) Version 1.1.
"""Run production FAT inhibit and clean-bit code with injected I/O failures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
packet = (root / 'rom/filesys/fat/packet.c').read_text()
validate = (root / 'rom/filesys/fat/validate.c').read_text()
case = packet[packet.index('        case ACTION_INHIBIT:'):packet.index('        case ACTION_DIE:')]

def function(signature):
    start = validate.index(signature)
    return validate[start:validate.index('\n}\n', start) + 3]

production = '\n'.join(function(s) for s in (
    'static ULONG clean_bit(', 'static void set_clean_bit(',
    'void MarkVolumeDirty(', 'BOOL MarkVolumeClean('))
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
typedef long LONG;
typedef unsigned long ULONG;
#define TRUE 1
#define FALSE 0
#define DOSTRUE -1
#define DOSFALSE 0
#define ERROR_UNKNOWN 99
#define CMD_UPDATE 4
#define ACTION_INHIBIT 31
#define FAT16_CLEAN_SHUTDOWN 0x8000
#define FAT32_CLEAN_SHUTDOWN 0x08000000
#define D(x)
struct IORequest { int io_Command; void *io_Data; int io_Length; };
struct DiskIO { struct IORequest iotd_Req; };
struct FSSuper { int type, volume_dirty, suppress_dirty, needs_validation, fat_io_error; void *cache; };
struct Globals { struct FSSuper *sb; int disk_inhibited; struct DiskIO *diskioreq; };
struct Packet { int dp_Arg1; };
static ULONG memory_fat[2], disk_fat[2];
static int fail_flush, flushes, fail_get, fail_set, fail_update, updates, removes, inserts;
static int ioerr, data_pending, data_persisted;
static char order[64];
static void event(char ch) { size_t n=strlen(order); assert(n+1<sizeof(order)); order[n]=ch; order[n+1]=0; }
static ULONG get_entry(struct FSSuper *sb, int entry)
{
    assert(entry==1); event('R');
    if(fail_get) { sb->fat_io_error=TRUE; return 0; }
    return memory_fat[0];
}
static BOOL set_entry(struct FSSuper *sb, int entry, ULONG value)
{
    assert(entry==1); event(value & (sb->type==16?0x8000:0x08000000)?'C':'D');
    memory_fat[0]=value;
    if(fail_set) { sb->fat_io_error=TRUE; return FALSE; }
    memory_fat[1]=value; return TRUE;
}
#define GET_NEXT_CLUSTER(sb,n) get_entry(sb,n)
#define SET_NEXT_CLUSTER(sb,n,v) set_entry(sb,n,v)
static BOOL Cache_Flush(void *cache)
{
    (void)cache; event('F'); ++flushes;
    if(flushes==fail_flush) { ioerr=77; return FALSE; }
    memcpy(disk_fat,memory_fat,sizeof(disk_fat));
    if(data_pending) { data_persisted=1; data_pending=0; }
    ioerr=0; return TRUE;
}
static int IoErr(void) { return ioerr; }
static int DoIO(struct IORequest *io)
{
    assert(io->io_Command==CMD_UPDATE && !io->io_Data && !io->io_Length);
    event('U'); ++updates; return fail_update;
}
static void DoDiskRemove(struct Globals *glob) { event('X'); ++removes; glob->sb=NULL; }
static void ProcessDiskChange(struct Globals *glob) { (void)glob; ++inserts; }
'''
wrapper = r'''
static LONG inhibit(struct Globals *glob, int argument)
{
    struct Packet packet={argument}, *pkt=&packet;
    LONG err=0, res=DOSFALSE;
    switch(ACTION_INHIBIT) {
CASE
    }
    return err ? err : res;
}
'''.replace('CASE', case)
suffix = r'''
static struct FSSuper sb;
static struct DiskIO io;
static struct Globals glob;
static void reset(int type)
{
    memset(&sb,0,sizeof(sb)); memset(&io,0,sizeof(io));
    sb.type=type; sb.volume_dirty=TRUE; sb.cache=&sb;
    glob=(struct Globals){&sb,0,&io};
    memory_fat[0]=memory_fat[1]=disk_fat[0]=disk_fat[1]=type==32?0x07ffffff:0x7fff;
    fail_flush=flushes=fail_get=fail_set=fail_update=updates=removes=inserts=ioerr=0;
    data_pending=1; data_persisted=0; order[0]=0;
}
static void success(int type)
{
    assert(inhibit(&glob,DOSTRUE)==DOSTRUE);
    assert(!glob.sb && glob.disk_inhibited==1 && removes==1 && updates==1);
    assert(data_persisted && disk_fat[0]==disk_fat[1]);
    if(type!=12) assert(disk_fat[0] & (type==16?0x8000:0x08000000));
}
int main(void)
{
    for(int type=16;type<=32;type+=16) {
        reset(type); success(type); assert(!strcmp(order,"FRCFUX"));
        /* Already clean and FAT12 need no marker but still update the device. */
        reset(type); sb.volume_dirty=FALSE; memory_fat[0]=memory_fat[1]=type==16?0xffff:0x0fffffff;
        success(type); assert(!strcmp(order,"FFUX"));
        for(int failure=1;failure<=5;++failure) {
            reset(type);
            if(failure<=2) fail_flush=failure;
            if(failure==3) fail_get=1;
            if(failure==4) fail_set=1;
            if(failure==5) fail_update=1;
            assert(inhibit(&glob,DOSTRUE)>0);
            assert(glob.sb==&sb && glob.disk_inhibited==0 && !removes && sb.volume_dirty);
            if(failure==2 || failure==5)
                assert(!(memory_fat[0] & (type==16?0x8000:0x08000000)));
            fail_flush=fail_get=fail_set=fail_update=0; updates=0;
            success(type);
        }
        reset(type); sb.fat_io_error=TRUE; success(type); /* clear only at marker boundary */
        reset(type); sb.needs_validation=TRUE;
        assert(inhibit(&glob,DOSTRUE)==DOSTRUE); /* retain dirty validation marker */
        assert(!(disk_fat[0] & (type==16?0x8000:0x08000000)));
    }
    reset(12); success(12); assert(!strcmp(order,"FFUX"));
    assert(inhibit(&glob,DOSTRUE)==DOSTRUE && glob.disk_inhibited==2 && removes==1);
    assert(inhibit(&glob,DOSFALSE)==DOSTRUE && glob.disk_inhibited==1 && !inserts);
    assert(inhibit(&glob,DOSFALSE)==DOSTRUE && glob.disk_inhibited==0 && inserts==1);
    assert(inhibit(&glob,DOSFALSE)==DOSTRUE && !glob.disk_inhibited && inserts==1);
    puts("fat-inhibit PASS FAT12/16/32, data-before-marker, both-copies, 5-I/O-failures/retries, clean/no-volume/nested/validation");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    # The pre-fix code is a negative control for the missing marker regression.
    missing = case[:case.index('                        /* Detach stops')] + case[case.index('                        glob->diskioreq->iotd_Req.io_Command = CMD_UPDATE;'):]
    for label, body in [('production', wrapper), ('missing-marker-negative', wrapper.replace(case, missing))]:
        source = directory / (label + '.c')
        binary = directory / label
        source.write_text(prefix + production + body + suffix)
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(source), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        if label == 'production':
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr)
            print(result.stdout.strip())
        else:
            assert result.returncode != 0, 'missing clean-marker mutation was accepted'
            print('fat-inhibit missing-marker negative control rejected')
