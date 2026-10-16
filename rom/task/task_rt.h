/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * SPDX-License-Identifier: APL-1.1
 */
#ifndef RESOURCES_TASK_RT_H
#define RESOURCES_TASK_RT_H
#include <exec/types.h>

/* Cooperative ownership, not protection against hostile callers. Registration
 * asserts exclusive ownership; never register framework-owned or escaped data.
 * Pin BEFORE publishing a pointer/message, unpin AFTER its last possible use.
 * See developer/debug/test/exec/resource_tracking/README.md for the contract. */
enum RTType {
    RT_MEM = 1, RT_VEC, RT_SEMAPHORE, RT_FILE, RT_LOCK, RT_PORT, RT_SIGNAL,
    RT_TYPE_COUNT
};
enum RTAction {
    RT_ADOPT = 1, RT_DISOWN, RT_ACCEPT, RT_FORGET, RT_PIN, RT_UNPIN,
    RT_BEGIN_MUTATION, RT_END_MUTATION, RT_CLEANUP, RT_STATS, RT_RECOVER,
    RT_PREPARE_EXIT
};
#define RTF_EXCLUSIVE       1UL
#define RTF_QUIESCENT        2UL
#define RTF_ROLLBACK_DONE    4UL

struct RTStats {
    ULONG live[RT_TYPE_COUNT];
    ULONG reclaimed[RT_TYPE_COUNT];
    ULONG quarantined, exported, pinned, dirty;
    ULONG contexts, closed_contexts, records, leases;
    ULONG close_errors; /* handle consumed, handler reported I/O failure */
};
struct RTRequest {
    ULONG size;                 /* sizeof(struct RTRequest) */
    ULONG action;
    IPTR handle;                /* opaque, never reused; zero is invalid */
    ULONG type;
    ULONG flags;
    IPTR object;                /* address, BPTR, or allocated signal number */
    IPTR auxiliary;             /* RT_MEM size, otherwise zero */
    struct RTStats *stats;      /* RT_STATS output, otherwise NULL */
};
/* TaskResourceControl returns a new handle for ADOPT/PIN, 1 for success, 0 for
 * refusal. CLEANUP returns 1 only if all processed records were reclaimed
 * without a close error. PREPARE_EXIT closes registration and processes only
 * FILE/LOCK while DOS context is live; success leaves other types for RemTask.
 * RECOVER is an explicit supervisor assertion of quiescence + completed
 * rollback; only transferable orphan resources can be accepted this way. */
#endif
