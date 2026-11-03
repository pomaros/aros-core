#ifndef VENUS_INTERN_H
#define VENUS_INTERN_H

/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

#include <exec/libraries.h>
#include <oop/oop.h>

struct IntHiddVenusBase
{
    struct Library      base;
    OOP_Class          *venusclass;
    struct Library     *vulkanhidd;
    struct Library     *nativehidd;
    struct Library     *venuslib;
    int32_t             slot;
};

#define METHOD(base, id, name) \
  base ## __ ## id ## __ ## name (OOP_Class *cl, OOP_Object *o, struct p ## id ## _ ## name *msg)

#endif
