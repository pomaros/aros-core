/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */

#pragma once
#include <stdint.h>
#include <stddef.h>
typedef void *APTR;
typedef unsigned long ULONG;
typedef long LONG;
typedef uintptr_t IPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define DOSFALSE 0
#define RETURN_OK 0
#define RETURN_WARN 5
#define RETURN_ERROR 10
#define RETURN_FAIL 20
#define SIGBREAKF_CTRL_C 4096
struct RDArgs { int unused; };
struct Resident { unsigned char rt_Version; };
struct RDArgs *ReadArgs(const char *, IPTR *, void *);
void FreeArgs(struct RDArgs *);
LONG IoErr(void);
void PrintFault(LONG,const char *);
int Printf(const char *, ...);
int PutStr(const char *);
APTR OpenResource(const char *);
struct Resident *FindResident(const char *);
ULONG SetSignal(ULONG,ULONG);
LONG Flush(APTR);
APTR Output(void);
APTR OF_OpenKey(char *);
void OF_CloseKey(APTR);
APTR OF_GetProperty(APTR,APTR);
APTR OF_GetChild(APTR,APTR);
APTR OF_FindProperty(APTR,char *);
const char *OF_GetNodeName(APTR);
const char *OF_GetPropName(APTR);
ULONG OF_GetPropLen(APTR);
APTR OF_GetPropValue(APTR);
