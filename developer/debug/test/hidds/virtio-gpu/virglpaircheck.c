/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
/* Two independent RunCommand processes. Keep B's renderer live until A has
 * completely exited, then require B to draw/read again before its own exit. */
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <exec/libraries.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include "virglpair_protocol.h"

struct job {
    BPTR segment, output;
    char arguments[256], path[512];
    ULONG argument_length, child_id;
    LONG rc;
    BOOL started, created;
};
static LONG child_entry(void)
{
    struct job *job = FindTask(NULL)->tc_UserData;
    /* Publish our own ID; the returned Process pointer can already be stale
     * if a fast child exits before CreateNewProc returns to the parent. */
    job->child_id = GetETask(FindTask(NULL))->et_UniqueID;
    __atomic_store_n(&job->started, TRUE, __ATOMIC_RELEASE);
    /* No parent CRT calls: this process enters the loaded child's own CRT. */
    job->rc = RunCommand(job->segment, 4000000, job->arguments, job->argument_length);
    return job->rc;
}
static UWORD library_count(CONST_STRPTR name)
{
    struct Library *base;
    UWORD count = 0;
    Forbid();
    base = (struct Library *)FindName(&SysBase->LibList, name);
    if (base) count = base->lib_OpenCnt;
    Permit();
    return count;
}
static BOOL active(struct job *job)
{ return __atomic_load_n(&job->started, __ATOMIC_ACQUIRE) && ChildStatus(job->child_id) == CHILD_ACTIVE; }
static BOOL pending(struct job *job)
{ return job->created && (!__atomic_load_n(&job->started, __ATOMIC_ACQUIRE) || active(job)); }
static void collect_ready(struct MsgPort *port, struct avgl_pair_ready **ready, BOOL *okay)
{
    struct Message *message;
    while ((message = GetMsg(port)) != NULL) {
        struct avgl_pair_ready *r = (struct avgl_pair_ready *)message;
        if (message->mn_Length != sizeof(*r) || r->role > 1 || ready[r->role]) {
            *okay = FALSE; ReplyMsg(message);
        } else ready[r->role] = r;
    }
}
static int occurrences(const char *text, const char *marker)
{
    int count = 0;
    while ((text = strstr(text, marker)) != NULL) { count++; text++; }
    return count;
}
int main(int argc, char **argv)
{
    struct job jobs[2] = {{0}};
    struct avgl_pair_ready *ready[2] = {0};
    struct MsgPort *port = NULL;
    char port_name[64], data[16384];
    BOOL okay = TRUE;
    UWORD before = library_count("posixc.library");
    struct Library *held_gallium;
    BOOL cold = argc == 4 && !strcmp(argv[3], "cold");
    unsigned i, ticks;
    if (argc != 3 && !cold) { puts("Usage: virglpaircheck child log-prefix [cold]"); return 10; }
    /* Prevent legitimate library GC from removing the cached provider before
     * its reference count is inspected. Opening Gallium does not load it. */
    held_gallium = OpenLibrary("gallium.library", 26);
    if (!held_gallium) return 10;
    UWORD provider_before = library_count("virgl.hidd");
    printf("loaded gallium: %u.%u; provider pins before clients=%u\n",
        held_gallium->lib_Version, held_gallium->lib_Revision, provider_before);
    if (cold && (held_gallium->lib_Version != 26 || held_gallium->lib_Revision != 2 || provider_before)) {
        puts("FAIL: cold fallback setup/version"); CloseLibrary(held_gallium); return 10;
    }
    snprintf(port_name, sizeof(port_name), "virgl-pair-%p", FindTask(NULL));
    port = CreateMsgPort();
    if (!port) { CloseLibrary(held_gallium); return 10; }
    port->mp_Node.ln_Name = port_name;
    Forbid();
    if (FindPort(port_name)) { Permit(); DeleteMsgPort(port); CloseLibrary(held_gallium); return 10; }
    AddPort(port); Permit();
    for (i = 0; i < 2; i++) {
        struct Process *child;
        BPTR input;
        int length = snprintf(jobs[i].path, sizeof(jobs[i].path), "%s-%u.log", argv[2], i);
        if (length < 0 || (unsigned)length >= sizeof(jobs[i].path)) { okay = FALSE; break; }
        length = snprintf(jobs[i].arguments, sizeof(jobs[i].arguments),
            "virgl single handles logerror=%u pairport=%s pairrole=%u\n", 70+i, port_name, i);
        if (length < 0 || (unsigned)length >= sizeof(jobs[i].arguments)) { okay = FALSE; break; }
        jobs[i].argument_length = length;
        jobs[i].rc = 10;
        jobs[i].segment = LoadSeg(argv[1]);
        jobs[i].output = Open(jobs[i].path, MODE_NEWFILE);
        input = Open("NIL:", MODE_OLDFILE);
        if (!jobs[i].segment || !jobs[i].output || !input) {
            if (input) Close(input);
            okay = FALSE; break;
        }
        child = CreateNewProcTags(NP_Entry, (IPTR)child_entry,
            NP_Name, (IPTR)"VirGL pair client", NP_Cli, TRUE,
            NP_UserData, (IPTR)&jobs[i], NP_StackSize, 131072,
            NP_Input, input, NP_CloseInput, TRUE,
            NP_Output, jobs[i].output, NP_CloseOutput, FALSE,
            NP_Error, jobs[i].output, NP_CloseError, FALSE,
            NP_NotifyOnDeath, TRUE, TAG_DONE);
        if (!child) { Close(input); okay = FALSE; break; }
        jobs[i].created = TRUE;
    }
    for (ticks = 0; okay && (!ready[0] || !ready[1]); ticks++) {
        collect_ready(port, ready, &okay);
        if ((!pending(&jobs[0]) || !pending(&jobs[1])) || ticks >= 6000) okay = FALSE;
        if (!ready[0] || !ready[1]) Delay(1);
    }
    if (okay) {
        puts("both renderer contexts are live: PASS"); fflush(stdout);
        ReplyMsg(&ready[0]->message); ready[0] = NULL;
        ChildWait(jobs[0].child_id);
        printf("first client exited: rc=%ld; releasing surviving client\n", (long)jobs[0].rc);
        fflush(stdout);
        ReplyMsg(&ready[1]->message); ready[1] = NULL;
    } else {
        puts("FAIL: both clients did not reach the live-context barrier"); fflush(stdout);
    }
    /* On partial startup/failure, release any late readiness messages, and
     * keep this entry code, job records and seglists alive through child death. */
    while (pending(&jobs[0]) || pending(&jobs[1])) {
        if (!okay) {
            collect_ready(port, ready, &okay);
            for (i = 0; i < 2; i++) if (ready[i]) {
                ReplyMsg(&ready[i]->message); ready[i] = NULL;
            }
        }
        Delay(1);
    }
    for (i = 0; i < 2; i++) {
        if (jobs[i].started) { ChildWait(jobs[i].child_id); ChildFree(jobs[i].child_id); }
        if (jobs[i].segment) UnLoadSeg(jobs[i].segment);
        if (jobs[i].output) {
            LONG length;
            Flush(jobs[i].output); Seek(jobs[i].output, 0, OFFSET_BEGINNING);
            length = Read(jobs[i].output, data, sizeof(data)-1);
            if (length < 0) length = 0;
            data[length] = 0;
            char own[64], other[64], survivor[64];
            snprintf(own, sizeof(own), "STDIO_CLIENT[logging-client-%02u]", 70+i);
            snprintf(other, sizeof(other), "STDIO_CLIENT[logging-client-%02u]", 71-i);
            snprintf(survivor, sizeof(survivor), "pair survivor: role=%u PASS", i);
            if (jobs[i].rc || occurrences(data, own) != 1 || occurrences(data, other) ||
                !strstr(data, survivor) || !strstr(data, "\nPASS\n")) okay = FALSE;
            Close(jobs[i].output);
        } else okay = FALSE;
        printf("client%u rc=%ld\n", i, (long)jobs[i].rc);
    }
    RemPort(port); DeleteMsgPort(port);
    UWORD provider_after = library_count("virgl.hidd");
    printf("provider pins after both clients=%u expected=1\n", provider_after);
    if (provider_after != 1) okay = FALSE;
    CloseLibrary(held_gallium);
    UWORD after = library_count("posixc.library");
    if (after != before) okay = FALSE;
    printf("pair stdio/ownership: PosixC %u->%u %s\n", before, after, okay ? "PASS" : "FAIL");
    return okay ? 0 : 10;
}
