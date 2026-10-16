/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
/* Public products do not include the native system-volume updater. */
#include <string.h>
#include "status.h"

void SoftwareUpdateReadStatus(struct UpdateStatus *status)
{
    strcpy(status->installed, "Not available on this target");
    strcpy(status->previous, "Not available on this target");
    strcpy(status->pending, "Automatic system updates are unavailable");
    strcpy(status->delivered, "Use the product distribution to update");
}
