/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1

    Desc: The kernel's record of the last task crashes (C:CrashLog).

    KrnGetSystemAttr(AROS_APPLE_KATTR_CRASH_LOG) answers the address of the
    struct AROSAppleCrashLog, or -1. The kernel writes it from the exception
    handler of the CPU that took the fault; readers at EL0 only read it.

    count is the number of crashes ever recorded; entry n (1-based) lives in
    entries[(n - 1) % capacity] while entries[...].sequence == n. A reader
    copies an entry, then checks that sequence is unchanged and non-zero
    (another CPU may be rewriting the slot after `capacity` newer crashes).
    suspended counts the crashes whose task the kernel parked; the others
    (verdict != AROS_APPLE_CRASH_SUSPEND) halted the CPU that took them, so
    such an entry is only read after a recovery that kept memory, or through
    the m1n1 proxy.

    A place names the boot-bundle module and ELF section an address lies in,
    the offset from the start of that section (llvm-nm/objdump of the
    module's ELF file use the same offsets), and the nearest preceding
    function symbol. For a task's own loaded program, module is the program
    name, section "segN" (the Nth segment of its seglist) and offset counts
    from the segment's first data byte. An empty module means the address
    lies in no known code.
*/
#ifndef AROS_APPLE_CRASHLOG_H
#define AROS_APPLE_CRASHLOG_H

#include <stdint.h>

#define AROS_APPLE_KATTR_CRASH_LOG UINT32_C(0x83f10017)
#define AROS_APPLE_CRASH_LOG_VERSION UINT32_C(1)
#define AROS_APPLE_CRASH_LOG_CAPACITY 4U
#define AROS_APPLE_CRASH_FRAMES 8U
#define AROS_APPLE_CRASH_STACK_WORDS 8U
#define AROS_APPLE_CRASH_REG_PLACES 8U
#define AROS_APPLE_CRASH_NAME_BYTES 32U
#define AROS_APPLE_CRASH_SYMBOL_BYTES 48U
#define AROS_APPLE_CRASH_FAULT_BYTES 96U

/* Verdicts. Only SUSPEND keeps the CPU running. */
enum AROSAppleCrashVerdict {
    AROS_APPLE_CRASH_SUSPEND = 0,
    AROS_APPLE_CRASH_HALT_KILL_SWITCH,  /* chosen aros,task-crash-halt = 1 */
    AROS_APPLE_CRASH_HALT_NOT_TASK,     /* not a synchronous fault from EL0 */
    AROS_APPLE_CRASH_HALT_PROOF,        /* a runtime proof owns the fault */
    AROS_APPLE_CRASH_HALT_INTERRUPT,    /* inside an interrupt handler */
    AROS_APPLE_CRASH_HALT_EARLY,        /* scheduler not running yet */
    AROS_APPLE_CRASH_HALT_NO_TASK,      /* no Exec task, or not an ETask */
    AROS_APPLE_CRASH_HALT_TASK_STATE,   /* task not in TS_RUN */
    AROS_APPLE_CRASH_HALT_IRQ_EXCLUSION, /* inside an IRQ-masked lock section */
    AROS_APPLE_CRASH_HALT_LOCK_HELD,    /* holds its own or a scheduler lock */
    AROS_APPLE_CRASH_HALT_NO_CONTEXT,   /* no saved-context storage */
    AROS_APPLE_CRASH_HALT_LOCK_TIMEOUT, /* scheduler locks not obtainable */
    AROS_APPLE_CRASH_VERDICTS
};

/* entry flags */
#define AROS_APPLE_CRASH_IN_FORBID UINT32_C(1)   /* td_nest >= 0 */
#define AROS_APPLE_CRASH_IN_DISABLE UINT32_C(2)  /* id_nest >= 0 */
#define AROS_APPLE_CRASH_STACK_OUTSIDE UINT32_C(4) /* SP outside its bounds */

struct AROSAppleCrashPlace {
    uint64_t address;
    uint64_t offset;            /* from the section/segment start */
    uint64_t symbol_offset;     /* from the symbol, when symbol[0] != 0 */
    char module[AROS_APPLE_CRASH_NAME_BYTES];
    char section[16];
    char symbol[AROS_APPLE_CRASH_SYMBOL_BYTES];
};

struct AROSAppleCrashRegPlace {
    uint32_t reg;               /* 0..30 for x0..x30 */
    uint32_t reserved;
    struct AROSAppleCrashPlace place;
};

struct AROSAppleCrashEntry {
    volatile uint64_t sequence;
    uint64_t counter;           /* CNTVCT at the fault */
    uint64_t task;
    char task_name[AROS_APPLE_CRASH_NAME_BYTES];
    uint32_t task_type;         /* ln_Type: NT_TASK 1, NT_PROCESS 13 */
    uint32_t cpu;
    uint32_t verdict;
    uint32_t flags;
    int32_t td_nest;
    int32_t id_nest;
    uint64_t esr, far, elr, spsr, sp;
    uint64_t x[31];
    uint64_t stack_lower, stack_upper;
    char fault[AROS_APPLE_CRASH_FAULT_BYTES];
    struct AROSAppleCrashPlace elr_place;
    struct AROSAppleCrashPlace lr_place;
    uint32_t frame_count;
    uint32_t stack_count;
    uint32_t reg_place_count;
    uint32_t reserved;
    struct AROSAppleCrashPlace frames[AROS_APPLE_CRASH_FRAMES];
    uint64_t stack[AROS_APPLE_CRASH_STACK_WORDS];
    struct AROSAppleCrashRegPlace reg_places[AROS_APPLE_CRASH_REG_PLACES];
};

struct AROSAppleCrashLog {
    uint32_t version;
    uint32_t size;
    uint32_t capacity;
    uint32_t entry_size;
    volatile uint64_t count;
    volatile uint64_t suspended;
    struct AROSAppleCrashEntry entries[AROS_APPLE_CRASH_LOG_CAPACITY];
};

#endif
