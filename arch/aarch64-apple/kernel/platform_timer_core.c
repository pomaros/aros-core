/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Injected ARM virtual-timer ownership and one-shot state machine.
 */

#include <aros/apple/platformtimer.h>

static void zero_state(struct AROSAppleTimerState *state)
{
    uint8_t *bytes = (uint8_t *)state;
    uint32_t index;

    for (index = 0; index < sizeof(*state); ++index)
        bytes[index] = 0;
}

static int state_valid(const struct AROSAppleTimerState *state)
{
    return state != NULL &&
        state->version == AROS_APPLE_TIMER_STATE_VERSION &&
        state->size == sizeof(*state) && state->initialized != 0 &&
        state->frequency != 0 && state->io.read_counter != NULL &&
        state->io.read_control != NULL &&
        state->io.write_control != NULL &&
        state->io.write_compare != NULL &&
        state->io.synchronize != NULL;
}

static int owner_valid(
    const struct AROSAppleTimerState *state, uint64_t owner)
{
    return state_valid(state) && state->claimed != 0 && owner != 0 &&
        state->owner == owner && state->callback != NULL &&
        state->callback_context != NULL;
}

static void write_control(
    struct AROSAppleTimerState *state, uint32_t control)
{
    state->io.write_control(state->io_context, control);
    state->io.synchronize(state->io_context);
}

static enum AROSAppleTimerStatus arm_internal(
    struct AROSAppleTimerState *state, uint64_t deadline)
{
    uint64_t now = state->io.read_counter(state->io_context);

    if (deadline == 0)
        return AROS_APPLE_TIMER_ARGUMENT;
    if (deadline <= now) {
        if (now == UINT64_MAX)
            return AROS_APPLE_TIMER_RANGE;
        deadline = now + 1U;
    }
    state->io.write_compare(state->io_context, deadline);
    write_control(state, AROS_APPLE_TIMER_CTL_ENABLE);
    state->programmed_deadline = deadline;
    state->armed = 1;
    return AROS_APPLE_TIMER_OK;
}

enum AROSAppleTimerStatus AROSAppleTimerInitialize(
    struct AROSAppleTimerState *state, uint64_t frequency,
    const struct AROSAppleTimerIO *io, void *io_context)
{
    if (state == NULL)
        return AROS_APPLE_TIMER_ARGUMENT;
    zero_state(state);
    if (frequency == 0 || frequency > UINT32_MAX || io == NULL ||
        io->read_counter == NULL || io->read_control == NULL ||
        io->write_control == NULL || io->write_compare == NULL ||
        io->synchronize == NULL)
        return AROS_APPLE_TIMER_ARGUMENT;
    state->version = AROS_APPLE_TIMER_STATE_VERSION;
    state->size = sizeof(*state);
    state->frequency = frequency;
    state->io = *io;
    state->io_context = io_context;
    state->initialized = 1;
    write_control(state, AROS_APPLE_TIMER_CTL_IMASK);
    return AROS_APPLE_TIMER_OK;
}

enum AROSAppleTimerStatus AROSAppleTimerClaim(
    struct AROSAppleTimerState *state, uint64_t owner,
    AROSAppleTimerCallback callback, void *callback_context)
{
    if (!state_valid(state) || owner == 0 || callback == NULL ||
        callback_context == NULL)
        return AROS_APPLE_TIMER_ARGUMENT;
    if (state->claimed != 0)
        return AROS_APPLE_TIMER_OWNERSHIP;
    state->owner = owner;
    state->callback = callback;
    state->callback_context = callback_context;
    state->claimed = 1;
    ++state->generation;
    return AROS_APPLE_TIMER_OK;
}

enum AROSAppleTimerStatus AROSAppleTimerArm(
    struct AROSAppleTimerState *state, uint64_t owner, uint64_t deadline)
{
    if (!state_valid(state))
        return AROS_APPLE_TIMER_STATE;
    if (!owner_valid(state, owner))
        return AROS_APPLE_TIMER_OWNERSHIP;
    return arm_internal(state, deadline);
}

enum AROSAppleTimerStatus AROSAppleTimerDisarm(
    struct AROSAppleTimerState *state, uint64_t owner)
{
    if (!state_valid(state))
        return AROS_APPLE_TIMER_STATE;
    if (!owner_valid(state, owner))
        return AROS_APPLE_TIMER_OWNERSHIP;
    write_control(state, AROS_APPLE_TIMER_CTL_IMASK);
    state->programmed_deadline = 0;
    state->armed = 0;
    return AROS_APPLE_TIMER_OK;
}

enum AROSAppleTimerStatus AROSAppleTimerRelease(
    struct AROSAppleTimerState *state, uint64_t owner)
{
    enum AROSAppleTimerStatus status;

    if (!state_valid(state))
        return AROS_APPLE_TIMER_STATE;
    if (!owner_valid(state, owner))
        return AROS_APPLE_TIMER_OWNERSHIP;
    status = AROSAppleTimerDisarm(state, owner);
    if (status != AROS_APPLE_TIMER_OK)
        return status;
    state->owner = 0;
    state->callback = NULL;
    state->callback_context = NULL;
    state->claimed = 0;
    ++state->generation;
    return AROS_APPLE_TIMER_OK;
}

enum AROSAppleTimerStatus AROSAppleTimerRefresh(
    struct AROSAppleTimerState *state, uint64_t owner)
{
    uint64_t generation, deadline, now;
    if (!state_valid(state))
        return AROS_APPLE_TIMER_STATE;
    if (!owner_valid(state, owner))
        return AROS_APPLE_TIMER_OWNERSHIP;
    generation = state->generation;
    now = state->io.read_counter(state->io_context);
    deadline = state->callback(state->callback_context, now);
    /* A callback can release/reclaim ownership. Its old deadline must not
     * overwrite the timer established by the new claim, even for the same
     * owner address. No register writes after ownership changes. */
    if (!owner_valid(state, owner) || state->generation != generation)
        return AROS_APPLE_TIMER_OWNERSHIP;
    if (deadline == 0)
        return AROSAppleTimerDisarm(state, owner);
    return arm_internal(state, deadline);
}

enum AROSAppleTimerStatus AROSAppleTimerHandle(
    struct AROSAppleTimerState *state)
{
    uint32_t control;
    if (!state_valid(state) || state->claimed == 0 ||
        state->callback == NULL || state->callback_context == NULL)
        return AROS_APPLE_TIMER_STATE;
    /*
     * Pending means enabled and asserted. The mask bit is deliberately not
     * part of the test: the m1n1 hypervisor masks the guest's virtual timer
     * before it delivers the FIQ, so a masked, asserted source is exactly
     * what this handler sees on the target, and refusing it turned every
     * timer expiry into an unexpected exception that ended the session.
     */
    control = state->io.read_control(state->io_context);
    if ((control & (AROS_APPLE_TIMER_CTL_ENABLE |
            AROS_APPLE_TIMER_CTL_ISTATUS)) !=
            (AROS_APPLE_TIMER_CTL_ENABLE |
             AROS_APPLE_TIMER_CTL_ISTATUS)) {
        ++state->unexpected_count;
        return AROS_APPLE_TIMER_NOT_PENDING;
    }

    /* Acknowledge the level source before calling into timer.device. */
    write_control(state, AROS_APPLE_TIMER_CTL_ENABLE |
        AROS_APPLE_TIMER_CTL_IMASK);
    state->armed = 0;
    ++state->fired_count;
    return AROSAppleTimerRefresh(state, state->owner);
}

int AROSAppleTimerArmed(const struct AROSAppleTimerState *state)
{
    return state_valid(state) && state->claimed != 0 && state->armed != 0;
}

const char *AROSAppleTimerStatusName(enum AROSAppleTimerStatus status)
{
    switch (status) {
    case AROS_APPLE_TIMER_OK:
        return "ok";
    case AROS_APPLE_TIMER_ARGUMENT:
        return "argument";
    case AROS_APPLE_TIMER_FDT:
        return "fdt";
    case AROS_APPLE_TIMER_UNSUPPORTED:
        return "unsupported";
    case AROS_APPLE_TIMER_STATE:
        return "state";
    case AROS_APPLE_TIMER_OWNERSHIP:
        return "ownership";
    case AROS_APPLE_TIMER_RANGE:
        return "range";
    case AROS_APPLE_TIMER_NOT_PENDING:
        return "not-pending";
    }
    return "unknown";
}
