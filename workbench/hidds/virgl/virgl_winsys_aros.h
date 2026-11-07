/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
#ifndef AROS_VIRGL_WINSYS_H
#define AROS_VIRGL_WINSYS_H
#include <stdbool.h>
#include <gallium/screenfactory.h>
#include "util/format/u_formats.h"
#include "virgl/virgl_winsys.h"
struct avgl_manager;
struct virgl_winsys *avgl_winsys_create(struct virgl_winsys **owner_slot,
    const struct GalliumScreenFactory *factory, struct avgl_manager *manager);
void avgl_winsys_dispose(struct virgl_winsys *ws);
bool avgl_read_resource(struct virgl_winsys *ws, struct virgl_hw_res *res,
    unsigned width, unsigned height, unsigned stride);
#endif
