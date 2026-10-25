/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.
*/
#ifndef INTUITION_FIRSTWINDOWEVIDENCE_H
#define INTUITION_FIRSTWINDOWEVIDENCE_H

#include <exec/types.h>

/* Private one-shot diagnostics, owned by the library rather than a screen.
 * The caller holds IBase throughout Capture/Take. These helpers only copy
 * scalars: neither publication nor a failed constructor performs logging. */
struct FirstWindowEvidenceRecord
{
    ULONG workbench, screenType, hidden, aware, contextReady;
    ULONG scale, factor, baseFont, physicalFont, frameReady, frameFont, barHeight;
};
struct FirstWindowEvidence
{
    struct FirstWindowEvidenceRecord record[2]; /* attempt, publication */
    ULONG captured, emitted;
};

static inline VOID FirstWindowEvidenceCapture(struct FirstWindowEvidence *state,
    ULONG phase, const struct FirstWindowEvidenceRecord *record)
{
    ULONG bit = 1UL << phase;
    if (!(state->captured & bit))
    {
        state->record[phase] = *record;
        state->captured |= bit;
    }
}

static inline ULONG FirstWindowEvidenceTake(struct FirstWindowEvidence *state,
    struct FirstWindowEvidenceRecord *records)
{
    ULONG pending = state->captured & ~state->emitted;
    if (pending & 1) records[0] = state->record[0];
    if (pending & 2) records[1] = state->record[1];
    state->emitted |= pending;
    return pending;
}
#endif
