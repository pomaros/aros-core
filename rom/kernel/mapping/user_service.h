/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_KERNEL_USER_SERVICE_H
#define AROS_KERNEL_USER_SERVICE_H
#include "service.h"
#include "user_buffer.h"
/* Internal, noncopyable kernel request. The trusted coordinator owns storage
 * and resolves worker/client request references; user integers are never raw
 * pointers. All calls hold the SAME mapping/service lock. Only these wrappers
 * may manipulate this request's underlying service token. The fixed byte
 * operation requires equal input/output length. No guest address reaches a
 * worker: input is copied and output is represented by a retained lease.
 * token is a RECORD-scoped generation, independent of the queue token. It
 * survives completion/cancellation; never reset it while stale references may
 * exist. Exhaustion retires this record. Only initialize before first use.
 * Cancel all outstanding requests when closing a domain; running cancellation
 * retains its output lease until worker acknowledgement. Close/permission
 * changes invalidate publication even if a worker completes before cancellation.
 */
struct KrnBufferService {
    struct KrnServiceQueue *queue;
    struct KrnBufferLease output;
    uint64_t principal, token, service_token;
    unsigned state;
};
int KrnBufferServiceSubmit(struct KrnBufferRegistry *, uint64_t principal,
    uint64_t domain_handle, struct KrnServiceQueue *, uintptr_t input,
    uintptr_t output, size_t length, const struct KrnBufferOps *,
    struct KrnBufferService *);
/* Worker-only references and captured tokens come from a trusted work queue.
 * Workers retain the submitted token: reading a reused record's new token
 * would let a delayed completion impersonate a later worker lease. */
int KrnBufferServiceTake(struct KrnBufferService *, uint64_t token, void *, size_t, size_t *);
int KrnBufferServiceComplete(struct KrnBufferService *, uint64_t token, const void *, size_t);
int KrnBufferServiceCancel(struct KrnBufferService *, uint64_t principal, uint64_t token);
/* A valid ready request is consumed even when publication fails (e.g. unmap
 * or protection change). Failure never publishes partial output. Token/owner
 * mismatch and requests not ready have no side effects. */
int KrnBufferServiceCollect(struct KrnBufferService *, uint64_t principal,
    uint64_t token, const struct KrnBufferOps *);
#endif
