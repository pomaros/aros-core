/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#ifndef FONTDIAG_WORKBENCH_H
#define FONTDIAG_WORKBENCH_H

/* LockPubScreen("Workbench") creates Workbench if absent. A diagnostic run
   before IPrefs must observe absence without changing the boot sequence.
   Hold the public list until the ordinary visitor pin has been obtained;
   LockPubScreenList's SignalSemaphore is recursive for the current task. */
static struct Screen *FontDiag_LockExistingWorkbench(void)
{
    struct List *list = LockPubScreenList();
    struct PubScreenNode *node;
    struct Screen *screen = NULL;

    for (node = (struct PubScreenNode *)list->lh_Head;
         node->psn_Node.ln_Succ;
         node = (struct PubScreenNode *)node->psn_Node.ln_Succ)
    {
        if (node->psn_Node.ln_Name &&
            !strcmp(node->psn_Node.ln_Name, "Workbench") &&
            node->psn_Screen && !(node->psn_Flags & PSNF_PRIVATE))
        {
            screen = LockPubScreen("Workbench");
            break;
        }
    }
    UnlockPubScreenList();
    return screen;
}

#endif
