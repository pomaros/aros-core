/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    The libdrm subset AROS gives the Mesa DRM drivers.

    Put this directory first on the include path so it replaces libdrm's
    own xf86drm.h. The functions are libdrm's, with libdrm's semantics:
    each is one or two drmIoctl() calls, implemented in aros_libdrm.c. The
    driver module supplies drmIoctl() itself and routes each request to its
    hardware resource (asahi: agx_aros_drm.c over agx.resource's client
    calls). Only the request's number, its low eight bits, is significant.

    No file descriptors exist behind "fd": it is any positive value and is
    passed back to drmIoctl() untouched. A sync file is what the module
    makes of SYNCOBJ_HANDLE_TO_FD with EXPORT_SYNC_FILE (asahi: a sync
    object holding a copy of the fence, behind a number the module's
    close() releases). Sync object and PRIME (dma-buf) file descriptors
    have no AROS counterpart; drmPrime* fail with ENOSYS, and the module
    refuses the plain handle/fd conversions.
*/

#ifndef AROS_XF86DRM_H
#define AROS_XF86DRM_H

#include <stdint.h>

#ifndef DRM_COMMAND_BASE
#define DRM_COMMAND_BASE 0x40
#endif

#define DRM_CAP_SYNCOBJ                 0x13
#define DRM_CAP_SYNCOBJ_TIMELINE        0x14

#define DRM_SYNCOBJ_CREATE_SIGNALED             (1 << 0)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL         (1 << 0)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT  (1 << 1)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE   (1 << 2)
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE    (1 << 3)
#define DRM_SYNCOBJ_QUERY_FLAGS_LAST_SUBMITTED  (1 << 0)

#ifndef DRM_CLOEXEC
#define DRM_CLOEXEC 02000000
#endif
#ifndef DRM_RDWR
#define DRM_RDWR 02
#endif

/* Request numbers the core DRM requests use (the low eight bits). */
#define AROS_DRM_NR_VERSION             0x00
#define AROS_DRM_NR_GEM_CLOSE           0x09
#define AROS_DRM_NR_GET_CAP             0x0c
#define AROS_DRM_NR_SYNCOBJ_CREATE      0xbf
#define AROS_DRM_NR_SYNCOBJ_DESTROY     0xc0
#define AROS_DRM_NR_SYNCOBJ_HANDLE_TO_FD 0xc1
#define AROS_DRM_NR_SYNCOBJ_FD_TO_HANDLE 0xc2
#define AROS_DRM_NR_SYNCOBJ_WAIT        0xc3
#define AROS_DRM_NR_SYNCOBJ_RESET       0xc4
#define AROS_DRM_NR_SYNCOBJ_SIGNAL      0xc5
#define AROS_DRM_NR_SYNCOBJ_TIMELINE_WAIT 0xca
#define AROS_DRM_NR_SYNCOBJ_QUERY       0xcb
#define AROS_DRM_NR_SYNCOBJ_TRANSFER    0xcc
#define AROS_DRM_NR_SYNCOBJ_TIMELINE_SIGNAL 0xcd

#define DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE (1 << 0)
#define DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE (1 << 0)

typedef struct _drmVersion {
    int version_major;
    int version_minor;
    int version_patchlevel;
    int name_len;
    char *name;
    int date_len;
    char *date;
    int desc_len;
    char *desc;
} drmVersion, *drmVersionPtr;

int drmIoctl(int fd, unsigned long request, void *arg);

/* A DRM device: one render node, aros_drm_render_node ("/dev/dri/renderD128",
 * device number 226:128, defined in aros_libdrm.c), which the module's open()
 * turns into a client (agxvulkan.hidd). No bus information is kept beyond
 * the bus type. */
#define DRM_NODE_PRIMARY 0
#define DRM_NODE_CONTROL 1
#define DRM_NODE_RENDER  2
#define DRM_NODE_MAX     3

#define DRM_BUS_PCI      0
#define DRM_BUS_USB      1
#define DRM_BUS_PLATFORM 2
#define DRM_BUS_HOST1X   3

typedef struct _drmPlatformBusInfo {
    char fullname[512];
} drmPlatformBusInfo, *drmPlatformBusInfoPtr;

typedef struct _drmPlatformDeviceInfo {
    char **compatible;
} drmPlatformDeviceInfo, *drmPlatformDeviceInfoPtr;

typedef struct _drmDevice {
    char **nodes;
    int available_nodes;
    int bustype;
    union {
        drmPlatformBusInfoPtr platform;
    } businfo;
    union {
        drmPlatformDeviceInfoPtr platform;
    } deviceinfo;
} drmDevice, *drmDevicePtr;

extern const char aros_drm_render_node[];
extern const unsigned long aros_drm_render_devid;

int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device);
int drmGetDeviceFromDevId(unsigned long dev_id, uint32_t flags, drmDevicePtr *device);
int drmGetDevices2(uint32_t flags, drmDevicePtr devices[], int max_devices);
void drmFreeDevice(drmDevicePtr *device);
void drmFreeDevices(drmDevicePtr devices[], int count);

drmVersionPtr drmGetVersion(int fd);
void drmFreeVersion(drmVersionPtr version);
int drmGetCap(int fd, uint64_t capability, uint64_t *value);

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy(int fd, uint32_t handle);
int drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles,
                   int64_t timeout_nsec, unsigned flags,
                   uint32_t *first_signaled);
int drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd);
int drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle);
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd);
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd);
int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t handle_count);
int drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t handle_count);
int drmSyncobjTimelineSignal(int fd, const uint32_t *handles,
                             uint64_t *points, uint32_t handle_count);
int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points,
                           unsigned num_handles, int64_t timeout_nsec,
                           unsigned flags, uint32_t *first_signaled);
int drmSyncobjQuery(int fd, uint32_t *handles, uint64_t *points,
                    uint32_t handle_count);
int drmSyncobjQuery2(int fd, uint32_t *handles, uint64_t *points,
                     uint32_t handle_count, uint32_t flags);
int drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point,
                       uint32_t src_handle, uint64_t src_point,
                       uint32_t flags);

int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle);

#endif
