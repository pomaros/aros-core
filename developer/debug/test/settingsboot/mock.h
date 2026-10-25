/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
   Distributed under the AROS Public License, version 1.1. */
#ifndef SETTINGSBOOT_MOCK_H
#define SETTINGSBOOT_MOCK_H
#include <stdint.h>
#include <stddef.h>
#include <dirent.h>
#include <stdio.h>
typedef uintptr_t BPTR;
typedef intptr_t IPTR;
typedef intptr_t SIPTR;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef unsigned char UBYTE;
typedef const char *CONST_STRPTR;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define BNULL 0
#define BADDR(p) ((void *)(p))
#define SHARED_LOCK 0
#define MODE_OLDFILE 0
#define MODE_NEWFILE 1
#define MEMF_ANY 0
#define DOS_FIB 0
#define ST_USERDIR 2
#define ST_FILE -3
#define GVF_GLOBAL_ONLY 0
#define RETURN_OK 0
#define RETURN_ERROR 10
#define RETURN_FAIL 20
#define ERROR_NO_FREE_STORE 103
#define ERROR_LINE_TOO_LONG 120
#define ERROR_OBJECT_EXISTS 203
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_DIR_NOT_FOUND 204
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_TOO_MANY_LEVELS 217
#define ERROR_NO_MORE_ENTRIES 232
#define ERROR_INVALID_RESIDENT_LIBRARY 122
struct Library { unsigned lib_Version, lib_Revision; };
extern struct Library *DOSBase;
#define __aros_getbase_DOSBase() (DOSBase)
#define ERROR_UNKNOWN 0x7fffffff
struct MsgPort { int disk; };
struct FileLock { struct MsgPort *fl_Task; char *path; DIR *iterator; int collision_cursor; };
struct FileInfoBlock { LONG fib_DirEntryType; char fib_FileName[256]; };
struct RDArgs { int dummy; };
LONG IoErr(void);
LONG SetIoErr(LONG);
BPTR Lock(CONST_STRPTR, LONG);
void UnLock(BPTR);
BPTR CreateDir(CONST_STRPTR);
BOOL DeleteFile(CONST_STRPTR);
BOOL Rename(CONST_STRPTR, CONST_STRPTR);
BOOL ReplaceObject(CONST_STRPTR, CONST_STRPTR);
BPTR Open(CONST_STRPTR, LONG);
BOOL Close(BPTR);
BOOL Flush(BPTR);
LONG Read(BPTR, void *, LONG);
LONG Write(BPTR, const void *, LONG);
void *AllocVec(size_t, ULONG);
void FreeVec(void *);
void *AllocDosObject(ULONG, void *);
void FreeDosObject(ULONG, void *);
BOOL Examine(BPTR, struct FileInfoBlock *);
BOOL ExNext(BPTR, struct FileInfoBlock *);
LONG DoPkt(struct MsgPort *, LONG, SIPTR, SIPTR, SIPTR, SIPTR, SIPTR);
struct RDArgs *ReadArgs(CONST_STRPTR, IPTR *, void *);
void FreeArgs(struct RDArgs *);
LONG SetVar(CONST_STRPTR, CONST_STRPTR, LONG, ULONG);
LONG Printf(CONST_STRPTR, ...);
#endif
