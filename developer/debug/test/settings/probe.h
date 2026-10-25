/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    The settings guest probes' report: one line per check to the console
    and to the debug serial the VM harness reads, under the probe's tag,
    and "<tag> result=PASS" at the end when every check held.
*/
#ifndef SETTINGS_PROBE_H
#define SETTINGS_PROBE_H
#include <stdarg.h>
#include <stdio.h>
#include <exec/types.h>
#include <aros/debug.h>

static unsigned failures;

static void say(const char *format, ...)
{
    char line[220];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    printf("[" PROBE_TAG "] %s\n", line);
    kprintf("[" PROBE_TAG "] %s\n", line);
}

#define CHECK(condition, format, ...) do { \
    BOOL holds = (condition) ? TRUE : FALSE; \
    say("%s " format, holds ? "ok  " : "FAIL", ##__VA_ARGS__); \
    if (!holds) failures++; \
} while (0)

#define PROBE_RESULT() (say("result=%s failures=%u", failures ? "FAIL" : "PASS", failures), \
    failures ? RETURN_FAIL : RETURN_OK)

/* A gadget by its MUIA_ObjectID, through every group. */
#ifdef LIBRARIES_MUI_H
static Object *find_id(Object *object, ULONG id)
{
    struct List *children = NULL;
    Object *child, *found;
    APTR state;

    if (XGET(object, MUIA_ObjectID) == id)
        return object;
    if (!GetAttr(MUIA_Group_ChildList, object, (IPTR *)&children) || children == NULL)
        return NULL;
    state = children->lh_Head;
    while ((child = NextObject(&state)))
        if ((found = find_id(child, id)))
            return found;
    return NULL;
}
#endif
#endif
