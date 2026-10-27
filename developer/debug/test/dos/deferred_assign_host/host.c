/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int BOOL;
typedef uintptr_t IPTR, BPTR;
typedef const char *CONST_STRPTR;
typedef char *STRPTR;
#define TRUE 1
#define FALSE 0
#define BNULL 0
#define BADDR(x) ((void *)(x))
#define AROS_BSTR_strlen(x) strlen(x)
#define AROS_BSTR_ADDR(x) (x)
#define USERPATH_MAX 512
#define MEMF_PUBLIC 1
#define MEMF_NO_EXPUNGE 2
#define SHARED_LOCK -2
#define LDF_ALL 7
#define LDF_READ 8
#define LDF_WRITE 16
#define LDF_ENTRY 32
#define DLT_DIRECTORY 1
#define DLT_LATE 3
#define DLT_NONBINDING 4
#define ERROR_INVALID_LOCK 211
#define ERROR_NO_FREE_STORE 103
#define ERROR_BAD_NUMBER 115
#define ERROR_DEVICE_NOT_MOUNTED 218
#define ERROR_OBJECT_IN_USE 202
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_NOT_IMPLEMENTED 236
#define TAG_DONE 0
#define secT_BufferSize 1
struct MsgPort { int unused; };
struct FileLock { struct MsgPort *fl_Task; unsigned identity; };
struct TagItem { IPTR ti_Tag, ti_Data; };
struct DosList { LONG dol_Type; BPTR dol_Lock; struct MsgPort *dol_Task; char *dol_Name; union { struct {char *dol_AssignName;} dol_assign;} dol_misc; };
struct DosLibrary { ULONG assignGenerationLow, assignGenerationHigh; BOOL secure; };
struct assign_generation {ULONG low,high;};
#define IDosBase(x) (x)
#define SECURITY_ACTIVE (DOSBase->secure)
static struct DosLibrary dos;
static struct MsgPort port;
static struct DosList *node;
static unsigned reads, entry_read, entry_write, writes, buffers, locks, lock_calls, unlock_calls;
static LONG error;
static int fail_alloc, mutation, mutate_on_unlock, expand_mode, locate_missing, created;
static void unlocked(void) { assert(!reads && !entry_read && !entry_write && !writes); }
static struct DosList *LockDosList(ULONG f) {
 if(f== (LDF_ALL|LDF_READ)) {assert(!writes);++reads;}
 else if(f==(LDF_ALL|LDF_WRITE)) {unlocked();++writes;}
 else if(f==(LDF_ENTRY|LDF_READ)) {assert(reads && !entry_write);++entry_read;}
 else {assert(f==(LDF_ENTRY|LDF_WRITE)&&reads&&!entry_read&&!entry_write);++entry_write;}
 return node;
}
static void UnLockDosList(ULONG f) {
 if(f==(LDF_ALL|LDF_READ)) {assert(reads&&!entry_read&&!entry_write);--reads;}
 else if(f==(LDF_ALL|LDF_WRITE)) {assert(writes);--writes;}
 else if(f==(LDF_ENTRY|LDF_READ)) {assert(entry_read);--entry_read;}
 else {assert(f==(LDF_ENTRY|LDF_WRITE)&&entry_write);--entry_write;}
}
static struct DosList *FindDosEntry(struct DosList *ignored, CONST_STRPTR name, ULONG f) {
 (void)ignored;assert(reads&&(entry_read||entry_write)&&f==LDF_ALL);return node&&!strcmp(node->dol_Name,name)?node:NULL;
}
static void *AllocVec(size_t n, ULONG f) {assert(reads&&entry_read&&f==(MEMF_PUBLIC|MEMF_NO_EXPUNGE));if(fail_alloc)return NULL;void *p=malloc(n);assert(p);++buffers;return p;}
static void FreeVec(void *p) {if(p){assert(buffers);--buffers;free(p);}}
#define CopyMem(a,b,n) memcpy((b),(a),(n))
static LONG SetIoErr(LONG x) {LONG old=error;error=x;return old;}
static LONG IoErr(void) {return error;}
static void replace(LONG kind,const char *target);
static BPTR newlock(unsigned id) {struct FileLock *f=malloc(sizeof(*f));assert(f);*f=(struct FileLock){&port,id};++locks;return (BPTR)f;}
static void UnLock(BPTR p) {
 if(!p)return;unlocked();assert(locks);--locks;++unlock_calls;free((void *)p);
 if(mutate_on_unlock){mutate_on_unlock=0;replace(DLT_LATE,"final");}
}
static void dispose_node(void) {
 if(!node)return;
 if(node->dol_Lock){assert(locks);--locks;free((void *)node->dol_Lock);}
 free(node->dol_Name);free(node->dol_misc.dol_assign.dol_AssignName);free(node);node=NULL;
}
static void replace(LONG kind,const char *target) {
 LockDosList(LDF_ALL|LDF_WRITE);dispose_node();
 if(kind>=0){node=calloc(1,sizeof(*node));assert(node);node->dol_Type=kind;node->dol_Name=strdup("TEST");node->dol_misc.dol_assign.dol_AssignName=target?strdup(target):NULL;if(kind==DLT_DIRECTORY){node->dol_Lock=newlock(99);node->dol_Task=&port;}}
 if(++dos.assignGenerationLow==0)++dos.assignGenerationHigh;
 UnLockDosList(LDF_ALL|LDF_WRITE);
}
static BPTR Lock(CONST_STRPTR target,LONG mode) {
 unlocked();assert(mode==SHARED_LOCK);++lock_calls;
 if(mutation){int m=mutation;if(m!=4)mutation=0;replace(m==2?-1:m==3?DLT_DIRECTORY:m==5?DLT_NONBINDING:DLT_LATE,"replacement");}
 /* Consume the argument after mutation has freed the old DosList text. */
 if(!strcmp(target,"zero-error")){error=0;return BNULL;}
 if(!strcmp(target,"bad")){error=777;return BNULL;}
 if(locate_missing&&!created){error=ERROR_OBJECT_NOT_FOUND;return BNULL;}
 error=0;return newlock(!strcmp(target,"original")?1:!strcmp(target,"replacement")?2:!strcmp(target,"final")?3:7);
}
static BPTR CreateDir(CONST_STRPTR p) {unlocked();assert(!strcmp(p,"user/42"));created=1;return newlock(8);}
static BOOL secExpandPathA(CONST_STRPTR input, STRPTR output,struct TagItem *tags) {
 unlocked();assert(strchr(input,'%')&&tags[0].ti_Tag==secT_BufferSize&&tags[0].ti_Data==USERPATH_MAX);
 if(expand_mode){error=expand_mode==1?ERROR_NOT_IMPLEMENTED:777;return FALSE;}
 strcpy(output,"user/42");return TRUE;
}
#include "actual.c"
static void reset(LONG kind,const char *target) {
 unlocked();dispose_node();assert(!locks&&!buffers);memset(&dos,0,sizeof(dos));error=0;fail_alloc=mutation=mutate_on_unlock=expand_mode=locate_missing=created=0;lock_calls=unlock_calls=0;replace(kind,target);
}
static BOOL resolve(struct DosList **found,BPTR *owned) {
 LockDosList(LDF_ALL|LDF_READ);*found=node;
 BOOL ok=deviceproc_resolve_deferred(&dos,found,owned);
 assert(reads==1&&!entry_read&&!entry_write&&!writes&&!buffers);
 UnLockDosList(LDF_ALL|LDF_READ);return ok;
}
int main(void) {
 struct DosList *found;BPTR owned;
 reset(DLT_LATE,"original");assert(resolve(&found,&owned)&&found==node&&!owned&&node->dol_Type==DLT_DIRECTORY&&((struct FileLock *)node->dol_Lock)->identity==1);
 reset(DLT_NONBINDING,"original");assert(resolve(&found,&owned)&&!found&&owned&&node->dol_Type==DLT_NONBINDING);UnLock(owned);
 reset(DLT_LATE,"original");mutation=1;mutate_on_unlock=1;assert(resolve(&found,&owned)&&found==node&&!owned&&((struct FileLock *)node->dol_Lock)->identity==3&&unlock_calls==1);
 reset(DLT_LATE,"original");mutation=2;assert(!resolve(&found,&owned)&&error==ERROR_DEVICE_NOT_MOUNTED&&!node&&!owned&&!locks);
 reset(DLT_LATE,"original");mutation=3;assert(resolve(&found,&owned)&&found==node&&!owned&&((struct FileLock *)node->dol_Lock)->identity==99&&unlock_calls==1);
 reset(DLT_LATE,"original");mutation=4;assert(!resolve(&found,&owned)&&error==ERROR_OBJECT_IN_USE&&!owned&&lock_calls==64&&unlock_calls==64&&!locks);
 reset(DLT_LATE,"bad");mutation=1;assert(resolve(&found,&owned)&&!owned&&((struct FileLock *)node->dol_Lock)->identity==2&&lock_calls==2);
 reset(DLT_LATE,"original");mutation=5;assert(resolve(&found,&owned)&&!found&&owned&&node->dol_Type==DLT_NONBINDING&&((struct FileLock *)owned)->identity==2);UnLock(owned);
 reset(DLT_LATE,"zero-error");assert(!resolve(&found,&owned)&&error==ERROR_BAD_NUMBER&&!locks);
 reset(DLT_LATE,"bad");assert(!resolve(&found,&owned)&&error==777&&!locks);
 reset(DLT_LATE,NULL);assert(!resolve(&found,&owned)&&error==ERROR_INVALID_LOCK&&!lock_calls);
 reset(DLT_LATE,"original");fail_alloc=1;assert(!resolve(&found,&owned)&&error==ERROR_NO_FREE_STORE&&!lock_calls);
 reset(DLT_LATE,"original");node->dol_Lock=newlock(6);assert(resolve(&found,&owned)&&node->dol_Type==DLT_DIRECTORY&&!lock_calls&&!owned);
 reset(DLT_DIRECTORY,NULL);assert(resolve(&found,&owned)&&!owned&&!lock_calls);
 reset(DLT_NONBINDING,"user/%u");dos.secure=TRUE;locate_missing=1;assert(resolve(&found,&owned)&&owned&&created&&lock_calls==2);UnLock(owned);
 reset(DLT_NONBINDING,"user/%u");dos.secure=TRUE;expand_mode=1;assert(resolve(&found,&owned)&&owned&&lock_calls==1);UnLock(owned);
 reset(DLT_NONBINDING,"user/%u");dos.secure=TRUE;expand_mode=2;assert(!resolve(&found,&owned)&&error==777&&!lock_calls);
 unlocked();dispose_node();assert(!locks&&!buffers);
 puts("PASS actual deferred resolution: promotion, nonbinding ownership, replacement/deletion, cleanup mutation, bounded retry, allocation/error and per-user paths");
}
