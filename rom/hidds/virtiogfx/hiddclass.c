/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1. */
#include "frontend.h"
#define EFIFBGfx__Root__New VirtioGfx_FramebufferNew
#include "../efifbgfx/efifbgfx_hiddclass.c"
#undef EFIFBGfx__Root__New
OOP_Object *EFIFBGfx__Root__New(OOP_Class *cl,OOP_Object *o,struct pRoot_New *msg)
{
    bug("[VirtioGfx] graphics New displayclass=%p bitmapclass=%p\n",XSD(cl)->displayclass,XSD(cl)->bmclass);
    OOP_Object *result=VirtioGfx_FramebufferNew(cl,o,msg);
    bug("[VirtioGfx] graphics New result=%p display=%p\n",result,XSD(cl)->efidisplay);
    return result;
}
