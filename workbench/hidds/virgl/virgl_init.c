/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
#include <aros/symbolsets.h>
#include <hidd/gallium.h>
#include <proto/oop.h>
#include <proto/exec.h>
#include "virgl_intern.h"
static int HiddVirgl_InitLib(LIBBASETYPEPTR LIBBASE)
{
    LIBBASE->sd.UtilityBase=OpenLibrary("utility.library",0);
    avgl_manager_init(&LIBBASE->sd.manager,LIBBASE->sd.UtilityBase);
    LIBBASE->sd.CyberGfxBase=OpenLibrary("cybergraphics.library",0);
    LIBBASE->sd.hiddGalliumAB=OOP_ObtainAttrBase(IID_Hidd_Gallium);
    if(LIBBASE->sd.UtilityBase && LIBBASE->sd.CyberGfxBase && LIBBASE->sd.hiddGalliumAB)return TRUE;
    if(LIBBASE->sd.hiddGalliumAB){OOP_ReleaseAttrBase(IID_Hidd_Gallium);LIBBASE->sd.hiddGalliumAB=0;}
    if(LIBBASE->sd.CyberGfxBase){CloseLibrary(LIBBASE->sd.CyberGfxBase);LIBBASE->sd.CyberGfxBase=NULL;}
    if(LIBBASE->sd.UtilityBase){CloseLibrary(LIBBASE->sd.UtilityBase);LIBBASE->sd.UtilityBase=NULL;}
    return FALSE;
}
static int HiddVirgl_ExpungeGuard(LIBBASETYPEPTR LIBBASE)
{
    return avgl_manager_can_expunge(&LIBBASE->sd.manager);
}
static int HiddVirgl_ExpungeLib(LIBBASETYPEPTR LIBBASE)
{
    if(LIBBASE->sd.hiddGalliumAB)OOP_ReleaseAttrBase(IID_Hidd_Gallium);
    if(LIBBASE->sd.CyberGfxBase)CloseLibrary(LIBBASE->sd.CyberGfxBase);
    if(LIBBASE->sd.UtilityBase)CloseLibrary(LIBBASE->sd.UtilityBase);
    return TRUE;
}
ADD2INITLIB(HiddVirgl_InitLib,0)
ADD2EXPUNGELIB(HiddVirgl_ExpungeGuard,100)
ADD2EXPUNGELIB(HiddVirgl_ExpungeLib,0)
ADD2LIBS((STRPTR)"gallium.hidd",7,static struct Library *,GalliumHiddBase);
