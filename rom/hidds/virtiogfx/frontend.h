/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef VIRTIOGFX_FRONTEND_H
#define VIRTIOGFX_FRONTEND_H
/* Share the maintained chunky bitmap/display classes, with distinct IDs. */
#include "../efifbgfx/efifbgfx_intern.h"
#include "../efifbgfx/efifbgfx_hidd.h"
#include "../efifbgfx/efifbgfx_display.h"
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
