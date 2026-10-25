/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef KEYRING_PROBEEXPORT_H
#define KEYRING_PROBEEXPORT_H
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <stdarg.h>
#include <stdio.h>

/* Safe proof metadata only. Never pass a plaintext credential to this helper.
 * RawPutChar reaches the serial observer even when stdout is a CON window. */
static inline int kr_probe_printf(const char *format, ...)
{
    char line[2048];
    va_list args;
    int length;
    size_t i;
    va_start(args, format);
    length=vsnprintf(line,sizeof(line),format,args);
    va_end(args);
    if(length<0 || (size_t)length>=sizeof(line)) return 0;
    for(i=0;i<(size_t)length;i++) RawPutChar((unsigned char)line[i]);
    return fwrite(line,1,(size_t)length,stdout)==(size_t)length && fflush(stdout)==0;
}

#define KR_PROOF_DIR "RAM:KeyringProof"
#define KR_PROOF_JSON KR_PROOF_DIR "/keyring.json"
#define KR_PROOF_REXX KR_PROOF_DIR "/keyring-rexx.log"

/* Create only explicit disposable evidence/fixture directories. Existing files
 * at these names are refused; a missing volume is never assigned implicitly. */
static inline int kr_probe_directory(const char *path)
{
    struct FileInfoBlock info;
    BPTR lock=Lock((CONST_STRPTR)path,ACCESS_READ);
    int ok;
    if(!lock) {
        if(IoErr()!=ERROR_OBJECT_NOT_FOUND) return 0;
        lock=CreateDir((CONST_STRPTR)path);
        if(!lock) return 0;
    }
    ok=Examine(lock,&info);
    if(ok && info.fib_DirEntryType<=0) { SetIoErr(ERROR_OBJECT_WRONG_TYPE);ok=0; }
    { LONG error=IoErr();UnLock(lock);if(!ok)SetIoErr(error); }
    return ok;
}

int kr_probe_export(const char *path, const char *relative);
int kr_probe_export_envarc(void);
void kr_probe_export_summary(void);
#endif
