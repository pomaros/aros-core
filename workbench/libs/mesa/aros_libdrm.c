/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    The libdrm subset AROS gives the Mesa DRM drivers; see
    aros_libdrm/xf86drm.h. Each function is libdrm's, over drmIoctl(),
    which the driver module supplies. The argument structures are the DRM
    UAPI's, declared here under private names so that this file needs no
    kernel headers and does not clash with Mesa's drm-uapi/drm.h.
*/

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "aros_libdrm/xf86drm.h"

struct aros_drm_version {
    int version_major;
    int version_minor;
    int version_patchlevel;
    unsigned long name_len;
    char *name;
    unsigned long date_len;
    char *date;
    unsigned long desc_len;
    char *desc;
};

struct aros_drm_get_cap {
    uint64_t capability;
    uint64_t value;
};

struct aros_drm_syncobj_create {
    uint32_t handle;
    uint32_t flags;
};

struct aros_drm_syncobj_destroy {
    uint32_t handle;
    uint32_t pad;
};

struct aros_drm_syncobj_handle {
    uint32_t handle;
    uint32_t flags;
    int32_t fd;
    uint32_t pad;
};

struct aros_drm_syncobj_wait {
    uint64_t handles;
    int64_t timeout_nsec;           /* absolute, CLOCK_MONOTONIC */
    uint32_t count_handles;
    uint32_t flags;
    uint32_t first_signaled;
    uint32_t pad;
    uint64_t deadline_nsec;
};

struct aros_drm_syncobj_timeline_wait {
    uint64_t handles;
    uint64_t points;
    int64_t timeout_nsec;           /* absolute, CLOCK_MONOTONIC */
    uint32_t count_handles;
    uint32_t flags;
    uint32_t first_signaled;
    uint32_t pad;
    uint64_t deadline_nsec;
};

struct aros_drm_syncobj_array {
    uint64_t handles;
    uint32_t count_handles;
    uint32_t pad;
};

struct aros_drm_syncobj_timeline_array {
    uint64_t handles;
    uint64_t points;
    uint32_t count_handles;
    uint32_t flags;
};

struct aros_drm_syncobj_transfer {
    uint32_t src_handle;
    uint32_t dst_handle;
    uint64_t src_point;
    uint64_t dst_point;
    uint32_t flags;
    uint32_t pad;
};

/* libdrm asks twice: once for the string lengths, once for the strings. */
drmVersionPtr drmGetVersion(int fd)
{
    struct aros_drm_version v;
    drmVersionPtr out;

    memset(&v, 0, sizeof(v));
    if (drmIoctl(fd, AROS_DRM_NR_VERSION, &v))
        return NULL;
    out = calloc(1, sizeof(*out));
    if (out == NULL)
        return NULL;
    v.name = calloc(1, v.name_len + 1);
    v.date = calloc(1, v.date_len + 1);
    v.desc = calloc(1, v.desc_len + 1);
    if (v.name == NULL || v.date == NULL || v.desc == NULL ||
        drmIoctl(fd, AROS_DRM_NR_VERSION, &v)) {
        free(v.name);
        free(v.date);
        free(v.desc);
        free(out);
        return NULL;
    }
    out->version_major = v.version_major;
    out->version_minor = v.version_minor;
    out->version_patchlevel = v.version_patchlevel;
    out->name_len = (int)v.name_len;
    out->name = v.name;
    out->date_len = (int)v.date_len;
    out->date = v.date;
    out->desc_len = (int)v.desc_len;
    out->desc = v.desc;
    return out;
}

void drmFreeVersion(drmVersionPtr version)
{
    if (version == NULL)
        return;
    free(version->name);
    free(version->date);
    free(version->desc);
    free(version);
}

int drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
    struct aros_drm_get_cap args;
    int ret;

    memset(&args, 0, sizeof(args));
    args.capability = capability;
    ret = drmIoctl(fd, AROS_DRM_NR_GET_CAP, &args);
    if (ret)
        return ret;
    *value = args.value;
    return 0;
}

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
    struct aros_drm_syncobj_create args;
    int ret;

    memset(&args, 0, sizeof(args));
    args.flags = flags;
    ret = drmIoctl(fd, AROS_DRM_NR_SYNCOBJ_CREATE, &args);
    if (ret)
        return ret;
    *handle = args.handle;
    return 0;
}

int drmSyncobjDestroy(int fd, uint32_t handle)
{
    struct aros_drm_syncobj_destroy args;

    memset(&args, 0, sizeof(args));
    args.handle = handle;
    return drmIoctl(fd, AROS_DRM_NR_SYNCOBJ_DESTROY, &args);
}

/* The one call that answers -errno rather than -1, as libdrm's does. */
int drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles,
                   int64_t timeout_nsec, unsigned flags,
                   uint32_t *first_signaled)
{
    struct aros_drm_syncobj_wait args;

    memset(&args, 0, sizeof(args));
    args.handles = (uint64_t)(uintptr_t)handles;
    args.timeout_nsec = timeout_nsec;
    args.count_handles = num_handles;
    args.flags = flags;
    if (drmIoctl(fd, AROS_DRM_NR_SYNCOBJ_WAIT, &args) < 0)
        return -errno;
    if (first_signaled)
        *first_signaled = args.first_signaled;
    return 0;
}

static int syncobj_handle_to_fd(int fd, uint32_t handle, uint32_t flags, int *obj_fd)
{
    struct aros_drm_syncobj_handle args;
    int ret;

    memset(&args, 0, sizeof(args));
    args.handle = handle;
    args.flags = flags;
    args.fd = -1;
    ret = drmIoctl(fd, AROS_DRM_NR_SYNCOBJ_HANDLE_TO_FD, &args);
    if (ret)
        return ret;
    *obj_fd = args.fd;
    return 0;
}

static int syncobj_fd_to_handle(int fd, int obj_fd, uint32_t flags, uint32_t *handle)
{
    struct aros_drm_syncobj_handle args;
    int ret;

    memset(&args, 0, sizeof(args));
    args.fd = obj_fd;
    args.flags = flags;
    args.handle = *handle;
    ret = drmIoctl(fd, AROS_DRM_NR_SYNCOBJ_FD_TO_HANDLE, &args);
    if (ret)
        return ret;
    *handle = args.handle;
    return 0;
}

int drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd)
{
    return syncobj_handle_to_fd(fd, handle, 0, obj_fd);
}

int drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle)
{
    *handle = 0;
    return syncobj_fd_to_handle(fd, obj_fd, 0, handle);
}

int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd)
{
    return syncobj_fd_to_handle(fd, sync_file_fd,
        DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE, &handle);
}

int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd)
{
    return syncobj_handle_to_fd(fd, handle,
        DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE, sync_file_fd);
}

static int syncobj_array(int fd, unsigned long nr, const uint32_t *handles, uint32_t count)
{
    struct aros_drm_syncobj_array args;

    memset(&args, 0, sizeof(args));
    args.handles = (uint64_t)(uintptr_t)handles;
    args.count_handles = count;
    return drmIoctl(fd, nr, &args);
}

int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t handle_count)
{
    return syncobj_array(fd, AROS_DRM_NR_SYNCOBJ_RESET, handles, handle_count);
}

int drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t handle_count)
{
    return syncobj_array(fd, AROS_DRM_NR_SYNCOBJ_SIGNAL, handles, handle_count);
}

int drmSyncobjTimelineSignal(int fd, const uint32_t *handles,
                             uint64_t *points, uint32_t handle_count)
{
    struct aros_drm_syncobj_timeline_array args;

    memset(&args, 0, sizeof(args));
    args.handles = (uint64_t)(uintptr_t)handles;
    args.points = (uint64_t)(uintptr_t)points;
    args.count_handles = handle_count;
    return drmIoctl(fd, AROS_DRM_NR_SYNCOBJ_TIMELINE_SIGNAL, &args);
}

/* As drmSyncobjWait, -errno rather than -1. */
int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points,
                           unsigned num_handles, int64_t timeout_nsec,
                           unsigned flags, uint32_t *first_signaled)
{
    struct aros_drm_syncobj_timeline_wait args;

    memset(&args, 0, sizeof(args));
    args.handles = (uint64_t)(uintptr_t)handles;
    args.points = (uint64_t)(uintptr_t)points;
    args.timeout_nsec = timeout_nsec;
    args.count_handles = num_handles;
    args.flags = flags;
    if (drmIoctl(fd, AROS_DRM_NR_SYNCOBJ_TIMELINE_WAIT, &args) < 0)
        return -errno;
    if (first_signaled)
        *first_signaled = args.first_signaled;
    return 0;
}

int drmSyncobjQuery2(int fd, uint32_t *handles, uint64_t *points,
                     uint32_t handle_count, uint32_t flags)
{
    struct aros_drm_syncobj_timeline_array args;

    memset(&args, 0, sizeof(args));
    args.handles = (uint64_t)(uintptr_t)handles;
    args.points = (uint64_t)(uintptr_t)points;
    args.count_handles = handle_count;
    args.flags = flags;
    return drmIoctl(fd, AROS_DRM_NR_SYNCOBJ_QUERY, &args);
}

int drmSyncobjQuery(int fd, uint32_t *handles, uint64_t *points,
                    uint32_t handle_count)
{
    return drmSyncobjQuery2(fd, handles, points, handle_count, 0);
}

int drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point,
                       uint32_t src_handle, uint64_t src_point,
                       uint32_t flags)
{
    struct aros_drm_syncobj_transfer args;

    memset(&args, 0, sizeof(args));
    args.src_handle = src_handle;
    args.dst_handle = dst_handle;
    args.src_point = src_point;
    args.dst_point = dst_point;
    args.flags = flags;
    return drmIoctl(fd, AROS_DRM_NR_SYNCOBJ_TRANSFER, &args);
}

int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{
    (void)fd; (void)handle; (void)flags; (void)prime_fd;
    errno = ENOSYS;
    return -1;
}

int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
    (void)fd; (void)prime_fd; (void)handle;
    errno = ENOSYS;
    return -1;
}
