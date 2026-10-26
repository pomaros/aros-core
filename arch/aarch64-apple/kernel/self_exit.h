/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef APPLE_SELF_EXIT_H
#define APPLE_SELF_EXIT_H
/* Each CPU owns a slot. Local IRQ/FIQ exclusion spans queue through dispatch.
 * The task is opaque: neither the queue nor drain dereferences its storage. */
typedef void (*AppleExitPublish)(void *, void *);
struct AppleSelfExit {
    unsigned state, cpu;
    void *task, *base;
    AppleExitPublish publish;
};
int AppleSelfExitQueue(struct AppleSelfExit *, unsigned, void *, void *, AppleExitPublish);
enum AppleSelfExitResult { APPLE_EXIT_REFUSED=0, APPLE_EXIT_EMPTY=1, APPLE_EXIT_PUBLISHED=2 };
/* Only at EL1 after the destination frame/TLS no longer refers to departing.
 * REFUSED is a contract violation; do not resume the old task.
 * PUBLISHED distinguishes a newly woken service task from an empty slot. */
int AppleSelfExitDetached(struct AppleSelfExit *, unsigned, void *departing);
#endif
