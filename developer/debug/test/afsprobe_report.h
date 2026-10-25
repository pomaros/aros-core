/* Copyright (C) 2026 John KNIPPER. AROS Public License 1.1. */
#ifndef AFSPROBE_REPORT_H
#define AFSPROBE_REPORT_H
#include <stddef.h>
#include <proto/exec.h>

/* Keep a bounded, already formatted record together against local timer
 * interrupts in the single-CPU guest. DEBUG_PUT_CHAR SVC preserves SPSR;
 * it performs no scheduler/timer call. No formatting or DOS I/O occurs
 * inside Disable/Enable. This is not a cross-CPU logging guarantee. */
static void afsprobe_debug_record(const char *record, size_t length)
{
    static const char invalid[] = "[AFSPROBE] result=FAIL reason=debug-record-bound\n";
    size_t i;
    if (length > 384) { record = invalid; length = sizeof(invalid)-1; }
    Disable();
    for (i=0;i<length;i++) RawPutChar((UBYTE)record[i]);
    Enable();
}

/* AFS-specific probe helpers. Other Settings probes keep their original
 * common probe.h. Only BootUndoProbe opts into these guarded records. */
#ifdef PROBE_TAG
#include <stdarg.h>
#include <stdio.h>
static unsigned failures;
static void say(const char *format, ...)
{
    char payload[384], record[512];
    int n;
    va_list args;
    va_start(args,format);
    n=vsnprintf(payload,sizeof(payload),format,args);
    va_end(args);
    if (n<0 || (size_t)n>=sizeof(payload)) {
        afsprobe_debug_record(NULL,513); failures++; return;
    }
    n=snprintf(record,sizeof(record),"[" PROBE_TAG "] %s\n",payload);
    if (n<0 || (size_t)n>=sizeof(record)) {
        afsprobe_debug_record(NULL,513); failures++; return;
    }
    afsprobe_debug_record(record,(size_t)n);
    printf("%s",record);
}
#define CHECK(condition, format, ...) do { \
    BOOL holds=(condition)?TRUE:FALSE; \
    say("%s " format,holds?"ok  ":"FAIL",##__VA_ARGS__); \
    if(!holds) failures++; \
} while(0)
#define PROBE_RESULT() (say("result=%s failures=%u",failures?"FAIL":"PASS",failures), \
    failures?RETURN_FAIL:RETURN_OK)
#ifdef LIBRARIES_MUI_H
static Object *find_id(Object *object, ULONG id)
{
    struct List *children=NULL;
    Object *child,*found;
    APTR state;
    if(XGET(object,MUIA_ObjectID)==id)return object;
    if(!GetAttr(MUIA_Group_ChildList,object,(IPTR *)&children)||!children)return NULL;
    state=children->lh_Head;
    while((child=NextObject(&state)))if((found=find_id(child,id)))return found;
    return NULL;
}
#endif
#endif
#endif
