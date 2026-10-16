/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1. */
#ifndef AROS_VIRTIO_PCI_MAPPING_H
#define AROS_VIRTIO_PCI_MAPPING_H
#include <stdint.h>
#include <stddef.h>
struct VirtioPCIMapping {
    void *regions[4];
    uint32_t bytes[4], notify_multiplier;
};
struct VirtioPCIAccess {
    void *context;
    uint32_t (*read)(void *,unsigned,unsigned);
    int (*bar)(void *,unsigned,uint64_t *,uint64_t *);
    void *(*map)(void *,uint64_t,uint32_t);
    void (*unmap)(void *,void *,uint32_t);
};
/* Caller must quiesce the device and remove consumers before unmapping. */
static inline void VirtioPCIUnmap(const struct VirtioPCIAccess *io,struct VirtioPCIMapping *m)
{
    for(unsigned i=4;i>0;--i) {
        unsigned at=i-1;
        if(m->regions[at]) io->unmap(io->context,m->regions[at],m->bytes[at]);
        m->regions[at]=NULL;m->bytes[at]=0;
    }
    m->notify_multiplier=0;
}
/* Conventional PCI capability list. No device reset, decode or DMA changes.
 * Repeated supported capabilities use the first instance; unknown types are
 * skipped. On failure every mapping acquired here is released. */
static inline int VirtioPCIMap(const struct VirtioPCIAccess *io,struct VirtioPCIMapping *out,unsigned required)
{
    struct VirtioPCIMapping m={0};
    uint8_t seen[64]={0};
    unsigned cap,found=0;
    if(!io || !out || !io->read || !io->bar || !io->map || !io->unmap || (required&~15u)) return 0;
    for(unsigned i=0;i<4;++i) if(out->regions[i]) return 0;
    cap=io->read(io->context,0x34,1);
    for(unsigned count=0;cap;++count) {
        if(count>=48 || cap<0x40 || cap>0xfc || (cap&3) || seen[cap/4]++) goto fail;
        unsigned next=io->read(io->context,cap+1,1);
        if(io->read(io->context,cap,1)==9) {
            unsigned size=io->read(io->context,cap+2,1);
            unsigned type=io->read(io->context,cap+3,1);
            if(type>=1 && type<=4 && !(found&(1u<<(type-1)))) {
                unsigned at=type-1;
                uint64_t base,span;
                if(size<(type==2?20u:16u) || size>256-cap) goto fail;
                unsigned bar=io->read(io->context,cap+4,1);
                uint32_t offset=io->read(io->context,cap+8,4), bytes=io->read(io->context,cap+12,4);
                unsigned alignment=type==1?8:type==2?2:1;
                unsigned minimum=type==1?56:type==2?2:1;
                if(bar>5 || bytes<minimum || (offset&(alignment-1)) ||
                    !io->bar(io->context,bar,&base,&span) || offset>span || bytes>span-offset ||
                    base>UINT64_MAX-offset || base+offset>UINT64_MAX-bytes) goto fail;
                void *cpu=io->map(io->context,base+offset,bytes);
                if(!cpu || cpu==(void *)(uintptr_t)UINTPTR_MAX) goto fail;
                m.regions[at]=cpu;m.bytes[at]=bytes;
                if((uintptr_t)cpu&(alignment-1) || bytes-1>UINTPTR_MAX-(uintptr_t)cpu) goto fail;
                if(type==2) m.notify_multiplier=io->read(io->context,cap+16,4);
                found|=1u<<at;
            }
        }
        cap=next;
    }
    if((found&required)!=required) goto fail;
    *out=m;return 1;
fail:
    VirtioPCIUnmap(io,&m);return 0;
}
static inline void *VirtioPCINotify(const struct VirtioPCIMapping *m,uint16_t queue_offset)
{
    uint64_t offset=(uint64_t)queue_offset*m->notify_multiplier;
    if(!m->regions[1] || (offset&1) || offset>m->bytes[1] || m->bytes[1]-offset<2)
        return NULL;
    return (uint8_t *)m->regions[1]+offset;
}
#endif
