/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.
*/
#include <aros/libcall.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <proto/exec.h>
#include <proto/fd.h>
#include "fd_private.h"
#include LC_LIBDEFS_FILE

/* One allocation per exported file description and one binding per number.
   Admitted hooks hold a lease on their binding. Closing a descriptor retires
   that binding, so a concurrent operation cannot see freed provider data. */
struct buffer_file {
    struct Library *base;
    struct fd_buffer buffer;
    CONST_APTR address;
    ULONG bindings;
};
struct buffer_binding {
    struct buffer_file *file;
    LONG flags;
};

static LONG new_binding(struct buffer_file *file, LONG minimum, LONG flags,
                        BOOL exact, LONG *outfd)
{
    struct Library *FDBase = file->base;
    struct buffer_binding *b;
    LONG error, fd = minimum;
    ULONG refs;

    if (minimum < 0) return EINVAL;
    b = AllocVec(sizeof(*b), MEMF_PUBLIC);
    if (!b) return ENOMEM;
    b->file = file;
    b->flags = flags;
    refs = __atomic_load_n(&file->bindings, __ATOMIC_RELAXED);
    do {
        if (refs == (ULONG)~0U) {
            FreeVec(b);
            return EOVERFLOW;
        }
    } while (!__atomic_compare_exchange_n(&file->bindings, &refs, refs + 1, FALSE,
                                          __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    error = exact ? FD_Reserve(fd, FD_OWNER_BUFFER, b)
                  : FD_Alloc(minimum, FD_OWNER_BUFFER, b, &fd);
    if (error) {
        __atomic_sub_fetch(&file->bindings, 1, __ATOMIC_RELAXED);
        FreeVec(b);
    } else {
        *outfd = fd;
    }
    return error;
}

static LONG retire_binding(APTR data, APTR cookie)
{
    struct buffer_binding *b = data;
    struct buffer_file *file = b->file;
    (void)cookie;
    FreeVec(b);
    if (!__atomic_sub_fetch(&file->bindings, 1, __ATOMIC_ACQ_REL)) {
        struct Library *base = file->base;
        file->buffer.release(file->buffer.payload);
        FreeVec(file);
        CloseLibrary(base);
    }
    return 0;
}

static LONG buffer_close(APTR data, LONG fd, LONG *error)
{
    struct buffer_binding *b = data;
    struct Library *FDBase = b->file->base;
    LONG close_error;
    *error = FD_RetireOwner(fd, FD_OWNER_BUFFER, b, retire_binding, NULL, &close_error);
    if (!*error) *error = close_error;
    return *error ? -1 : 0;
}

static QUAD buffer_seek(APTR data, LONG fd, QUAD offset, LONG whence, LONG *error)
{
    struct buffer_binding *b = data;
    (void)fd;
    if (offset || (whence != SEEK_END && whence != SEEK_SET)) {
        *error = EINVAL;
        return -1;
    }
    return whence == SEEK_END ? b->file->buffer.size : 0;
}

static LONG buffer_fcntl(APTR data, LONG fd, LONG cmd, IPTR arg, LONG *error)
{
    struct buffer_binding *b = data;
    LONG result;
    (void)fd;
    switch (cmd) {
    case F_GETFD:
        return __atomic_load_n(&b->flags, __ATOMIC_RELAXED);
    case F_SETFD:
        if (arg & ~((IPTR)FD_CLOEXEC)) break;
        __atomic_store_n(&b->flags, (LONG)arg, __ATOMIC_RELAXED);
        return 0;
    case F_GETFL:
        return b->file->buffer.writable ? O_RDWR : O_RDONLY;
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
        *error = new_binding(b->file, (LONG)arg, cmd == F_DUPFD_CLOEXEC ? FD_CLOEXEC : 0,
                             FALSE, &result);
        return *error ? -1 : result;
    }
    *error = EINVAL;
    return -1;
}

static LONG buffer_dup(APTR data, LONG fd, LONG newfd, LONG *error)
{
    struct buffer_binding *b = data;
    (void)fd;
    *error = new_binding(b->file, newfd, 0, TRUE, &newfd);
    return *error ? -1 : 0;
}

const struct fd_hooks fd_buffer_hooks = {
    .fdh_Version = FD_HOOKS_VERSION, .fdh_close = buffer_close,
    .fdh_lseek = buffer_seek, .fdh_fcntl = buffer_fcntl, .fdh_dup = buffer_dup
};

static LONG export_buffer(struct fd_base *libBase, const struct fd_buffer *buffer,
                          CONST_APTR address, LONG flags, LONG *outfd)
{
    struct buffer_file *file;
    LONG error;
    if (!buffer || !buffer->provider || !buffer->payload || !buffer->release ||
        buffer->size <= 0 || !outfd || (flags & ~FD_CLOEXEC)) return EINVAL;
    if (address && (UQUAD)buffer->size - 1 > (uintptr_t)UINTPTR_MAX - (uintptr_t)address) return EOVERFLOW;
    file = AllocVec(sizeof(*file), MEMF_PUBLIC | MEMF_CLEAR);
    if (!file) return ENOMEM;
    file->base = OpenLibrary((CONST_STRPTR)"fd.library", 52);
    if (file->base != &libBase->fd_LibNode) {
        if (file->base) CloseLibrary(file->base);
        FreeVec(file);
        return ENOSYS;
    }
    file->buffer = *buffer;
    file->address = address;
    error = new_binding(file, 3, flags, FALSE, outfd);
    if (error) {
        CloseLibrary(file->base);
        FreeVec(file);
    }
    return error;
}

AROS_LH3(LONG, FD_ExportBuffer,
    AROS_LHA(const struct fd_buffer *, buffer, A0), AROS_LHA(LONG, flags, D0),
    AROS_LHA(LONG *, outfd, A1), struct fd_base *, libBase, 19, FD)
{
    AROS_LIBFUNC_INIT
    return export_buffer(libBase, buffer, NULL, flags, outfd);
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, FD_ExportMappedBuffer,
    AROS_LHA(const struct fd_buffer *, buffer, A0), AROS_LHA(CONST_APTR, address, A1),
    AROS_LHA(LONG, flags, D0), AROS_LHA(LONG *, outfd, A2),
    struct fd_base *, libBase, 21, FD)
{
    AROS_LIBFUNC_INIT
    if (!address) return EINVAL;
    return export_buffer(libBase, buffer, address, flags, outfd);
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, FD_AcquireBuffer,
    AROS_LHA(LONG, fd, D0), AROS_LHA(CONST_APTR, provider, A0),
    AROS_LHA(struct fd_lease *, lease, A1), AROS_LHA(APTR *, payload, A2),
    struct fd_base *, libBase, 20, FD)
{
    AROS_LIBFUNC_INIT
    struct Library *FDBase = &libBase->fd_LibNode;
    struct buffer_binding *b;
    LONG error;
    if (!provider || !lease || !payload) return EINVAL;
    error = FD_Acquire(fd, lease);
    if (error) return error;
    if (lease->owner == FD_OWNER_BUFFER && lease->hooks == &fd_buffer_hooks) {
        b = lease->data;
        if (b->file->buffer.provider == provider) {
            *payload = b->file->buffer.payload;
            return 0;
        }
    }
    FD_Release(lease);
    return EINVAL;
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, FD_AcquireBufferMapping,
    AROS_LHA(LONG, fd, D0), AROS_LHA(const struct fd_buffer_range *, range, A0),
    AROS_LHA(struct fd_lease *, lease, A1), AROS_LHA(APTR *, address, A2), struct fd_base *, libBase, 22, FD)
{
    AROS_LIBFUNC_INIT
    struct Library *FDBase = &libBase->fd_LibNode;
    LONG error;
    if (!range || !lease || !address || range->offset < 0 || range->length <= 0) return EINVAL;
    error = FD_Acquire(fd, lease);
    if (error) return error;
    error = ENODEV;
    if (lease->owner == FD_OWNER_BUFFER && lease->hooks == &fd_buffer_hooks) {
        struct buffer_binding *binding = lease->data;
        struct buffer_file *file = binding->file;
        if (range->offset > file->buffer.size || range->length > file->buffer.size - range->offset)
            error = EINVAL;
        else if (range->write && !file->buffer.writable)
            error = EACCES;
        else if (file->address) {
            *address = (APTR)((uintptr_t)file->address + (uintptr_t)range->offset);
            return 0;
        }
    }
    FD_Release(lease);
    return error;
    AROS_LIBFUNC_EXIT
}
