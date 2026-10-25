#ifndef _POSIXC_SYS_AUXV_H_
#define _POSIXC_SYS_AUXV_H_

/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    getauxval() as glibc provides it. AROS has no ELF auxiliary vector; the
    values it can answer come from the system: the page size and, on
    AArch64, the CPU feature words, whose bits are those of Linux's
    AT_HWCAP and AT_HWCAP2 (KATTR_HWCap and KATTR_HWCap2 of kernel.resource).
*/

#include <aros/system.h>

#define AT_NULL     0
#define AT_PAGESZ   6
#define AT_HWCAP    16
#define AT_HWCAP2   26

__BEGIN_DECLS

unsigned long getauxval(unsigned long type);

__END_DECLS

#endif /* _POSIXC_SYS_AUXV_H_ */
