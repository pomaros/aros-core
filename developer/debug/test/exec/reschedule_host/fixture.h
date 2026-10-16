/* SPDX-License-Identifier: APL-1.1
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Minimal UP ABI fixture; scheduling decisions come from production sources.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
typedef signed char BYTE;
typedef unsigned long ULONG;
#define TRUE 1
#define FALSE 0
#define TS_REMOVED 0
#define TS_RUN 1
#define TS_READY 2
#define TS_WAIT 3
#define TF_EXCEPT 1
#define TF_SWITCH 2
#define TF_LAUNCH 4
#define SFF_QuantumOver 1
#define SFF_AttnSwitch 2
#define INTF_SOFTINT 1
#define AN_StackProbe 1
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
#define AROS_LH0(ret, name, base, var, slot, lib) ret name(void)
#define AROS_ATOMIC_OR(x, y) ((x) |= (y))
#define AROS_ATOMIC_AND(x, y) ((x) &= (y))
#define AROS_UFC1NR(ret, fn, arg) ((fn)())
#define AROS_UFCA(t, value, reg) value
#define D(x)
#define bug(...) ((void)0)
struct Node { struct Node *ln_Succ, *ln_Pred; BYTE ln_Pri; const char *ln_Name; };
struct List { struct Node *head; };
struct Task {
    struct Node tc_Node;
    int tc_State, tc_Flags, tc_IDNestCnt;
    uintptr_t tc_SPReg, tc_SPLower, tc_SPUpper;
    ULONG tc_SigWait;
    void (*tc_Switch)(void), (*tc_Launch)(void);
};
struct ExecBase {
    struct Task *ThisTask;
    struct List TaskReady, TaskWait;
    int SysFlags, AttnResched, IDNestCnt, TDNestCnt, Quantum, Elapsed;
    ULONG IdleCount, DispCount;
};
static struct ExecBase base, *SysBase = &base;
static unsigned softints, schedules;
#define CUSTOM_CAUSE(x) (++softints)
#define IsListEmpty(l) (!(l)->head)
#define GetHead(l) ((l)->head)
static void Enqueue(struct List *list, struct Node *node)
{
    struct Node **at = &list->head, *prev = NULL;
    while (*at && (*at)->ln_Pri >= node->ln_Pri) {
        prev = *at;
        at = &(*at)->ln_Succ;
    }
    node->ln_Succ = *at;
    node->ln_Pred = prev;
    if (*at) (*at)->ln_Pred = node;
    *at = node;
}
static struct Node *remhead(struct List *list)
{
    struct Node *node = list->head;
    if (node) {
        list->head = node->ln_Succ;
        if (list->head) list->head->ln_Pred = NULL;
        node->ln_Succ = node->ln_Pred = NULL;
    }
    return node;
}
#define REMHEAD(l) remhead(l)
static void Remove(struct Node *node) { assert(!"unexpected Remove in UP fixture"); }
static void Alert(ULONG code) { assert(!"unexpected stack alert"); }
static void KrnSchedule(void);
