/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <proto/exec.h>
#include <proto/oop.h>
#include <aros/symbolsets.h>
#include "pciecam.h"
#undef OOPBase
static int init(struct PCIECAMBase *base)
{
    struct Library *OOPBase = base->oop;
    base->pci = OpenLibrary("pci.hidd", 0);
    if (!base->pci) return FALSE;
    base->utility = OpenLibrary("utility.library", 0);
    if (!base->utility) { CloseLibrary(base->pci); base->pci=NULL; return FALSE; }
    base->config_attr = OOP_ObtainAttrBase(IID_Hidd_PCIECAM);
    base->driver_attr = OOP_ObtainAttrBase(IID_Hidd_PCIDriver);
    if (base->config_attr && base->driver_attr) return TRUE;
    if (base->config_attr) OOP_ReleaseAttrBase(IID_Hidd_PCIECAM);
    if (base->driver_attr) OOP_ReleaseAttrBase(IID_Hidd_PCIDriver);
    CloseLibrary(base->utility); base->utility=NULL;
    CloseLibrary(base->pci); base->pci = NULL;
    return FALSE;
}
ADD2INITLIB(init, 0)
