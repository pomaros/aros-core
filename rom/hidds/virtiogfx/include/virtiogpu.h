/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * hidd.virtiogpu.transport: the VirtIO GPU transport service offered by the
 * virtiogfx display owner to in-guest protocol clients (currently VirGL).
 *
 * This is an experimental local interface of the shared-transport
 * integration. It is not an approved public ABI and has not been accepted by
 * the AROS or virtiogfx maintainers. Version 1 may change incompatibly.
 */
#ifndef HIDD_VIRTIOGPU_H
#define HIDD_VIRTIOGPU_H
#include <exec/types.h>
#include <oop/oop.h>

#define IID_Hidd_VirtioGPU "hidd.virtiogpu.transport"
#define CLID_Hidd_VirtioGPU "hidd.virtiogpu.transport"

/* Acquire refuses any other version. */
#define VIRTIOGPU_TRANSPORT_VERSION 1

/* Bounds checked by the owner on every call. */
#define VIRTIOGPU_MAX_REQUEST_BYTES (512UL * 1024)
#define VIRTIOGPU_MAX_RESPONSE_BYTES 8192UL
#define VIRTIOGPU_MAX_BACKING_BYTES (32UL * 1024 * 1024)

/*
 * Obtaining a service
 *
 * HIDD_Display_CreateObject on a virtiogfx display, asked for this class,
 * returns a new service object holding one reference, or NULL when the host
 * device has no VirGL feature or the owner is unhealthy. The display also
 * creates one internally for every Gallium client it binds. Each service
 * object is a separate client identity: IDs and allocations obtained
 * through it belong to it and are refused through any other service.
 *
 * References and lifetime
 *
 * Acquire adds one reference after checking the version; Release consumes
 * one. The last Release disposes the object. A caller must hold a reference
 * across every call and must have quiesced its own commands before its last
 * Release. Disposal never resets the device and never frees memory the
 * device may still use: live or uncertain objects of the identity become
 * orphans that the owner retains until the display itself closes the
 * physical device, after the scanout and every client are gone.
 *
 * Synchronisation
 *
 * The owner serialises every user of the control queue, including the
 * display's own Present, under one lock and publishes one command at a
 * time. Command is synchronous: it returns after the host completed or
 * rejected the request, or after the 1 second device deadline passed. The
 * caller's request bytes are copied before validation, so they may be
 * reused immediately after the call. Task context only; never from an
 * interrupt.
 *
 * Command
 *
 * request/bytes is a complete VirtIO GPU control request including its
 * 24-byte header; response/response_bytes receives the complete reply and
 * response_type is the reply type the caller expects. The owner checks:
 * header flags and ring are zero; the command type is one of
 * GET_DISPLAY_INFO, GET_CAPSET_INFO, GET_CAPSET, CTX_CREATE, CTX_DESTROY,
 * CTX_ATTACH_RESOURCE, CTX_DETACH_RESOURCE, RESOURCE_CREATE_3D,
 * RESOURCE_UNREF, RESOURCE_ATTACH_BACKING, RESOURCE_DETACH_BACKING,
 * TRANSFER_TO_HOST_3D, TRANSFER_FROM_HOST_3D or SUBMIT_3D (scanout, 2D and
 * reset commands are refused); the exact wire size of each; that every
 * context and resource ID was obtained from this service's AllocateID and
 * is in the state the command requires; that ATTACH_BACKING names exactly
 * one entry equal to a whole allocation of this service; that 3D transfer
 * boxes lie within the geometry recorded at RESOURCE_CREATE_3D, within the
 * attached backing, and that the backing is inside a BackingBegin/End
 * interval; that SUBMIT_3D's size matches the request; and that the reply
 * type and size match what the command produces. Format, bind, sampler
 * and command-stream contents are not interpreted by the owner: the host
 * renderer validates them, and the client interprets the replies.
 *
 * Results and errors
 *
 * Command returns TRUE only when the host acknowledged the request; the
 * reply is then copied to response. FALSE covers a refused request (nothing
 * was published), a host rejection (a complete fenced error reply), a
 * device deadline, or an unhealthy owner. After FALSE with Healthy still
 * TRUE the operation failed and the service remains usable. After FALSE
 * with Healthy FALSE the owner has marked the device unusable: a completion
 * arrived that did not match the published command, or a deadline passed
 * and the late completion never came. The owner never resets a device whose
 * scanout is live; continued desktop updates after device loss are not
 * promised. A failed command never authorises freeing backing.
 *
 * Deadline and quarantine
 *
 * After a deadline the owner keeps the published command and its DMA
 * storage in place and refuses new commands until the late completion is
 * reaped by a later call. State-changing commands (context create/destroy,
 * resource create/unref, backing attach/detach) are applied provisionally
 * and committed, reverted or quarantined by the completion they receive,
 * immediate or late. An object whose completion was invalid stays
 * uncertain: it cannot be used, its backing cannot be freed, and it is only
 * released by the physical stop.
 *
 * IDs and memory
 *
 * AllocateID returns a fresh context ID (context=TRUE) or resource ID. One
 * allocator covers the display's scanout (resource 1) and every client;
 * resource 0 and 1 are never returned. Allocate returns a zeroed,
 * physically contiguous DMA-capable block of at most
 * VIRTIOGPU_MAX_BACKING_BYTES owned by this service; Address reports its
 * bus address for the client's ATTACH_BACKING entry, which the owner
 * re-validates. BackingBegin/BackingEnd bracket host access through the
 * platform's CachePreDMA/CachePostDMA with flags 0 or DMA_ReadFromRAM; a
 * transfer is refused outside such an interval. Free releases a block only
 * when it is detached, outside a DMA interval, no command is pending and
 * the owner is healthy; otherwise it returns FALSE and the block stays
 * registered.
 */
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

/* BOOL: TRUE when the version matched and one reference was added. */
struct pHidd_VirtioGPU_Acquire { STACKED OOP_MethodID mID; STACKED ULONG version; };
/* Consumes one reference; the object may be gone on return. */
struct pHidd_VirtioGPU_Release { STACKED OOP_MethodID mID; };
/* BOOL: TRUE when the host acknowledged the request and response is valid. */
struct pHidd_VirtioGPU_Command {
    STACKED OOP_MethodID mID;
    STACKED const void *request;
    STACKED ULONG bytes;
    STACKED void *response;
    STACKED ULONG response_bytes;
    STACKED ULONG response_type;
};
/* ULONG: a new ID of the requested kind, or 0 when refused. */
struct pHidd_VirtioGPU_AllocateID { STACKED OOP_MethodID mID; STACKED BOOL context; };
/* BOOL: the owner is ready, has the VirGL feature and no failure or pending command. */
struct pHidd_VirtioGPU_Healthy { STACKED OOP_MethodID mID; };
/* APTR: a zeroed DMA-capable block owned by this service, or NULL. */
struct pHidd_VirtioGPU_Allocate { STACKED OOP_MethodID mID; STACKED ULONG bytes; };
/* BOOL: TRUE when the block was released; FALSE keeps it registered. */
struct pHidd_VirtioGPU_Free { STACKED OOP_MethodID mID; STACKED APTR pointer; };
/* BOOL: TRUE and *address set for a block of this service. */
struct pHidd_VirtioGPU_Address { STACKED OOP_MethodID mID; STACKED APTR pointer; STACKED UQUAD *address; };
/* BOOL: TRUE when the whole block is prepared for device access. */
struct pHidd_VirtioGPU_BackingBegin { STACKED OOP_MethodID mID; STACKED APTR pointer; STACKED ULONG flags; };
/* Ends the interval; a pending or failed owner keeps it open. */
struct pHidd_VirtioGPU_BackingEnd { STACKED OOP_MethodID mID; STACKED APTR pointer; };
#endif
