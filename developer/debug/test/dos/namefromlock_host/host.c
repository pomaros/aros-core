/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
typedef intptr_t BPTR;
typedef char *STRPTR;
typedef int32_t LONG;
struct DosLibrary { int unused; };
#define AROS_LIBFUNC_INIT
#define BNULL 0
#define DOSFALSE 0
#define DOSTRUE (-1)
#define ERROR_LINE_TOO_LONG 120
#define CopyMem(s,d,n) memcpy(d,s,n)
static LONG error, duplication_error, traversal_error;
static unsigned duplicates, traversals, unlocks;
static BPTR DupLock(BPTR lock)
{
 assert(lock==17);duplicates++;
 if(duplication_error){error=duplication_error;return BNULL;}
 return 23;
}
static LONG IoErr(void){return error;}
static void SetIoErr(LONG value){error=value;}
static BOOL namefrom_internal(struct DosLibrary *base,BPTR lock,STRPTR buffer,LONG length)
{
 (void)base;assert(length>=16);traversals++;
 /* Model the dangerous old NULL fallback faithfully, rather than rejecting
  * it in the adapter: the caller must reject the failed duplicate itself. */
 if(!lock){strcpy(buffer,"SYS:");return DOSTRUE;}
 assert(lock==23);
 if(traversal_error){error=traversal_error;return DOSFALSE;}
 strcpy(buffer,"RAM:created");return DOSTRUE;
}
static void UnLock(BPTR lock){assert(lock==23 || lock==BNULL);unlocks++;error=0;}
#include "actual.c"
static void reset(void){error=duplication_error=traversal_error=0;duplicates=traversals=unlocks=0;}
int main(void)
{
 struct DosLibrary base={0};char buffer[32];
 for(unsigned i=0;i<2;i++){
  reset();duplication_error=i?103:202;strcpy(buffer,"unchanged");
  assert(!actual_name(17,buffer,sizeof(buffer),&base));
  assert(error==duplication_error && duplicates==1 && !traversals && !unlocks);
  assert(!strcmp(buffer,"unchanged"));
 }
 reset();assert(actual_name(17,buffer,sizeof(buffer),&base));
 assert(!strcmp(buffer,"RAM:created") && duplicates==1 && traversals==1 && unlocks==1);
 reset();traversal_error=120;
 assert(!actual_name(17,buffer,sizeof(buffer),&base));
 assert(error==120 && duplicates==1 && traversals==1 && unlocks==1);
 reset();assert(actual_name(BNULL,buffer,sizeof(buffer),&base));
 assert(!strcmp(buffer,"SYS:") && !duplicates && !traversals && !unlocks);
 reset();assert(actual_name(BNULL,buffer,5,&base));
 assert(!strcmp(buffer,"SYS:") && !duplicates && !traversals && !unlocks);
 reset();assert(!actual_name(BNULL,buffer,4,&base));
 assert(error==120 && !duplicates && !traversals && !unlocks);
 puts("PASS: seven actual NameFromLock cases (exclusive/OOM duplicate failure, shared success, traversal error, NULL and short NULL buffer)");
 return 0;
}
