/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Distributed under the AROS Public License 1.1.

    CANCLOSELIB: a per-task duplicated library's generated OpenLib and
    CloseLib, with the actual rellibrary and libfunc set code, and a close
    guard that refuses or allows. Exec, task storage and library loading
    are host mocks.
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
static int allow_close = 1, guard_calls; static struct Library *guard_saw;
static int canclose1(struct Library *b) { ++guard_calls; guard_saw = b; note(allow_close ? "guard-allow" : "guard-refuse"); return allow_close; }
static const void * const guard_set[] = { (void *)1, canclose1, NULL };
static const void * const *test_set_CANCLOSELIB = guard_set;   /* NULL: no guard */

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

    /* 1. a refused close changes nothing and keeps the reference */
    reset(); struct Library *b = OpenLib(0, root);
    assert(b && slot == b && offset_table == b && root->lib_OpenCnt == 1);
    offset_table = root;                       /* another base is current */
    allow_close = 0; trace[0] = 0; guard_calls = 0;
    assert(CloseLib(b) == 0 && guard_calls == 1 && guard_saw == b);
    assert(!strcmp(trace, "guard-refuse;"));
    assert(((struct __GM_DupBase *)b)->taskopencount == 1 && root->lib_OpenCnt == 1);
    assert(offset_table == root && slot == b && live_bases == 1);
    assert(rel_open[0] == 1 && rel_open[1] == 1 && allocations == 1);

    /* 2. the owner's close is allowed and closes normally */
    allow_close = 1; trace[0] = 0;
    CloseLib(b);
    assert(!strncmp(trace, "guard-allow;closelib1;", 22));
    assert(slot == NULL && root->lib_OpenCnt == 0 && live_bases == 0 && !rel_open[0] && !rel_open[1]);

    /* 3. two opens by one Task: a refusal keeps both, then each allowed
          close takes one */
    reset(); b = OpenLib(0, root); assert(OpenLib(0, root) == b);
    assert(((struct __GM_DupBase *)b)->taskopencount == 2);
    assert(__GM_GetBaseOpenCount(b) == 2);
    allow_close = 0; CloseLib(b);
    assert(((struct __GM_DupBase *)b)->taskopencount == 2 && live_bases == 1);
    allow_close = 1; CloseLib(b);
    assert(((struct __GM_DupBase *)b)->taskopencount == 1 && live_bases == 1 && slot == b);
    assert(__GM_GetBaseOpenCount(b) == 1);
    CloseLib(b);
    assert(live_bases == 0 && slot == NULL);

    /* 4. no guard registered: the close behaves as before */
    reset(); test_set_CANCLOSELIB = NULL; guard_calls = 0;
    b = OpenLib(0, root); CloseLib(b);
    assert(!guard_calls && live_bases == 0 && slot == NULL && root->lib_OpenCnt == 0);
    test_set_CANCLOSELIB = guard_set;

    reset();
    printf("PASS: a refused close changes nothing; an allowed close is normal; two opens; no guard as before\n");
    return 0;
}
