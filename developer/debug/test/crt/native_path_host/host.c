/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
/* Native DOS calls are boundaries; the complete production callback and its
 * actual public operation numbers are inserted by run.py. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef void *APTR;
typedef void *BPTR;
typedef intptr_t SIPTR;
typedef uintptr_t IPTR;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int BOOL;
typedef const char *CONST_STRPTR;
typedef char *STRPTR;
#define TRUE 1
#define FALSE 0
#define DVPF_UNLOCK 2
#define ERROR_INVALID_LOCK 211
#define ERROR_ACTION_NOT_KNOWN 209
#define GVF_LOCAL_ONLY 1
#define SYS_Input 1
#define SYS_Output 2
#define SYS_Error 3
#define TAG_DONE 0
struct DosLibrary { int unused; };
struct MsgPort { int unused; };
struct DateStamp { int unused; };
struct DevProc { ULONG dvp_Flags; BPTR dvp_Lock; };
/* PUBLIC_CONTEXT */
/* CALLBACK_REQUEST */
/* CALLBACK_OPERATIONS */
static LONG error, native_error;
static BOOL native_result;
static unsigned calls;
static const char from[] = "RAM:source", to[] = "RAM:destination";
static LONG SetIoErr(LONG value) { LONG previous = error; error = value; return previous; }
static LONG IoErr(void) { return error; }
static BOOL ReplaceObject(CONST_STRPTR source, CONST_STRPTR target)
{
    assert(source == from && target == to && "native replacement gets both original paths");
    assert(error == 0 && "callback clears stale error before entering DOS");
    calls++;
    SetIoErr(native_error);
    return native_result;
}
static SIPTR unexpected(void) { assert(0 && "wrong native operation"); return 0; }
static struct DevProc *GetDeviceProc(CONST_STRPTR n, struct DevProc *p) { return (APTR)unexpected(); }
static BPTR DupLock(BPTR p) { return (APTR)unexpected(); }
static void FreeDeviceProc(struct DevProc *p) { unexpected(); }
static BPTR Lock(CONST_STRPTR p, LONG mode) { return (APTR)unexpected(); }
static BPTR Open(CONST_STRPTR p, LONG mode) { return (APTR)unexpected(); }
static BPTR CreateDir(CONST_STRPTR p) { return (APTR)unexpected(); }
static BOOL DeleteFile(CONST_STRPTR p) { return unexpected(); }
static BOOL Rename(CONST_STRPTR p, CONST_STRPTR q) { return unexpected(); }
static BOOL MakeLink(CONST_STRPTR p, SIPTR q, LONG mode) { return unexpected(); }
static LONG ReadLink(struct MsgPort *p, BPTR l, CONST_STRPTR n, STRPTR b, LONG size) { return unexpected(); }
static BOOL SetProtection(CONST_STRPTR p, LONG mask) { return unexpected(); }
static BOOL SetFileDate(CONST_STRPTR p, struct DateStamp *date) { return unexpected(); }
static BOOL SetComment(CONST_STRPTR p, CONST_STRPTR comment) { return unexpected(); }
static BOOL SetOwner(CONST_STRPTR p, LONG owner) { return unexpected(); }
static BOOL NameFromLock(BPTR l, STRPTR b, LONG size) { return unexpected(); }
static BPTR LoadSeg(CONST_STRPTR p) { return (APTR)unexpected(); }
static BOOL SetVar(CONST_STRPTR n, CONST_STRPTR v, LONG length, ULONG flags) { return unexpected(); }
static LONG SystemTags(CONST_STRPTR n, ...) { return unexpected(); }
/* CALLBACK_SOURCE */
int main(void)
{
    struct DosLibrary dos;
    SIPTR paths[] = {(SIPTR)from, (SIPTR)to};
    struct __cwd_native_call request = {DOS_PATH_REPLACE_OBJECT, paths};
    for (unsigned i = 0; i < 2; i++) {
        error = 999;
        native_result = i ? 0 : 71;
        native_error = i ? 203 : 0;
        SIPTR result = __cwd_native_call(&request, &dos);
        assert(calls == i + 1 && "replacement reaches native DOS exactly once");
        assert(result == native_result && "native result preserved");
        assert(IoErr() == native_error && "native replacement error preserved");
    }
    request.operation = -1;
    assert(!__cwd_native_call(&request, &dos));
    assert(IoErr() == ERROR_ACTION_NOT_KNOWN && calls == 2);
    puts("PASS: native replacement paths, result, error and unknown-operation refusal");
    return 0;
}
