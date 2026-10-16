#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Run the actual PrefsEditor lifecycle with a recording DOS/MUI backend."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]/'workbench/classes/zune/prefseditor'
source=(root/'prefseditor.c').read_bytes().decode('latin1')
functions=source[source.index('IPTR PrefsEditor__MUIM_Setup'):source.index('IPTR PrefsEditor__OM_SET')]
private=(root/'prefseditor_private.h').read_bytes().decode('latin1')
data=private[private.index('struct PrefsEditor_DATA'):private.index('#define BACKUP_PREFIX')]
fixture=r'''#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
typedef int BOOL; typedef uintptr_t IPTR; typedef unsigned long ULONG;
typedef char *STRPTR; typedef char TEXT; typedef intptr_t BPTR;
typedef unsigned int Object; typedef void Class; typedef void *Msg;
#define TRUE 1
#define FALSE 0
#define BNULL 0
#define BACKUP_PREFIX "T:PT"
#define ENV 1
#define ENVARC 2
#define SHARED_LOCK 0
#define ID_WRITE_PROTECTED 80
#define MUIM_PrefsEditor_ImportFromDirectory 1
#define MUIM_PrefsEditor_SetDefaults 2
#define MUIM_PrefsEditor_ExportFH 3
#define MUIA_PrefsEditor_Changed 4
struct MUIP_Setup { int id; }; struct MUIP_Cleanup { int id; };
struct InfoData { int id_DiskState; };
''' + data + r'''
static struct PrefsEditor_DATA instance;
static unsigned imports, exports, closes, deletes;
static int super_ok=1;
#define SETUP_INST_DATA struct PrefsEditor_DATA *data=&instance
#define SET(o,a,v) ((void)0)
static IPTR DoSuperMethodA(Class *cl,Object *o,Msg msg) { return super_ok; }
static IPTR DoMethod(Object *o,IPTR method,...) {
 if(method==MUIM_PrefsEditor_ImportFromDirectory) ++imports;
 if(method==MUIM_PrefsEditor_ExportFH) ++exports;
 return TRUE;
}
static BPTR CreateTemporary(char *out,const char *prefix) { return 17; }
static BPTR Lock(const char *name,int mode) { return 1; }
static int Info(BPTR lock,struct InfoData *info) { info->id_DiskState=0; return 1; }
static void UnLock(BPTR lock) {}
static void Close(BPTR file) { assert(file==17); ++closes; }
static void DeleteFile(const char *name) { ++deletes; }
static void FreeVec(void *p) { free(p); }
''' + functions + r'''
int main(void) {
 Object object=0; struct MUIP_Setup setup={0}; struct MUIP_Cleanup cleanup={0};
 super_ok=0; assert(!PrefsEditor__MUIM_Setup(NULL,&object,&setup));
 assert(!instance.ped_Loaded && !imports && !exports);
 super_ok=1; assert(PrefsEditor__MUIM_Setup(NULL,&object,&setup));
 assert(imports==1 && exports==1 && instance.ped_BackupFH==17);
 instance.ped_Changed=TRUE;
 PrefsEditor__MUIM_Cleanup(NULL,&object,&cleanup);
 assert(closes==0 && deletes==0);
 assert(PrefsEditor__MUIM_Setup(NULL,&object,&setup));
 assert(imports==1 && exports==1 && instance.ped_Changed);
 PrefsEditor__OM_DISPOSE(NULL,&object,NULL);
 assert(closes==1 && deletes==1 && instance.ped_BackupFH==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    c=Path(directory)/'lifecycle.c';binary=Path(directory)/'lifecycle'
    c.write_text(fixture)
    subprocess.run(['clang','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('prefseditor-lifecycle result=PASS import-once=1 snapshot-preserved=1 changed-preserved=1 dispose-close=1')
