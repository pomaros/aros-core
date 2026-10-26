/*
 * Copyright (C) 2025 The AROS Development Team. All rights reserved.
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1.
 */
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <aros/types/locale_s.h>
#include "__langinfo.h"

char *nl_langinfo_l(nl_item item, locale_t loc)
{
    if (!loc)
        return nl_langinfo(item);
    if (loc == LC_GLOBAL_LOCALE)
    {
        errno = EINVAL;
        return "";
    }
    return __posixc_langinfo(item, loc->__lc_name);
}
