/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    glibc getauxval() function
*/

#include <aros/kernel.h>
#include <proto/exec.h>
#include <proto/kernel.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

/*****************************************************************************

    NAME */
#include <sys/auxv.h>

        unsigned long getauxval(

/*  SYNOPSIS */
        unsigned long type)

/*  FUNCTION
        Returns a value of the kind an ELF auxiliary vector carries.

    INPUTS
        type - AT_PAGESZ, AT_HWCAP or AT_HWCAP2

    RESULT
        The value, or 0 with errno set to ENOENT when the system has no such
        value.

    NOTES
        AROS has no auxiliary vector. AT_HWCAP and AT_HWCAP2 are the CPU
        feature words of kernel.resource (KATTR_HWCap, KATTR_HWCap2), with
        the bit numbers of Linux on AArch64; a kernel that does not report
        them answers ENOENT.

    EXAMPLE

    BUGS

    SEE ALSO
        sysconf()

    INTERNALS

******************************************************************************/
{
    APTR KernelBase;
    intptr_t value;
    ULONG attribute;

    switch (type)
    {
    case AT_PAGESZ:
        return (unsigned long)sysconf(_SC_PAGESIZE);
    case AT_HWCAP:
        attribute = KATTR_HWCap;
        break;
    case AT_HWCAP2:
        attribute = KATTR_HWCap2;
        break;
    default:
        errno = ENOENT;
        return 0;
    }

    KernelBase = OpenResource("kernel.resource");
    value = KernelBase ? KrnGetSystemAttr(attribute) : -1;
    if (value == -1)
    {
        errno = ENOENT;
        return 0;
    }
    return (unsigned long)value;
} /* getauxval() */
