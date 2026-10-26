/*
 * Copyright (C) 2025 The AROS Development Team. All rights reserved.
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1.
 */
#include <langinfo.h>
#include <locale.h>
#include "__langinfo.h"

char *nl_langinfo(nl_item item)
{
    return __posixc_langinfo(item, setlocale(LC_CTYPE, NULL));
}
