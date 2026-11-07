/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef VIRTIOGFX_FRONTEND_H
#define VIRTIOGFX_FRONTEND_H
#include "transport.h"
#include "include/virtiogpu.h"
#include <exec/semaphores.h>
#include <utility/tagitem.h>
#define VIRTIOGPU_OWNER_TAG (TAG_USER + 0x4750)
struct VirtioGPUServiceData {
    struct VirtioGPU *gpu;
    struct Library *module;
    struct SignalSemaphore lock;
    ULONG references;
};
/* Share the maintained chunky bitmap/display classes, with distinct IDs. */
#include "../efifbgfx/efifbgfx_intern.h"
#include "../efifbgfx/efifbgfx_hidd.h"
#include "../efifbgfx/efifbgfx_display.h"
struct VirtioGfxBase {
    struct Library library;
    struct EFIFBGfx_staticdata vsd;
    OOP_Class *serviceclass;
    struct Library *virglmodule;
    struct SignalSemaphore loader_lock;
};
#undef XSD
#define XSD(cl) (&((struct VirtioGfxBase *)cl->UserData)->vsd)
#undef CLID_Hidd_Gfx_EFIFB
#undef CLID_Hidd_Display_EFIFB
#undef CLID_Hidd_BitMap_EFIFB
#define CLID_Hidd_Gfx_EFIFB "hidd.gfx.virtio"
#define CLID_Hidd_Display_EFIFB "hidd.display.virtio"
#define CLID_Hidd_BitMap_EFIFB "hidd.bitmap.virtio"
#define FB_DRIVER_NAME "virtiogfx.hidd"
#define FB_HARDWARE_NAME "Virtio GPU 2D"
#define FB_PRODUCER_NAME "Virtio"
#endif
