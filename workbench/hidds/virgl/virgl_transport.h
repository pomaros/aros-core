/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
#ifndef AROS_VIRGL_TRANSPORT_H
#define AROS_VIRGL_TRANSPORT_H
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <oop/oop.h>
#include <hidd/virtiogpu.h>
#include <stdint.h>
#include "virgl_gpu_protocol.h"
struct avgl_manager {
    struct SignalSemaphore mutex;
    struct Library *utility_base;
    ULONG references;
    BOOL initialized;
    OOP_Object *service;
};
/* Winsys client state. Physical ownership and quarantine stay in virtiogfx. */
struct avgl_transport {
    struct avgl_manager *manager;
    struct Library *utility_base;
    UBYTE caps[8192];
    ULONG caps_size, context, resources_live;
    BOOL context_created, failed;
    OOP_Object *service;
};
void avgl_manager_init(struct avgl_manager *m, struct Library *utility_base);
BOOL avgl_manager_can_expunge(struct avgl_manager *m);
BOOL avgl_healthy(struct avgl_transport *s);
ULONG avgl_resource_id(struct avgl_transport *s);
BOOL avgl_cleanup_command(struct avgl_transport *s, const void *input, ULONG bytes);



BOOL avgl_open(struct avgl_transport *s, struct avgl_manager *manager);
BOOL avgl_close(struct avgl_transport *s);
BOOL avgl_command(struct avgl_transport *s, const void *input, ULONG bytes,
    ULONG expected, ULONG minimum, ULONG *received, BOOL fenced);
UQUAD avgl_dma_address(struct avgl_transport *s, APTR pointer);
APTR avgl_alloc(struct avgl_transport *s, ULONG bytes);
BOOL avgl_backing_begin(struct avgl_transport *s, APTR pointer, ULONG flags);
void avgl_backing_end(struct avgl_transport *s, APTR pointer);
void avgl_free(struct avgl_transport *s, APTR pointer);
#endif
