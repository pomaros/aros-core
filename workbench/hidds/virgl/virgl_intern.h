/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
#ifndef AROS_VIRGL_INTERN_H
#define AROS_VIRGL_INTERN_H
#include <stdint.h>
#include <stdbool.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <gallium/screenfactory.h>
#include "virgl_transport.h"
#include <oop/oop.h>
#include <hidd/gallium.h>
#include LC_LIBDEFS_FILE
#define CLID_Hidd_Gallium_Virgl "hidd.gallium.virgl"
struct virgl_winsys;
struct pipe_screen;
struct HiddGalliumVirglData {
    struct avgl_manager manager;
    struct virgl_winsys *winsys;
    struct pipe_screen *screen;
    struct GalliumScreenFactory factory;
    struct SignalSemaphore screen_lock;
};
struct virglstaticdata {
    struct avgl_manager manager;
    OOP_Class *galliumclass;
    OOP_AttrBase hiddGalliumAB;
    struct Library *CyberGfxBase, *UtilityBase;
};
LIBBASETYPE { struct Library LibNode; struct virglstaticdata sd; };
#define METHOD(base,id,name) base ## __ ## id ## __ ## name (OOP_Class *cl,OOP_Object *o,struct p ## id ## _ ## name *msg)
#define BASE(lib) ((LIBBASETYPEPTR)(lib))
#define SD(cl) (&BASE(cl->UserData)->sd)
#endif
