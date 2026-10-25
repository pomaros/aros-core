/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Native thread-local storage of ELF segments (AArch64 TLS descriptors)
*/

#include <exec/alerts.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include <string.h>

#include "dos_intern.h"
#include "elf_tls.h"

#if defined(__aarch64__)

/*
 * Every TLS variable is reached through a descriptor (TLSDESC): any task
 * may run any segment's code, a hook in a foreign task included, so there is
 * no static TLS and no constant offset from the thread pointer. A task's
 * block for a segment is allocated on its first access, from that task.
 *
 * TLS is available in task context only: not in interrupt code, not in
 * tc_Switch/tc_Launch, and not for a first access under Disable(), since
 * the slow path allocates memory.
 */

/* The task's TLS anchor: exec sets TPIDR_EL0 to its address for the task's
   whole life, and never changes it; compiled code may keep it in a register
   across accesses, so only the word it points to changes, never the pointer */
static inline struct ELFTLSControl **anchor(void)
{
    struct ELFTLSControl **tp;

    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp));
    return tp;
}

/*
 * A task's control block and blocks hang on its tc_MemEntry list, so exec
 * frees them with the task, whoever removes it. Only the task itself adds
 * or removes them.
 */
static APTR task_alloc(IPTR size, struct MemList **ml)
{
    struct MemList *list = AllocMem(sizeof(struct MemList), MEMF_PUBLIC | MEMF_CLEAR);
    APTR            mem;

    if (!list)
        return NULL;
    mem = AllocMem(size, MEMF_ANY | MEMF_CLEAR);
    if (!mem)
    {
        FreeMem(list, sizeof(struct MemList));
        return NULL;
    }
    list->ml_NumEntries = 1;
    list->ml_ME[0].me_Addr = mem;
    list->ml_ME[0].me_Length = size;
    Forbid();
    AddTail(&FindTask(NULL)->tc_MemEntry, &list->ml_Node);
    Permit();
    *ml = list;
    return mem;
}

static void task_free(struct MemList *ml)
{
    Forbid();
    Remove(&ml->ml_Node);
    Permit();
    FreeEntry(ml);
}

static void free_block(UBYTE *block)
{
    task_free(((struct MemList **)block)[-1]);
}

static UBYTE *alloc_block(struct ELFTLSModule *mod)
{
    IPTR            align = mod->tm_Align > sizeof(APTR) ? mod->tm_Align : sizeof(APTR);
    struct MemList *ml;
    UBYTE          *raw = task_alloc(mod->tm_BlockSize + align + sizeof(APTR), &ml);
    UBYTE          *block;

    if (!raw)
        return NULL;
    block = (UBYTE *)AROS_ROUNDUP2((IPTR)raw + sizeof(APTR), align);
    ((struct MemList **)block)[-1] = ml;
    if (mod->tm_ImageSize)
        CopyMem(mod->tm_Image, block, mod->tm_ImageSize);
    return block;
}

/* Called by elftls_resolve with every caller-saved register preserved */
IPTR elftls_resolve_slow(struct ELFTLSDescriptor *desc)
{
    struct ELFTLSControl **tp   = anchor();
    struct ELFTLSControl  *ctl  = *tp;
    ULONG                  slot = ELFTLS_SLOT(desc->td_Key);
    struct ELFTLSSlot     *s;

    if (!ctl)
    {
        struct MemList *ml;

        ctl = task_alloc(sizeof(*ctl), &ml);
        if (!ctl)
            Alert(AT_DeadEnd | AG_NoMemory | AO_DOSLib);
        ctl->tc_MemList = ml;
        *tp = ctl;
    }
    if (slot >= ctl->tc_Count)
    {
        IPTR count = AROS_ROUNDUP2(slot + 1, 8);
        struct MemList *ml;
        struct ELFTLSSlot *slots = task_alloc(count * sizeof(struct ELFTLSSlot), &ml);

        if (!slots)
            Alert(AT_DeadEnd | AG_NoMemory | AO_DOSLib);
        if (ctl->tc_Slots)
        {
            CopyMem(ctl->tc_Slots, slots, ctl->tc_Count * sizeof(struct ELFTLSSlot));
            task_free(ctl->tc_SlotsMemList);
        }
        ctl->tc_Slots = slots;
        ctl->tc_SlotsMemList = ml;
        ctl->tc_Count = count;
    }

    s = &ctl->tc_Slots[slot];
    if (s->ts_Key != desc->td_Key)
    {
        /* Left by a segment unloaded while this task was running elsewhere */
        if (s->ts_Block)
            free_block(s->ts_Block);
        s->ts_Key = 0;
        s->ts_Block = alloc_block(desc->td_Module);
        if (!s->ts_Block)
            Alert(AT_DeadEnd | AG_NoMemory | AO_DOSLib);
        s->ts_Key = desc->td_Key;
    }

    return (IPTR)s->ts_Block + desc->td_Offset - (IPTR)tp;
}

/*
 * The fast path finds the block with five loads and no call. NZCV is
 * clobbered, as the TLSDESC call sequence allows; every other register but
 * x0 is preserved, SIMD included around the C slow path.
 */
__asm__(
"   .text\n"
"   .balign 16\n"
"   .globl  elftls_resolve\n"
"   .type   elftls_resolve, %function\n"
"elftls_resolve:\n"
"   stp     x1, x2, [sp, #-32]!\n"
"   stp     x3, x4, [sp, #16]\n"
"   mrs     x1, tpidr_el0\n"           /* the task's anchor */
"   ldr     x3, [x1]\n"                 /* its control block */
"   cbz     x3, 1f\n"
"   ldr     x2, [x0, #16]\n"            /* td_Key, slot in its low 16 bits */
"   ldr     x4, [x3, #8]\n"             /* tc_Count */
"   cmp     x4, w2, uxth\n"
"   b.ls    1f\n"
"   ldr     x3, [x3]\n"                 /* tc_Slots */
"   add     x3, x3, w2, uxth #4\n"
"   ldp     x3, x4, [x3]\n"             /* ts_Block, ts_Key */
"   cmp     x4, x2\n"
"   b.ne    1f\n"
"   ldr     x2, [x0, #8]\n"             /* td_Offset */
"   add     x0, x3, x2\n"
"   sub     x0, x0, x1\n"
"   ldp     x3, x4, [sp, #16]\n"
"   ldp     x1, x2, [sp], #32\n"
"   ret\n"
"1: sub     sp, sp, #512\n"
"   stp     x5, x6, [sp, #0]\n"
"   stp     x7, x8, [sp, #16]\n"
"   stp     x9, x10, [sp, #32]\n"
"   stp     x11, x12, [sp, #48]\n"
"   stp     x13, x14, [sp, #64]\n"
"   stp     x15, x16, [sp, #80]\n"
"   stp     x17, x18, [sp, #96]\n"
"   stp     x29, x30, [sp, #112]\n"
"   stp     q0, q1, [sp, #128]\n"
"   stp     q2, q3, [sp, #160]\n"
"   stp     q4, q5, [sp, #192]\n"
"   stp     q6, q7, [sp, #224]\n"
"   stp     q16, q17, [sp, #256]\n"
"   stp     q18, q19, [sp, #288]\n"
"   stp     q20, q21, [sp, #320]\n"
"   stp     q22, q23, [sp, #352]\n"
"   stp     q24, q25, [sp, #384]\n"
"   stp     q26, q27, [sp, #416]\n"
"   stp     q28, q29, [sp, #448]\n"
"   stp     q30, q31, [sp, #480]\n"
"   add     x29, sp, #112\n"
"   bl      elftls_resolve_slow\n"
"   ldp     x5, x6, [sp, #0]\n"
"   ldp     x7, x8, [sp, #16]\n"
"   ldp     x9, x10, [sp, #32]\n"
"   ldp     x11, x12, [sp, #48]\n"
"   ldp     x13, x14, [sp, #64]\n"
"   ldp     x15, x16, [sp, #80]\n"
"   ldp     x17, x18, [sp, #96]\n"
"   ldp     x29, x30, [sp, #112]\n"
"   ldp     q0, q1, [sp, #128]\n"
"   ldp     q2, q3, [sp, #160]\n"
"   ldp     q4, q5, [sp, #192]\n"
"   ldp     q6, q7, [sp, #224]\n"
"   ldp     q16, q17, [sp, #256]\n"
"   ldp     q18, q19, [sp, #288]\n"
"   ldp     q20, q21, [sp, #320]\n"
"   ldp     q22, q23, [sp, #352]\n"
"   ldp     q24, q25, [sp, #384]\n"
"   ldp     q26, q27, [sp, #416]\n"
"   ldp     q28, q29, [sp, #448]\n"
"   ldp     q30, q31, [sp, #480]\n"
"   add     sp, sp, #512\n"
"   ldp     x3, x4, [sp, #16]\n"
"   ldp     x1, x2, [sp], #32\n"
"   ret\n"
"   .size   elftls_resolve, . - elftls_resolve\n"
);

BOOL elftls_register(struct ELFTLSModule *mod, struct DosLibrary *DOSBase)
{
    struct IntDosBase *idb = IDosBase(DOSBase);
    ULONG slot;
    BOOL  ok = TRUE;

    ObtainSemaphore(&idb->tlsSem);
    for (slot = 0; slot < idb->tlsCapacity && idb->tlsModules[slot]; slot++)
        ;
    if (slot == idb->tlsCapacity)
    {
        ULONG capacity = idb->tlsCapacity ? idb->tlsCapacity * 2 : 16;
        struct ELFTLSModule **grown = NULL;

        if (capacity <= ELFTLS_MAXSLOTS)
            grown = AllocVec(capacity * sizeof(*grown), MEMF_PUBLIC | MEMF_CLEAR);
        if (grown)
        {
            if (idb->tlsModules)
            {
                CopyMem(idb->tlsModules, grown, idb->tlsCapacity * sizeof(*grown));
                FreeVec(idb->tlsModules);
            }
            idb->tlsModules = grown;
            idb->tlsCapacity = capacity;
        }
        else
            ok = FALSE;
    }
    if (ok)
    {
        idb->tlsModules[slot] = mod;
        mod->tm_Key = (++idb->tlsSerial << 16) | slot;
    }
    ReleaseSemaphore(&idb->tlsSem);

    return ok;
}

void elftls_unregister_seglist(BPTR seglist, struct DosLibrary *DOSBase)
{
    struct IntDosBase    *idb = IDosBase(DOSBase);
    struct ELFTLSControl *ctl = *anchor();
    BPTR  seg;
    ULONG slot;

    if (!idb->tlsCapacity)
        return;

    ObtainSemaphore(&idb->tlsSem);
    for (seg = seglist; seg; seg = *(BPTR *)BADDR(seg))
    {
        for (slot = 0; slot < idb->tlsCapacity; slot++)
        {
            struct ELFTLSModule *mod = idb->tlsModules[slot];

            if (!mod || mod->tm_Hunk != seg)
                continue;

            /* A Shell runs and unloads commands in its own process: free its
               block now instead of when the Shell ends. Other tasks' blocks
               no longer match any key and go when they do. */
            if (ctl && slot < ctl->tc_Count && ctl->tc_Slots[slot].ts_Key == mod->tm_Key)
            {
                free_block(ctl->tc_Slots[slot].ts_Block);
                ctl->tc_Slots[slot].ts_Block = NULL;
                ctl->tc_Slots[slot].ts_Key = 0;
            }
            idb->tlsModules[slot] = NULL;
        }
    }
    ReleaseSemaphore(&idb->tlsSem);
}

#endif /* __aarch64__ */
