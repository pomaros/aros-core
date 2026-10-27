/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Distributed under the AROS Public License 1.1.

    Host model of a per-task duplicated library's OpenLib/CloseLib, as
    genmodule generates them, with the actual rellibrary and libfunc set
    code. Exec, task storage and library loading are host mocks.
*/
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char UBYTE; typedef unsigned short UWORD; typedef uint32_t ULONG;
typedef int32_t LONG; typedef short BOOL; typedef void *APTR; typedef intptr_t IPTR;
typedef intptr_t BPTR; typedef const char *CONST_STRPTR;
#define TRUE 1
#define BNULL ((BPTR)0)
#define FALSE 0
#define NT_TASK 1
#define NT_PROCESS 13
#define LIBF_DELEXP 8
struct Node { struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; char *ln_Name; };
struct Library { struct Node lib_Node; UBYTE lib_Flags; UWORD lib_NegSize, lib_PosSize; UWORD lib_OpenCnt; };
struct Task { struct Node tc_Node; };
struct Process { struct Task pr_Task; APTR pr_ReturnAddr; BPTR pr_CIS, pr_COS, pr_CES; };
struct ExecBase { int unused; } sysbase, *SysBase = &sysbase;
#define LIBBASETYPE struct Library
#define LIBBASETYPEPTR struct Library *
#define LIBBASE libbase
#define GM_UNIQUENAME(x) test_##x
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
#define D(x)
#define ForeachElementInSet(set, direction, pos, elem)                       \
for (pos = (direction >= 0) ? 1 : (long)(set)[0];                            \
     elem = (void *)(set)[pos], (direction >= 0) ? elem != NULL : (pos) != 0; \
     pos += (direction >= 0) ? 1 : -1)
struct rellibraryset { CONST_STRPTR name; const LONG * const versionptr; const IPTR * const baseoffsetptr; };
#define SETNAME(set) test_set_##set
#define set_open_rellibraries(base) _set_open_rellibraries_list(base, SETNAME(RELLIBS), SysBase)
#define set_close_rellibraries(base) _set_close_rellibraries_list(base, SETNAME(RELLIBS), SysBase)

/* ---- trace ---- */
static char trace[4096];
static void note(const char *s) { strcat(trace, s); strcat(trace, ";"); }

/* ---- task storage, offset table ---- */
static struct Process process;
static struct Library *slot, *parent_slot, *offset_table;
static struct Task *FindTask(void *n) { assert(!n); return &process.pr_Task; }
static IPTR GetTaskStorageSlot(LONG id) { (void)id; return (IPTR)slot; }
static IPTR GetParentTaskStorageSlot(LONG id) { (void)id; return (IPTR)parent_slot; }
static BOOL SetTaskStorageSlot(LONG id, IPTR v) { (void)id; slot = (struct Library *)v; return TRUE; }
static char *__aros_getoffsettable(void) { return (char *)offset_table; }
static void __aros_setoffsettable(void *b) { offset_table = b; }
#define __aros_getbase_UserelBase() __aros_getoffsettable()

/* ---- memory and libraries ---- */
static int fail_make, live_bases;
struct InitTable { APTR FuncTable, DataTable; ULONG Size; };
static struct Library *MakeLibrary(APTR f, APTR d, APTR i, ULONG size, BPTR s)
{
    (void)f; (void)d; (void)i; (void)s;
    if (fail_make) return NULL;
    UBYTE *m = calloc(1, 64 + size); assert(m);
    struct Library *l = (struct Library *)(m + 64);
    l->lib_NegSize = 64; l->lib_PosSize = size; ++live_bases;
    return l;
}
static void CopyMem(const void *s, void *d, ULONG n) { memcpy(d, s, n); }
static void FreeMem(APTR p, ULONG n) { (void)n; --live_bases; memset(p, 0x5a, n); free(p); }

static struct Library rel_libs[2];          /* what this base opens */
static struct Library root_rel[2];          /* the root's own, borrowed by a copy */
static int rel_open[2], fail_rel = -1;
static const char *rel_names[2] = {"dummy.library", "pertask.library"};
static struct Library *OpenLibrary(CONST_STRPTR name, ULONG v)
{
    (void)v;
    int i = !strcmp(name, rel_names[1]);
    if (i == fail_rel) { note(i ? "open-rel1-fail" : "open-rel0-fail"); return NULL; }
    ++rel_open[i]; note(i ? "open-rel1" : "open-rel0");
    return &rel_libs[i];
}
static void CloseLibrary(struct Library *l)
{
    assert(l == &rel_libs[0] || l == &rel_libs[1]); /* never a root's borrowed base */
    int i = l == &rel_libs[1];
    assert(rel_open[i] > 0); --rel_open[i];
    note(i ? "close-rel1" : "close-rel0");
}
static void __showerror(const char *f, ...) { (void)f; }

/* ---- the module's sets ---- */
#include "dupbase.c"            /* generated struct __GM_DupBase and helpers */
static const IPTR rel_offset[2] = { sizeof(struct __GM_DupBase), sizeof(struct __GM_DupBase) + sizeof(void *) };
static const LONG rel_version[2] = { 0, 0 };
static const struct rellibraryset rel0 = { "dummy.library", &rel_version[0], &rel_offset[0] };
static const struct rellibraryset rel1 = { "pertask.library", &rel_version[1], &rel_offset[1] };
static const void * const test_set_RELLIBS[] = { (void *)2, &rel0, &rel1, NULL };

/* The module state OPENLIB sets up on the new base: an allocation that the
   second OPENLIB function may fail after. */
static int fail_openlib2, allocations;
static APTR *state_of(struct Library *b) { return (APTR *)&((struct __GM_DupBase *)b)->retaddr + 0; }
static void **module_state(struct Library *b) { return (void **)((char *)b + rel_offset[1] + sizeof(void *)); }
static int openlib1(struct Library *b) { *module_state(b) = malloc(16); ++allocations; note("openlib1"); return 1; }
static int openlib2(struct Library *b) { (void)b; note(fail_openlib2 ? "openlib2-fail" : "openlib2"); return !fail_openlib2; }
static int closelib1(struct Library *b) { free(*module_state(b)); *module_state(b) = NULL; --allocations; note("closelib1"); return 1; }
static int abortopenlib1(struct Library *b)
{
    /* The failed base is still current, its relative libraries still as the
       open left them, and it must guard partial initialization. */
    assert(offset_table == b && slot == b);
    note(rel_open[0] || rel_open[1] ? "abort-with-rels" : "abort-no-rels");
    if (*module_state(b)) { free(*module_state(b)); *module_state(b) = NULL; --allocations; }
    return 1;
}
static const void * const test_set_OPENLIB[] = { (void *)2, openlib1, openlib2, NULL };
static const void * const test_set_CLOSELIB[] = { (void *)1, closelib1, NULL };
static const void * const test_set_ABORTOPENLIB[] = { (void *)1, abortopenlib1, NULL };
static const void * const *test_set_CANCLOSELIB = NULL;   /* no close guard */

#include "actual_sets.c"        /* actual set_call_libfuncs and rellibrary code */

static const struct InitTable test_InitTable = { NULL, NULL, LIBBASESIZE + 2 * sizeof(void *) };
#define __freebase(LIBBASE) do { UBYTE *negptr = (UBYTE *)(LIBBASE) - (LIBBASE)->lib_NegSize; FreeMem(negptr, (LIBBASE)->lib_NegSize + (LIBBASE)->lib_PosSize); } while (0)
#include "openclose.c"          /* generated OpenLib and CloseLib bodies */

static struct Library *root;
static void reset(void)
{
    assert(live_bases == 0 && !rel_open[0] && !rel_open[1] && !allocations);
    trace[0] = 0; fail_make = 0; fail_rel = -1; fail_openlib2 = 0;
    slot = parent_slot = NULL; offset_table = root;
    process.pr_Task.tc_Node.ln_Type = NT_PROCESS;
}

int main(void)
{
    (void)state_of; (void)__pertaskslot;
    root = calloc(1, LIBBASESIZE + 2 * sizeof(void *) + 64);
    root->lib_Node.ln_Name = "userel.library";
    root->lib_PosSize = LIBBASESIZE + 2 * sizeof(void *);
    root->lib_NegSize = 64; /* a dup has the root's function table size */
    *rootbase_field = root;
    /* The root's own relative bases: a copy borrows these pointers. */
    *(struct Library **)((char *)root + rel_offset[0]) = &root_rel[0];
    *(struct Library **)((char *)root + rel_offset[1]) = &root_rel[1];

    /* 1. success, then the normal close is unchanged */
    reset(); struct Library *b = OpenLib(0, root);
    assert(b && slot == b && offset_table == b && root->lib_OpenCnt == 1 && live_bases == 1);
    assert(!strcmp(trace, "open-rel0;open-rel1;openlib1;openlib2;"));
    trace[0] = 0; CloseLib(b);
    assert(!strcmp(trace, "closelib1;close-rel1;close-rel0;") || !strcmp(trace, "closelib1;close-rel0;close-rel1;"));
    assert(slot == NULL && offset_table == root && root->lib_OpenCnt == 0);

    /* 2. the first relative library fails: nothing opened, root's borrowed
          pointers never closed, task slot and offset table restored */
    reset(); fail_rel = 0; assert(!OpenLib(0, root));
    assert(!strcmp(trace, "open-rel0-fail;abort-no-rels;"));
    assert(slot == NULL && offset_table == root && root->lib_OpenCnt == 0);

    /* 3. the second fails: the first is closed exactly once */
    reset(); fail_rel = 1; assert(!OpenLib(0, root));
    assert(!strcmp(trace, "open-rel0;open-rel1-fail;close-rel0;abort-no-rels;"));
    assert(slot == NULL && offset_table == root);

    /* 4. OPENLIB fails after allocating: abort runs first, on the current
          base, then the relative libraries close, then the base is freed */
    reset(); fail_openlib2 = 1; assert(!OpenLib(0, root));
    assert(!strncmp(trace, "open-rel0;open-rel1;openlib1;openlib2-fail;abort-with-rels;", 59));
    assert(strstr(trace, "close-rel0") && strstr(trace, "close-rel1") && !strstr(trace, "closelib1"));
    assert(slot == NULL && offset_table == root && root->lib_OpenCnt == 0);

    /* 5. a parent's base in the slot is restored, not cleared */
    static struct __GM_DupBase parent_dup; /* another task's dup: not reusable here */
    reset(); slot = &parent_dup.base; fail_openlib2 = 1;
    assert(!OpenLib(0, root) && slot == &parent_dup.base && offset_table == root);
    slot = NULL;

    /* 6. MakeLibrary fails: no hook, nothing changed */
    reset(); fail_make = 1; assert(!OpenLib(0, root) && !trace[0] && slot == NULL && root->lib_OpenCnt == 0);

    reset();
    printf("PASS: success and normal close unchanged; rel0/rel1/OPENLIB failures roll back once, abort on the current base, slot and offset table restored, root pointers untouched\n");
    return 0;
}
