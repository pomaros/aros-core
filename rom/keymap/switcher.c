/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * This file is distributed under the AROS Public License, version 1.1.
 */
#include "keymap_intern.h"
#include <devices/inputevent.h>
#include <devices/rawkeycodes.h>
#include <proto/exec.h>
#include <clib/keymap_protos.h>

/* Conversion and input-handler callers cannot take a sleeping semaphore.
 * Disable local preemption before spinning, so an interrupt cannot wait on
 * a lock held by the task it interrupted. No allocation, callbacks or I/O
 * are permitted in these short critical sections. */
static void SwitchLock(struct KeymapSwitcher *state)
{
    Disable();
#ifdef __AROSEXEC_SMP__
    while (__atomic_exchange_n(&state->Lock, 1, __ATOMIC_ACQUIRE))
        while (__atomic_load_n(&state->Lock, __ATOMIC_RELAXED))
            ;
#endif
}

static void SwitchUnlock(struct KeymapSwitcher *state)
{
#ifdef __AROSEXEC_SMP__
    __atomic_store_n(&state->Lock, 0, __ATOMIC_RELEASE);
#endif
    Enable();
}

/* Preserve KMS semantics: an active alternate overrides an explicit map too.
 * Maps are immutable and permanent, so the selected pointer survives unlock. */
struct KeyMap *KeymapSelect(struct KeymapBase *base, struct KeyMap *requested)
{
    struct KeymapSwitcher *state = &base->Switcher;
    SwitchLock(state);
    if (state->Active)
        requested = state->Alternate;
    SwitchUnlock(state);
    return requested ? requested : KeymapDefaultLoad(base);
}

/* ConfigureKeyMapSwitcher(alternate, qualifier, code), since version 43.
 * Publishes the complete configuration and resets activation in one operation.
 * NULL or KEYMAP_SWITCH_DISABLED disables switching. The supplied map must
 * already be initialized and must never be freed or modified afterwards.
 * Callable from task/input context; performs no allocation or blocking I/O. */
AROS_LH3(void, ConfigureKeyMapSwitcher,
    AROS_LHA(struct KeyMap *, alternate, A0),
    AROS_LHA(UWORD, qualifier, D0),
    AROS_LHA(UWORD, code, D1),
    struct Library *, KeymapBase, 9, Keymap)
{
    AROS_LIBFUNC_INIT
    struct KeymapSwitcher *state = &KMBase(KeymapBase)->Switcher;
    SwitchLock(state);
    state->Alternate = alternate;
    state->Qualifier = alternate ? qualifier : KEYMAP_SWITCH_DISABLED;
    state->Code = code;
    state->Active = FALSE;
    SwitchUnlock(state);
    AROS_LIBFUNC_EXIT
}

/* HandleKeyMapSwitcher(event), since version 43. Returns TRUE only when this
 * raw key event toggled the active map and should be swallowed by the input
 * handler. Mouse qualifiers do not participate in the hotkey match. */
AROS_LH1(BOOL, HandleKeyMapSwitcher,
    AROS_LHA(struct InputEvent *, event, A0),
    struct Library *, KeymapBase, 10, Keymap)
{
    AROS_LIBFUNC_INIT
    struct KeymapSwitcher *state = &KMBase(KeymapBase)->Switcher;
    BOOL consumed = FALSE;
    if (!event || event->ie_Class != IECLASS_RAWKEY)
        return FALSE;
    SwitchLock(state);
    if (state->Alternate && state->Qualifier != KEYMAP_SWITCH_DISABLED &&
        (event->ie_Qualifier & 0x03ff) == state->Qualifier &&
        ((state->Code == KEYMAP_SWITCH_NOKEY &&
          event->ie_Code >= RAWKEY_LSHIFT && event->ie_Code <= RAWKEY_RAMIGA) ||
         event->ie_Code == state->Code))
    {
        state->Active = !state->Active;
        consumed = TRUE;
    }
    SwitchUnlock(state);
    return consumed;
    AROS_LIBFUNC_EXIT
}
