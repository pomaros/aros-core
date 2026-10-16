/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License 1.1.
 */
#ifndef VIRTIO_STATUS_H
#define VIRTIO_STATUS_H
#include <stdint.h>

/* Transport callbacks must perform ordered, completed status accesses.
 * The caller serializes device users. A reset acknowledgement stops device
 * queue access (Virtio 1.2 section 2.4), not software tasks or IRQ callbacks.
 */
struct VirtioStatusAccess {
    void *context;
    uint8_t (*read)(void *);
    void (*write)(void *, uint8_t);
};

static inline int VirtioStatusReset(const struct VirtioStatusAccess *io,
                                    unsigned max_reads)
{
    if (!io || !io->read || !io->write || !max_reads) return 0;
    io->write(io->context, 0);
    /* Exactly max_reads attempts: no unsigned post-decrement wraparound. */
    for (unsigned attempt = 0; attempt < max_reads; ++attempt)
        if (io->read(io->context) == 0) return 1;
    return 0;
}

static inline int VirtioStatusBegin(const struct VirtioStatusAccess *io,
                                    unsigned max_reads)
{
    if (!VirtioStatusReset(io, max_reads)) return 0;
    io->write(io->context, 1); /* ACKNOWLEDGE */
    io->write(io->context, 3); /* ACKNOWLEDGE | DRIVER */
    return io->read(io->context) == 3;
}
#endif
