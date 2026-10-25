/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Native thread-local storage of ELF segments (AArch64 TLS descriptors)
*/

#ifndef DOS_ELF_TLS_H
#define DOS_ELF_TLS_H

#include <exec/types.h>
#include <dos/bptr.h>

struct DosLibrary;
struct MemList;

/*
 * The TLS image of one loaded segment. It lives in a hunk of that segment,
 * so UnLoadSeg() frees it together with the code that uses it.
 *
 * The key is (serial << 16) | slot. The slot indexes every task's control
 * block; the serial is never reused, so a block left in a task by an
 * unloaded segment never matches a segment loaded later in the same slot.
 */
struct ELFTLSModule
{
    UQUAD  tm_Key;
    BPTR   tm_Hunk;         /* the hunk holding this record */
    ULONG  tm_Align;        /* block alignment, a power of two */
    IPTR   tm_BlockSize;    /* .tdata and .tbss */
    IPTR   tm_ImageSize;    /* initialised prefix, copied from tm_Image */
    UBYTE *tm_Image;
};

/*
 * What a TLSDESC code sequence addresses. The resolver is called with the
 * descriptor's address in x0 and returns the variable's address minus
 * TPIDR_EL0; it preserves every other register. elf_tls.c's assembly reads
 * td_Offset at 8 and td_Key at 16.
 */
struct ELFTLSDescriptor
{
    APTR                 td_Resolver;
    IPTR                 td_Offset;   /* of the variable in the segment's block */
    UQUAD                td_Key;      /* copy of tm_Key */
    struct ELFTLSModule *td_Module;
};

/*
 * TPIDR_EL0 holds the address of the running task's TLS anchor, a word of
 * its context that exec sets up for every task and never moves: TLS
 * descriptor code may keep the thread pointer in a register for a whole
 * function, so the pointer never changes. The anchor holds the task's
 * control block, NULL until its first TLS access; the slot table behind it
 * grows without the anchor or the control block moving. Only the task
 * itself changes them (the resolver's slow path, UnLoadSeg() in its own
 * process). The control block, the table and the blocks are on the task's
 * tc_MemEntry list, so they go with the task. elf_tls.c's assembly reads
 * tc_Slots at 0, tc_Count at 8, and 16-byte slots.
 */
struct ELFTLSSlot
{
    UBYTE *ts_Block;
    UQUAD  ts_Key;
};

struct ELFTLSControl
{
    struct ELFTLSSlot *tc_Slots;
    IPTR               tc_Count;
    struct MemList    *tc_MemList;       /* this block's entry in tc_MemEntry */
    struct MemList    *tc_SlotsMemList;  /* the table's */
};

#define ELFTLS_SLOT(key)    ((ULONG)((key) & 0xffff))
#define ELFTLS_MAXSLOTS     0x10000

BOOL elftls_register(struct ELFTLSModule *mod, struct DosLibrary *DOSBase);
void elftls_unregister_seglist(BPTR seglist, struct DosLibrary *DOSBase);
void elftls_resolve(void);

#endif /* DOS_ELF_TLS_H */
