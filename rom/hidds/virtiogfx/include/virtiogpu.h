/* Experimental local interface; not an approved public ABI. */
#ifndef HIDD_VIRTIOGPU_H
#define HIDD_VIRTIOGPU_H
#include <exec/types.h>
#include <oop/oop.h>
#define IID_Hidd_VirtioGPU "hidd.virtiogpu.transport"
#define CLID_Hidd_VirtioGPU "hidd.virtiogpu.transport"
#define VIRTIOGPU_TRANSPORT_VERSION 1
#define VIRTIOGPU_MAX_REQUEST_BYTES (512UL * 1024)
#define VIRTIOGPU_MAX_RESPONSE_BYTES 8192UL
#define VIRTIOGPU_MAX_BACKING_BYTES (32UL * 1024 * 1024)
/* CreateObject on the display returns one owned reference. Acquire retains
 * it and Release consumes one reference. Callers must quiesce operations
 * before their last Release. Command copies input/output synchronously;
 * FALSE plus Healthy=FALSE means the device is unusable. A failed command
 * never authorizes freeing backing: Free may refuse pending/quarantined DMA.
 * AllocateID never returns scanout resource 1 or the reserved ID 0. */
enum {
    moHidd_VirtioGPU_Acquire,
    moHidd_VirtioGPU_Release,
    moHidd_VirtioGPU_Command,
    moHidd_VirtioGPU_AllocateID,
    moHidd_VirtioGPU_Healthy,
    moHidd_VirtioGPU_Allocate,
    moHidd_VirtioGPU_Free,
    moHidd_VirtioGPU_Address,
    moHidd_VirtioGPU_BackingBegin,
    moHidd_VirtioGPU_BackingEnd,
    num_Hidd_VirtioGPU_Methods
};
struct pHidd_VirtioGPU_Acquire { STACKED OOP_MethodID mID; STACKED ULONG version; };
struct pHidd_VirtioGPU_Release { STACKED OOP_MethodID mID; };
/* IDs and complete contiguous backing allocations belong to this service
 * object. Command rejects peer/unallocated IDs and unregistered bus ranges.
 * Free is refused while backing is attached or completion is uncertain.
 * Release of an abandoned device-visible object quarantines it; it never
 * resets the display's device. Callers retain a reference across each call. */
struct pHidd_VirtioGPU_Command {
    STACKED OOP_MethodID mID;
    STACKED const void *request;
    STACKED ULONG bytes;
    STACKED void *response;
    STACKED ULONG response_bytes;
    STACKED ULONG response_type;
};
struct pHidd_VirtioGPU_AllocateID { STACKED OOP_MethodID mID; STACKED BOOL context; };
struct pHidd_VirtioGPU_Healthy { STACKED OOP_MethodID mID; };
struct pHidd_VirtioGPU_Allocate { STACKED OOP_MethodID mID; STACKED ULONG bytes; };
struct pHidd_VirtioGPU_Free { STACKED OOP_MethodID mID; STACKED APTR pointer; };
struct pHidd_VirtioGPU_Address { STACKED OOP_MethodID mID; STACKED APTR pointer; STACKED UQUAD *address; };
struct pHidd_VirtioGPU_BackingBegin { STACKED OOP_MethodID mID; STACKED APTR pointer; STACKED ULONG flags; };
struct pHidd_VirtioGPU_BackingEnd { STACKED OOP_MethodID mID; STACKED APTR pointer; };
#endif
