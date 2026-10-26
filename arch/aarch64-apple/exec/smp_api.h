/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Private kernel/Exec bridge for separately linked boot modules. */
#ifndef APPLE_EXEC_SMP_API_H
#define APPLE_EXEC_SMP_API_H
#include <exec/types.h>
#include <aros/types/spinlock_s.h>
#include <utility/hooks.h>
#include <stdint.h>
#include <aros/apple/runtime_local.h>
struct CallIPIEntry;
struct AROSAppleExecSMPAPI {
    uint32_t version, bytes;
    uint64_t (*irq_save)(void);
    void (*irq_restore)(uint64_t);
    void (*spin_init)(spinlock_t *);
    spinlock_t *(*spin_lock)(spinlock_t *, struct Hook *, ULONG);
    void (*spin_unlock)(spinlock_t *);
    struct CallIPIEntry *(*claim)(int);
    void (*commit)(struct CallIPIEntry *, int, struct Hook *, int, IPTR *);
    void (*abort)(struct CallIPIEntry *, int);
    void (*cancel)(APTR, IPTR);
    struct CallIPIEntry *(*try_claim)(int);
    spinlock_t *(*spin_trylock)(spinlock_t *, ULONG);
    int (*self_exit_queue)(void *, void *, void (*)(void *, void *));
};
extern const struct AROSAppleExecSMPAPI AROSAppleExecSMPAPI;
static inline int AROSAppleExecSMPAPIValid(const struct AROSAppleExecSMPAPI *table)
{
    return table && table->version==4 && table->bytes==sizeof(*table) &&
        table->spin_trylock && table->self_exit_queue;
}
static inline const struct AROSAppleExecSMPAPI *AROSAppleExecSMPAPIGet(void)
{
    struct AROSAppleRuntimeLocal *local;
    __asm__ volatile("mrs %0,tpidrro_el0":"=r"(local));
    const struct AROSAppleExecSMPAPI *table=local ? local->exec_smp_api : NULL;
    if(!AROSAppleExecSMPAPIValid(table)) __builtin_trap();
    return table;
}
#endif
