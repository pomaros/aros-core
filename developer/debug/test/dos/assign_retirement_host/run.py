#!/usr/bin/env python3
# Copyright (C) 2026 John KNIPPER.
# SPDX-License-Identifier: APL-1.1
"""Execute actual assign replacement bodies with checked namespace ownership."""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[5];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
functions={}
for name in ['AssignLock','AssignPath','AssignLate']:
 s=(root/'rom/dos'/f'{name.lower()}.c').read_text()
 body=s[s.index('    AROS_LIBFUNC_INIT'):s.index('    AROS_LIBFUNC_EXIT')]
 functions[name]='static int '+name+'(const char *name,'+('BPTR lock' if name=='AssignLock' else 'const char *path')+') {\n'+body+'\n}\n'
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t BPTR; typedef void *APTR; typedef const char *CONST_STRPTR;typedef char *STRPTR;typedef unsigned long ULONG;typedef int BOOL;
#define AROS_LIBFUNC_INIT
#define BNULL 0
#define BADDR(p) ((void *)(p))
#define DOSTRUE 1
#define DOSFALSE 0
#define DLT_DEVICE 0
#define DLT_DIRECTORY 1
#define DLT_VOLUME 2
#define DLT_LATE 3
#define DLT_NONBINDING 4
#define LDF_ALL 7
#define LDF_WRITE 16
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define ERROR_NO_FREE_STORE 103
#define ERROR_OBJECT_EXISTS 203
struct FileLock {void *fl_Task;};
struct AssignList {BPTR al_Lock;struct AssignList *al_Next;};
struct DosList {int dol_Type;void *dol_Task;BPTR dol_Lock;struct {struct {struct AssignList *dol_List;char *dol_AssignName;} dol_assign;} dol_misc;};
static struct DosList *published;static int held,freed_locks,allocation_fail;
static struct FileLock filelock;
static struct DosList *MakeDosEntry(const char *n,int type){(void)n;struct DosList *d=calloc(1,sizeof(*d));assert(d);d->dol_Type=type;return d;}
static void FreeDosEntry(struct DosList *d){assert(d!=published);free(d);}
static void *AllocVec(size_t n,int f){(void)f;return allocation_fail?NULL:calloc(1,n);}
static void FreeVec(void *p){free(p);}
static void CopyMem(const void *s,void *d,size_t n){memcpy(d,s,n);}
static void SetIoErr(int e){assert(e==103||e==203);}
static struct DosList *LockDosList(int f){assert(f==(LDF_ALL|LDF_WRITE)&&!held);held=1;return published;}
static void UnLockDosList(int f){assert(f==(LDF_ALL|LDF_WRITE)&&held);held=0;}
static struct DosList *FindDosEntry(struct DosList *d,const char *n,int f){(void)d;(void)n;(void)f;assert(held);return published;}
static void AddDosEntry(struct DosList *d){assert(held);published=d;}
static void RemDosEntry(struct DosList *d){assert(held&&d==published);published=NULL;}
static void UnLock(BPTR lock){assert(!held);if(lock)++freed_locks;}
static void old_assign(void){assert(!published&&!held);published=MakeDosEntry("A",DLT_DIRECTORY);published->dol_Lock=(BPTR)&filelock;struct AssignList *m=calloc(1,sizeof(*m));assert(m);m->al_Lock=(BPTR)&filelock;published->dol_misc.dol_assign.dol_List=m;freed_locks=0;}
static void clear(void){assert(!held);if(published){assert(!published->dol_misc.dol_assign.dol_List);free(published->dol_misc.dol_assign.dol_AssignName);free(published);published=NULL;}}
'''
post=r'''
int main(void){
 old_assign();assert(AssignLock("A",(BPTR)&filelock));assert(freed_locks==2);clear();
 old_assign();assert(AssignLock("A",0));assert(!published&&freed_locks==2);
 for(int late=0;late<2;late++)for(int empty=0;empty<2;empty++){
  char *path=malloc(empty?1:5);assert(path);memcpy(path,empty?"":"RAM:",empty?1:5);
  old_assign();assert(late?AssignLate("A",path):AssignPath("A",path));assert(freed_locks==2&&!held);assert(!strcmp(published->dol_misc.dol_assign.dol_AssignName,path));clear();
  allocation_fail=1;assert(!(late?AssignLate("A",path):AssignPath("A",path)));assert(!published&&!held);allocation_fail=0;
  published=MakeDosEntry("A",DLT_DEVICE);assert(!(late?AssignLate("A",path):AssignPath("A",path)));assert(published->dol_Type==DLT_DEVICE&&!held);clear();free(path);
 }
 puts("PASS actual assign detach, primary/multi-lock retirement, exact path bounds and refusal cleanup");return 0;
}
'''
variants={'current':functions.copy()}
for name in functions:
 changed=functions.copy();body=changed[name];marker='    UnLockDosList(LDF_ALL | LDF_WRITE);';assert body.count(marker)==1;body=body.replace(marker,'',1);i=body.rindex('    return ');changed[name]=body[:i]+marker+'\n'+body[i:];variants['wait-under-write-'+name]=changed
for name in ['AssignPath','AssignLate']:
 changed=functions.copy();changed[name]=changed[name].replace('namelen = s2 - path;', 'namelen = s2 - path + 1;');variants['path-overread-'+name]=changed
rows=[]
with tempfile.TemporaryDirectory(prefix='assign-retirement-') as tmp:
 for name,bodies in variants.items():
  source=out/(name+'.c');source.write_text(pre+'\n'+''.join(bodies.values())+post);binary=Path(tmp)/name
  build=subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(source),'-o',str(binary)],capture_output=True,text=True,timeout=60);(out/(name+'-build.log')).write_text(build.stdout+build.stderr);build.check_returncode()
  run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10);(out/(name+'.log')).write_text(run.stdout+run.stderr)
  okay=run.returncode==0 if name=='current' else run.returncode!=0 and ('heap-buffer-overflow' if name.startswith('path-overread') else '(!held)') in run.stderr
  rows.append({'name':name,'exit':run.returncode,'expected':okay})
result={'result':'PASS' if all(x['expected'] for x in rows) else 'FAIL','scope':'Actual mutator bodies with modeled DOS namespace and handler-release precondition; not guest/concurrent-handler qualification','checks':rows,'sources':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'rom/dos'/f'{n.lower()}.c' for n in functions]+[Path(__file__).resolve()]}}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(result['result']!='PASS')
