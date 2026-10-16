#ifndef EXEC_PROCESSMESSAGE_H
#define EXEC_PROCESSMESSAGE_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/

/* TryProcessMessage: only TPM_OK transfers ownership of a non-NULL message.
 * TPM_BUSY leaves it untouched and permits a later retry. */
#define TPM_OK          0
#define TPM_DEAD        1
#define TPM_BUSY        2
#define TPM_INVALID     3
#define TPM_UNSUPPORTED 4

#endif
