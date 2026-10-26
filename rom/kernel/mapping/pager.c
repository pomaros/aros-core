/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 */
#include "pager.h"

static int cancelled(const struct KrnPagerOps *ops)
{
    return ops->cancelled && ops->cancelled(ops->context);
}

enum KrnPagerResult KrnPagerPageIn(const struct KrnPagerOps *ops, size_t size,
    enum KrnPagerBacking backing, uint64_t offset, uint64_t file_length,
    struct KrnPagerPage *page)
{
    if (!page)
        return KPAGER_INVALID;
    *page = (struct KrnPagerPage){0, NULL};
    if (!ops || !ops->allocate || !ops->release || !size ||
        (size & (size-1)) || (offset & (size-1)) ||
        (backing != KPAGER_ZERO && backing != KPAGER_FILE) ||
        (backing == KPAGER_FILE && !ops->read_at))
        return KPAGER_INVALID;
    if (backing == KPAGER_FILE && offset >= file_length)
        return KPAGER_TRUNCATED;
    if (cancelled(ops))
        return KPAGER_CANCELLED;
    if (!ops->allocate(ops->context,size,page)) {
        *page = (struct KrnPagerPage){0,NULL};
        return KPAGER_NO_MEMORY;
    }
    if (!page->physical || !page->alias || (page->physical & (size-1))) {
        ops->release(ops->context,page,size);
        *page = (struct KrnPagerPage){0,NULL};
        return KPAGER_INVALID;
    }
    unsigned char *bytes=page->alias;
    for (size_t i=0;i<size;++i)
        bytes[i]=0;
    if (backing == KPAGER_ZERO) {
        if (!cancelled(ops))
            return KPAGER_OK;
        ops->release(ops->context,page,size);
        *page = (struct KrnPagerPage){0,NULL};
        return KPAGER_CANCELLED;
    }
    size_t length = file_length-offset < size ? (size_t)(file_length-offset) : size;
    size_t done=0;
    enum KrnPagerResult error=KPAGER_OK;
    while (done<length) {
        size_t actual=0;
        if (cancelled(ops)) {
            error=KPAGER_CANCELLED;
            break;
        }
        if (!ops->read_at(ops->context,offset+done,bytes+done,length-done,&actual) ||
            actual>length-done) {
            error=KPAGER_IO;
            break;
        }
        if (!actual) {
            error=KPAGER_TRUNCATED;
            break;
        }
        done+=actual;
    }
    if (error == KPAGER_OK && cancelled(ops))
        error=KPAGER_CANCELLED;
    if (error != KPAGER_OK) {
        ops->release(ops->context,page,size);
        *page = (struct KrnPagerPage){0,NULL};
    }
    return error;
}
