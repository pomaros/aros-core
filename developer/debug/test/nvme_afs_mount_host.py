#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Execute actual NVMe boot-node builder and handler policy/parser on the host."""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--afsplus', type=Path, required=True)
a = parser.parse_args()
core = Path(__file__).resolve().parents[3]
afs = a.afsplus.resolve()


def function(path, signature):
    text = path.read_text()
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


build = function(core / 'arch/aarch64-apple/devs/applenvme/applenvme_device.c',
                 'static int add_disk_boot_node(')
env_defines = '\n'.join(line for line in (core / 'compiler/include/dos/filehandler.h').read_text(encoding='latin1').splitlines()
                        if line.startswith('#define DE_'))
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
typedef uintptr_t IPTR; typedef uint32_t ULONG; typedef int32_t LONG;
typedef unsigned char UBYTE; typedef void *APTR; typedef char *STRPTR;
typedef const char *CONST_STRPTR;
#define EXEC_TYPES_H
#include "dos/bptr.h"
#include "nvme_afs_mount_policy.h"
#include "afsplus_control.h"
#define BLOCK 4096
#define MEMF_PUBLIC 1
#define AROS_APPLE_NVME_DEVICE_NAME "applenvme.device"
struct Library { int unused; }; struct ExecBase { int unused; };
struct SecurityBase { int unused; }; struct AppleNVMeBase { int unused; };
struct DeviceNode { IPTR parameters[4+DE_BOOTBLOCKS+1]; };
static struct Library expansion;
static int fail_open, fail_node, fail_add, closes, frees;
static struct DeviceNode *registered;
static struct Library *OpenLibrary(const char *name, unsigned revision) {
    (void)revision; assert(!strcmp(name,"expansion.library"));
    return fail_open ? NULL : &expansion;
}
static void CloseLibrary(struct Library *base) { assert(base==&expansion); closes++; }
static struct DeviceNode *MakeDosNode(IPTR *parameters) {
    if(fail_node) return NULL;
    struct DeviceNode *node=malloc(sizeof(*node)); assert(node);
    memcpy(node->parameters,parameters,sizeof(node->parameters)); return node;
}
static int AddBootNode(LONG priority, int flags, struct DeviceNode *node, void *config) {
    assert(priority==7 && flags==0 && config==NULL);
    if(fail_add) return 0; registered=node; return 1;
}
static void FreeVec(void *p) { frees++; free(p); }
static void device_log(struct AppleNVMeBase *base, const char *format, ...) { (void)base; (void)format; }
'''
main = r'''
#undef OpenLibrary
#undef CloseLibrary
#ifndef EXPECTED_EDIT
#define EXPECTED_EDIT 4096
#define EXPECTED_VIEWS 8
#define EXPECTED_RECLAIM 64
#define EXPECTED_POLICY 3
#endif
int main(void) {
    struct AppleNVMeBase base={0};
    assert(add_disk_boot_node(&base,"AFSSYS",2,0,2047,0x4146532b,7));
    assert(registered->parameters[4+DE_TABLESIZE]==DE_BOOTBLOCKS);
    assert(registered->parameters[4+DE_MASK]==~(IPTR)1);
    assert(registered->parameters[4+DE_LOWCYL]==0 && registered->parameters[4+DE_HIGHCYL]==2047);
    BSTR text=(BSTR)registered->parameters[4+DE_CONTROL]; assert(text);
    struct AfsplusArosControl control;
    assert(afsplus_control_parse(AROS_BSTR_ADDR(text),(uint32_t)AROS_BSTR_strlen(text),&control)==AFSPLUS_CONTROL_OK);
    assert(control.snapshot_max_edit_records==EXPECTED_EDIT && control.snapshot_max_views==EXPECTED_VIEWS && control.snapshot_reclaim_records==EXPECTED_RECLAIM);
    assert(control.snapshot_reclaim_records<=control.snapshot_max_edit_records);
    /* ADR-135: every local task may snapshot and restore AFSSYS. */
    assert(control.backup_policy==EXPECTED_POLICY && control.restore_policy==EXPECTED_POLICY && control.format_snapshots==0);
    FreeVec(registered); registered=NULL;
    for(unsigned unit=0;unit<2;unit++) {
        assert(add_disk_boot_node(&base,unit?"WORK":"ESP",unit,23,2047,0x46415432,7));
        assert(registered->parameters[4+DE_CONTROL]==0);
        FreeVec(registered); registered=NULL;
    }
    /* The resident Control still exists after both stack-local packets are gone. */
    assert(!strcmp(AROS_BSTR_ADDR(text),AROS_APPLE_AFSSYS_CONTROL));
    int prior=closes; fail_open=1; assert(!add_disk_boot_node(&base,"AFSSYS",2,0,2047,0x4146532b,7)); assert(closes==prior); fail_open=0;
    fail_node=1; assert(!add_disk_boot_node(&base,"AFSSYS",2,0,2047,0x4146532b,7)); assert(closes==prior+1); fail_node=0;
    prior=frees; fail_add=1; assert(!add_disk_boot_node(&base,"AFSSYS",2,0,2047,0x4146532b,7)); assert(frees==prior+1); fail_add=0;
    /* Generic parser defaults remain deny with no snapshot admission supplied. */
    assert(afsplus_control_parse(NULL,0,&control)==AFSPLUS_CONTROL_OK);
    assert(control.backup_policy==0 && control.restore_policy==0 && control.snapshot_max_edit_records==0);
    puts("PASS actual boot-node Control, FAT unchanged, resident BSTR, cleanup and ANYONE policy");
}
'''
with tempfile.TemporaryDirectory(prefix='nvme-afs-control-') as temp:
    work = Path(temp)
    source = work / 'fixture.c'
    source.write_text(env_defines+'\n'+fixture.replace('#include "dos/bptr.h"', '#include "'+str(core/'compiler/include/dos/bptr.h')+'"')+'\n'+build+'\n'+main)
    common = ['clang','-std=gnu11','-g','-O1','-Wall','-Wextra','-Werror','-Wno-duplicate-decl-specifier',
              '-fsanitize=address,undefined','-fno-sanitize-recover=all',
              '-I'+str(core/'arch/aarch64-apple/devs/applenvme'),
              '-I'+str(afs/'native/aros'),'-I'+str(afs/'api')]
    for name, flags in [('classic',[]),('fast',['-DAROS_FAST_BPTR','-DAROS_FAST_BSTR']),
                        ('override',['-DAROS_FAST_BPTR','-DAROS_FAST_BSTR',
                          '-DAROS_APPLE_AFSSYS_CONTROL="SNAPSHOTEDIT=512 SNAPSHOTVIEWS=3 SNAPSHOTRECLAIM=16 BACKUP=ROOT RESTORE=ROOT"',
                          '-DEXPECTED_EDIT=512','-DEXPECTED_VIEWS=3','-DEXPECTED_RECLAIM=16','-DEXPECTED_POLICY=1'])]:
        binary=work/name
        subprocess.run(common+flags+[str(source),str(afs/'native/aros/afsplus_control.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    # Explicit administrative build literals must remain representable by classic BSTR.
    too_long=common+['-DAROS_APPLE_AFSSYS_CONTROL="'+'x'*256+'"','-fsyntax-only',str(source)]
    result=subprocess.run(too_long,capture_output=True,text=True)
    assert result.returncode and 'AFSSYS Control must fit' in result.stderr
    print('PASS overlong deployment literal rejected at compile time')
