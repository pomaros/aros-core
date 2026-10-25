/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: The pure part of the task crash handler: whether a fault may cost
 *       only its task, what the fault was, where an address lies, and the
 *       frame-pointer walk. Integer-only, no Exec, no libc, no allocation:
 *       the exception handler and the host fixture
 *       (developer/debug/test/kernel/taskcrash_host.py) include it alike.
 */

#ifndef AROS_APPLE_TASK_CRASH_POLICY_H
#define AROS_APPLE_TASK_CRASH_POLICY_H

#include <stdint.h>
#include <aros/apple/crashlog.h>
#include <aros/apple/modulemap.h>

#define CRASH_ESR_EC(esr) ((uint32_t)(((esr) >> 26) & 0x3fU))
#define CRASH_EC_UNKNOWN 0x00U
#define CRASH_EC_FP_ACCESS 0x07U
#define CRASH_EC_BTI 0x0dU
#define CRASH_EC_ILLEGAL_STATE 0x0eU
#define CRASH_EC_SVC32 0x11U
#define CRASH_EC_SVC64 0x15U
#define CRASH_EC_SYSREG 0x18U
#define CRASH_EC_PAC 0x1cU
#define CRASH_EC_IABORT_LOWER 0x20U
#define CRASH_EC_PC_ALIGN 0x22U
#define CRASH_EC_DABORT_LOWER 0x24U
#define CRASH_EC_SP_ALIGN 0x26U
#define CRASH_EC_FP_EXCEPTION 0x2cU
#define CRASH_EC_SERROR 0x2fU
#define CRASH_EC_BREAKPOINT_LOWER 0x30U
#define CRASH_EC_STEP_LOWER 0x32U
#define CRASH_EC_WATCHPOINT_LOWER 0x34U
#define CRASH_EC_BRK 0x3cU

/* Runtime vector index of a synchronous exception from AArch64 EL0. */
#define CRASH_VECTOR_SYNC_EL0 8U
/* exec/tasks.h TS_RUN, repeated so the host fixture needs no Exec. */
#define CRASH_TS_RUN 2U
/* An address below this is a NULL pointer plus a structure offset. */
#define CRASH_NULL_PAGE UINT64_C(0x10000)

/*
 * Everything the decision needs, gathered by the handler from the frame,
 * the CPU-local runtime state and the faulting task. A field the handler
 * could not read safely is set to the unsafe value.
 */
struct AROSAppleCrashState {
    uint32_t vector;
    uint64_t esr;
    uint32_t kill_switch;           /* chosen aros,task-crash-halt == 1 */
    uint32_t proof_active;          /* the runtime proof's fail-fast owns it */
    uint32_t interrupt_depth;       /* IRQ/FIQ handlers in progress */
    uint32_t irq_mask_depth;        /* IRQSave sections (spinlock holders) */
    uint32_t scheduler_live;        /* SysBase set and tasks dispatched */
    uint32_t have_task;
    uint32_t task_is_etask;
    uint32_t task_state;
    uint32_t task_lock_held;        /* its tc_SpinLock is write-held by it */
    uint32_t list_lock_held;        /* a scheduler list lock is held by it */
    uint32_t context_saveable;      /* et_RegFrame and the SIMD area exist */
};

/*
 * The one decision. A synchronous fault of an EL0 task costs that task
 * only when nothing the rest of the system needs is caught in it: no
 * interrupt handler is running, the task is not inside an IRQ-masked
 * section (the only way a task holds a scheduler or list spinlock), it
 * holds neither its own task lock nor a scheduler list lock, the
 * scheduler is running, and its context can be saved. Forbid() and
 * Disable() of the task are not reasons to halt: both nest per task, the
 * CPU leaves them with the task. Everything else keeps the visible halt.
 */
static inline uint32_t AROSAppleCrashDecide(const struct AROSAppleCrashState *s)
{
    uint32_t ec = CRASH_ESR_EC(s->esr);

    if (s->kill_switch)
        return AROS_APPLE_CRASH_HALT_KILL_SWITCH;
    if (s->vector != CRASH_VECTOR_SYNC_EL0 || ec == CRASH_EC_SVC64 ||
            ec == CRASH_EC_SVC32 || ec == CRASH_EC_SERROR)
        return AROS_APPLE_CRASH_HALT_NOT_TASK;
    if (s->proof_active)
        return AROS_APPLE_CRASH_HALT_PROOF;
    if (s->interrupt_depth != 0)
        return AROS_APPLE_CRASH_HALT_INTERRUPT;
    if (!s->scheduler_live)
        return AROS_APPLE_CRASH_HALT_EARLY;
    if (!s->have_task || !s->task_is_etask)
        return AROS_APPLE_CRASH_HALT_NO_TASK;
    if (s->task_state != CRASH_TS_RUN)
        return AROS_APPLE_CRASH_HALT_TASK_STATE;
    if (s->irq_mask_depth != 0)
        return AROS_APPLE_CRASH_HALT_IRQ_EXCLUSION;
    if (s->task_lock_held || s->list_lock_held)
        return AROS_APPLE_CRASH_HALT_LOCK_HELD;
    if (!s->context_saveable)
        return AROS_APPLE_CRASH_HALT_NO_CONTEXT;
    return AROS_APPLE_CRASH_SUSPEND;
}

/*
 * The Amiga CPU trap number of the fault (exec/alerts.h: ACPU_BusErr 2,
 * ACPU_AddressErr 3, ACPU_InstErr 4, ACPU_PrivErr 8), 0 for none. It goes
 * into the one-line [TRAP] record the VM qualification tools match.
 */
static inline uint32_t AROSAppleCrashTrapNumber(uint64_t esr)
{
    switch (CRASH_ESR_EC(esr)) {
    case CRASH_EC_DABORT_LOWER:
        return 2;
    case CRASH_EC_IABORT_LOWER:
    case CRASH_EC_PC_ALIGN:
    case CRASH_EC_SP_ALIGN:
        return 3;
    case CRASH_EC_UNKNOWN:
    case CRASH_EC_FP_ACCESS:
    case CRASH_EC_ILLEGAL_STATE:
    case CRASH_EC_FP_EXCEPTION:
    case CRASH_EC_BRK:
    case CRASH_EC_BTI:
    case CRASH_EC_PAC:
        return 4;
    case CRASH_EC_SYSREG:
        return 8;
    default:
        return 0;
    }
}

static inline const char *AROSAppleCrashVerdictName(uint32_t verdict)
{
    static const char *const names[AROS_APPLE_CRASH_VERDICTS] = {
        "suspended", "kill switch", "not a task fault", "runtime proof",
        "in interrupt", "before the scheduler", "no exec task",
        "task not running", "inside IRQ exclusion", "holds a scheduler lock",
        "no context storage", "scheduler locks busy"
    };
    return verdict < AROS_APPLE_CRASH_VERDICTS ? names[verdict] : "?";
}

/* Bounded text building: never writes past cap - 1, always terminated. */
struct AROSAppleCrashText {
    char *buffer;
    uint32_t capacity;
    uint32_t length;
};

static inline void crash_text_init(struct AROSAppleCrashText *t,
    char *buffer, uint32_t capacity)
{
    t->buffer = buffer;
    t->capacity = capacity;
    t->length = 0;
    if (capacity != 0)
        buffer[0] = 0;
}

static inline void crash_text_char(struct AROSAppleCrashText *t, char c)
{
    if (t->capacity == 0 || t->length + 1U >= t->capacity)
        return;
    t->buffer[t->length++] = c;
    t->buffer[t->length] = 0;
}

static inline void crash_text_str(struct AROSAppleCrashText *t, const char *s)
{
    uint32_t n;

    for (n = 0; s != 0 && s[n] != 0 && n < 256U; ++n)
        crash_text_char(t, s[n]);
}

/* 0x-prefixed, without leading zeros. */
static inline void crash_text_hex(struct AROSAppleCrashText *t, uint64_t v)
{
    static const char digits[] = "0123456789abcdef";
    int shift = 60;

    crash_text_str(t, "0x");
    while (shift > 0 && ((v >> shift) & 0xfU) == 0)
        shift -= 4;
    for (; shift >= 0; shift -= 4)
        crash_text_char(t, digits[(v >> shift) & 0xfU]);
}

static inline void crash_text_dec(struct AROSAppleCrashText *t, uint64_t v)
{
    char digits[20];
    int n = 0;

    do {
        digits[n++] = (char)('0' + (int)(v % 10U));
        v /= 10U;
    } while (v != 0 && n < 20);
    while (n > 0)
        crash_text_char(t, digits[--n]);
}

static inline void crash_copy_name(char *out, uint32_t capacity,
    const char *in)
{
    uint32_t n = 0;

    if (capacity == 0)
        return;
    if (in != 0)
        for (; n + 1U < capacity && in[n] != 0; ++n)
            out[n] = in[n];
    out[n] = 0;
}

static inline const char *crash_fault_status(uint32_t fsc, uint32_t *level)
{
    *level = fsc & 3U;
    switch (fsc & 0x3cU) {
    case 0x04: return "unmapped";            /* translation fault */
    case 0x08: return "access-flag fault on";
    case 0x0c: return "permission fault on";
    case 0x00: return "address-size fault on";
    default: break;
    }
    *level = 4;
    switch (fsc) {
    case 0x10: return "external abort on";
    case 0x11: return "tag check fault on";
    case 0x21: return "misaligned";
    case 0x30: return "TLB conflict on";
    default: return "fault on";
    }
}

/*
 * One line that says what the fault was, for example
 *   "instruction fetch at NULL+0x10 (branch to 0x10), unmapped, level 1"
 *   "data read from unmapped 0x804d2ee9c, level 3"
 *   "data write: permission fault on 0x1000 (read-only or no-access), level 3"
 * followed by the ESR fields that decided it.
 */
static inline void AROSAppleCrashDescribe(struct AROSAppleCrashText *t,
    uint64_t esr, uint64_t far, uint64_t elr)
{
    uint32_t ec = CRASH_ESR_EC(esr);
    uint32_t iss = (uint32_t)(esr & 0x1ffffffU);
    uint32_t fsc = iss & 0x3fU;
    uint32_t level;
    const char *status;

    switch (ec) {
    case CRASH_EC_IABORT_LOWER:
        status = crash_fault_status(fsc, &level);
        if (far < CRASH_NULL_PAGE) {
            crash_text_str(t, "instruction fetch at NULL+");
            crash_text_hex(t, far);
            crash_text_str(t, " (branch to ");
            crash_text_hex(t, far);
            crash_text_str(t, ")");
        } else if ((fsc & 0x3cU) == 0x0cU) {
            crash_text_str(t, "instruction fetch from non-executable ");
            crash_text_hex(t, far);
        } else {
            crash_text_str(t, "instruction fetch, ");
            crash_text_str(t, status);
            crash_text_char(t, ' ');
            crash_text_hex(t, far);
        }
        break;
    case CRASH_EC_DABORT_LOWER:
        status = crash_fault_status(fsc, &level);
        crash_text_str(t, (iss & 0x40U) ? "data write" : "data read");
        if (far < CRASH_NULL_PAGE) {
            crash_text_str(t, " through NULL+");
            crash_text_hex(t, far);
            crash_text_str(t, ", ");
            crash_text_str(t, status);
        } else if ((fsc & 0x3cU) == 0x04U) {
            crash_text_str(t, (iss & 0x40U) ? " to unmapped " :
                " from unmapped ");
            crash_text_hex(t, far);
        } else {
            crash_text_str(t, ": ");
            crash_text_str(t, status);
            crash_text_char(t, ' ');
            crash_text_hex(t, far);
            if ((fsc & 0x3cU) == 0x0cU)
                crash_text_str(t, (iss & 0x40U) ? " (read-only or no-access)" :
                    " (no-access)");
        }
        break;
    case CRASH_EC_PC_ALIGN:
        crash_text_str(t, "misaligned PC ");
        crash_text_hex(t, far);
        level = 4;
        break;
    case CRASH_EC_SP_ALIGN:
        crash_text_str(t, "misaligned SP");
        level = 4;
        break;
    case CRASH_EC_UNKNOWN:
        crash_text_str(t, "undefined instruction at ");
        crash_text_hex(t, elr);
        level = 4;
        break;
    case CRASH_EC_BRK:
        crash_text_str(t, "breakpoint brk #");
        crash_text_hex(t, iss & 0xffffU);
        level = 4;
        break;
    case CRASH_EC_FP_ACCESS:
        crash_text_str(t, "FP/SIMD access trapped");
        level = 4;
        break;
    case CRASH_EC_FP_EXCEPTION:
        crash_text_str(t, "floating-point exception");
        level = 4;
        break;
    case CRASH_EC_SYSREG:
        crash_text_str(t, "system register access trapped");
        level = 4;
        break;
    case CRASH_EC_ILLEGAL_STATE:
        crash_text_str(t, "illegal execution state");
        level = 4;
        break;
    case CRASH_EC_BTI:
        crash_text_str(t, "branch target exception");
        level = 4;
        break;
    case CRASH_EC_PAC:
        crash_text_str(t, "pointer authentication failure");
        level = 4;
        break;
    case CRASH_EC_SERROR:
        crash_text_str(t, "SError (asynchronous abort)");
        level = 4;
        break;
    default:
        crash_text_str(t, "exception class ");
        crash_text_hex(t, ec);
        level = 4;
        break;
    }
    if ((ec == CRASH_EC_IABORT_LOWER || ec == CRASH_EC_DABORT_LOWER) &&
            level < 4) {
        crash_text_str(t, ", level ");
        crash_text_dec(t, level);
    }
    crash_text_str(t, " [ec=");
    crash_text_hex(t, ec);
    if (ec == CRASH_EC_IABORT_LOWER || ec == CRASH_EC_DABORT_LOWER) {
        crash_text_str(t, " fsc=");
        crash_text_hex(t, fsc);
        if (ec == CRASH_EC_DABORT_LOWER) {
            crash_text_str(t, (iss & 0x40U) ? " wnr=1" : " wnr=0");
            if (iss & (1U << 24)) {
                crash_text_str(t, " size=");
                crash_text_dec(t, 1U << ((iss >> 22) & 3U));
                crash_text_str(t, " reg=x");
                crash_text_dec(t, (iss >> 16) & 0x1fU);
            }
        }
    }
    crash_text_str(t, " iss=");
    crash_text_hex(t, iss);
    crash_text_str(t, "]");
}

static inline void crash_place_clear(struct AROSAppleCrashPlace *p,
    uint64_t address)
{
    uint32_t n;

    p->address = address;
    p->offset = 0;
    p->symbol_offset = 0;
    for (n = 0; n < sizeof(p->module); ++n)
        p->module[n] = 0;
    for (n = 0; n < sizeof(p->section); ++n)
        p->section[n] = 0;
    for (n = 0; n < sizeof(p->symbol); ++n)
        p->symbol[n] = 0;
}

/* The map is trusted (stage two built it in protected memory) but checked
 * for its own consistency before any index is followed. */
static inline int crash_map_valid(const struct AROSAppleModuleMap *map)
{
    return map != 0 && map->version == AROS_APPLE_MODULE_MAP_VERSION &&
        map->size >= sizeof(*map) && map->code != 0 &&
        map->code_count <= AROS_APPLE_MODULE_MAP_MAX_CODE &&
        map->symbol_count <= AROS_APPLE_MODULE_MAP_MAX_SYMBOLS &&
        map->string_bytes <= AROS_APPLE_MODULE_MAP_MAX_STRINGS &&
        (map->symbol_count == 0 ||
            (map->symbols != 0 && map->strings != 0 && map->string_bytes != 0));
}

/*
 * Place an address in the boot-bundle code: module, section, offset and
 * the nearest function symbol at or below it (within its size when the
 * symbol has one). Returns 1 when the address lies in a module's code.
 */
static inline int AROSAppleCrashPlaceModule(
    const struct AROSAppleModuleMap *map, uint64_t address,
    struct AROSAppleCrashPlace *p)
{
    const struct AROSAppleModuleCode *code = 0;
    uint32_t index;

    crash_place_clear(p, address);
    if (!crash_map_valid(map))
        return 0;
    for (index = 0; index < map->code_count; ++index) {
        const struct AROSAppleModuleCode *c = &map->code[index];

        if (address >= c->start && address - c->start < c->size) {
            code = c;
            break;
        }
    }
    if (code == 0)
        return 0;
    p->offset = address - code->start;
    crash_copy_name(p->module, sizeof(p->module), code->name);
    crash_copy_name(p->section, sizeof(p->section), code->section_name);
    if (code->symbol_count != 0 && code->symbol_first < map->symbol_count &&
            code->symbol_count <= map->symbol_count - code->symbol_first) {
        const struct AROSAppleModuleSymbol *s = &map->symbols[code->symbol_first];
        uint32_t low = 0, high = code->symbol_count;

        /* Last symbol with offset <= p->offset. */
        while (low < high) {
            uint32_t mid = low + (high - low) / 2U;

            if (s[mid].offset <= p->offset)
                low = mid + 1U;
            else
                high = mid;
        }
        if (low != 0) {
            const struct AROSAppleModuleSymbol *hit = &s[low - 1U];
            uint64_t into = p->offset - hit->offset;

            if ((hit->size == 0 || into < hit->size) &&
                    hit->name < map->string_bytes) {
                uint32_t n = 0;
                const char *name = map->strings + hit->name;

                while (n + 1U < sizeof(p->symbol) &&
                        hit->name + n < map->string_bytes && name[n] != 0) {
                    p->symbol[n] = name[n];
                    ++n;
                }
                p->symbol[n] = 0;
                p->symbol_offset = into;
            }
        }
    }
    return 1;
}

/* "gfx.hidd .text+0x13500 BM__Hidd_BitMap__CopyMemBox32+0xa4" or "?". */
static inline void AROSAppleCrashFormatPlace(struct AROSAppleCrashText *t,
    const struct AROSAppleCrashPlace *p)
{
    if (p->module[0] == 0) {
        crash_text_char(t, '?');
        return;
    }
    crash_text_str(t, p->module);
    crash_text_char(t, ' ');
    crash_text_str(t, p->section);
    crash_text_char(t, '+');
    crash_text_hex(t, p->offset);
    if (p->symbol[0] != 0) {
        crash_text_char(t, ' ');
        crash_text_str(t, p->symbol);
        crash_text_char(t, '+');
        crash_text_hex(t, p->symbol_offset);
    }
}

/* A validated 64-bit read: returns 0 when the address may not be read. */
typedef int (*AROSAppleCrashRead)(void *context, uint64_t address,
    uint64_t *value);

/*
 * The frame-pointer chain of an AArch64 AAPCS64 task: x29 points at a
 * {saved x29, saved x30} pair on the task's stack. Every frame must lie
 * inside [lower, upper - 16], be 8-byte aligned, and sit strictly above
 * the previous one (so the walk cannot loop); each read goes through the
 * validating reader. Returns the number of return addresses stored, at
 * most max. Return addresses of zero end the walk.
 */
static inline uint32_t AROSAppleCrashWalk(uint64_t fp, uint64_t lower,
    uint64_t upper, AROSAppleCrashRead read, void *context,
    uint64_t *returns, uint32_t max)
{
    uint32_t count = 0;
    uint64_t previous = 0;

    if (upper <= lower || upper - lower < 16U)
        return 0;
    while (count < max) {
        uint64_t next, ret;

        if ((fp & 7U) != 0 || fp < lower || fp > upper - 16U ||
                (previous != 0 && fp <= previous))
            break;
        if (!read(context, fp, &next) || !read(context, fp + 8U, &ret))
            break;
        if (ret == 0)
            break;
        returns[count++] = ret;
        previous = fp;
        fp = next;
    }
    return count;
}

#endif /* AROS_APPLE_TASK_CRASH_POLICY_H */
