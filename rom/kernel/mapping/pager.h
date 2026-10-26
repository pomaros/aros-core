/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#ifndef AROS_KERNEL_MAPPING_PAGER_H
#define AROS_KERNEL_MAPPING_PAGER_H

#include <stddef.h>
#include <stdint.h>

enum KrnPagerResult {
    KPAGER_OK, KPAGER_INVALID, KPAGER_NO_MEMORY, KPAGER_IO, KPAGER_TRUNCATED,
    KPAGER_CANCELLED
};
enum KrnPagerBacking { KPAGER_ZERO, KPAGER_FILE };
struct KrnPagerPage {
    uintptr_t physical;
    void *alias;
};
/* read_at runs on the pager's private handle, never the caller's seek
 * position. It may block and short-read. This service is therefore invoked
 * in task context without the mapping/PTE/allocator lock held. Allocation
 * failure must release any partial allocation itself. A successful allocation
 * transfers one page to this service, even if its returned geometry is invalid.
 * cancelled is optional and observes request retirement without retaining any
 * mapping lock across I/O. It cannot interrupt a blocked read; cancellation is
 * checked again when that read returns. The coordinator must still revalidate
 * the request generation under its lock before installing a returned page.
 * The caller pins ops, context, the private backing handle and allocated alias
 * until PageIn returns. Marking a request cancelled does not release those
 * pins. Read/cancellation callbacks must not reclaim the context or page;
 * page reclamation belongs to release or failed-allocation cleanup. */
struct KrnPagerOps {
    void *context;
    int (*allocate)(void *, size_t, struct KrnPagerPage *);
    void (*release)(void *, struct KrnPagerPage *, size_t);
    int (*read_at)(void *, uint64_t, void *, size_t, size_t *actual);
    int (*cancelled)(void *);
};

/* file_length is the source object's current size (the retained clone for
 * snapshot mappings). Reading an entirely post-EOF page is a task trap,
 * while the final partial page is zero beyond EOF. A short read before the
 * expected EOF detects a concurrent truncate, never returns invented bytes.
 * Success transfers a filled but unpublished physical page to the caller;
 * failed installation/completion must release that page through release.
 */
enum KrnPagerResult KrnPagerPageIn(const struct KrnPagerOps *, size_t page_size,
    enum KrnPagerBacking, uint64_t offset, uint64_t file_length,
    struct KrnPagerPage *);

#endif
