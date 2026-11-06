/* Native bounded state-machine regression using the actual owner code.
 * Synthetic queue memory never belongs to a live PCI device. */
#include "../../../../../rom/hidds/virtiogfx/transport.c"
#include "../../crt/test.h"
static struct VirtioGPU gpu;
static struct virtio_queue queue;
static struct GPUAllocation allocation;
static APTR used_memory;
void cleanup(void)
{
    /* No DMA registrations or PCI allocations are made by this fixture. */
    if(allocation.pointer)FreeVec(allocation.pointer);
    if(gpu.message)FreeVec(gpu.message);
    if(used_memory)FreeVec(used_memory);
    allocation.pointer=gpu.message=used_memory=NULL;
}
int main(void)
{
    InitSemaphore(&gpu.lock);
    gpu.message=AllocVec(GPU_MESSAGE_BYTES,MEMF_PUBLIC|MEMF_CLEAR);TEST(gpu.message!=NULL);
    used_memory=AllocVec(4096,MEMF_PUBLIC|MEMF_CLEAR);TEST(used_memory!=NULL);
    queue.q_Used=used_memory;gpu.device.dev_VQ=&queue;
    gpu.pending=TRUE;gpu.pending_next=1;gpu.pending_bytes=24;gpu.pending_type=0x1100;gpu.fence=7;
    allocation.owner=&gpu;allocation.pointer=AllocVec(4096,MEMF_PUBLIC);TEST(allocation.pointer!=NULL);
    allocation.bytes=4096;allocation.dma_active=TRUE;gpu.allocations=&allocation;
    TEST(gpu_reap(&gpu)==0 && gpu.pending);
    VirtioGPUBackingEnd(&gpu,&gpu,allocation.pointer);
    TEST(allocation.dma_active && gpu.allocations==&allocation);
    TEST(!VirtioGPUFree(&gpu,&gpu,allocation.pointer));
    struct GPUHeader *reply=(APTR)((UBYTE*)gpu.message+GPU_REQUEST_BYTES);
    reply->type=0x1100;reply->flags=1;reply->fence=7;
    queue.q_Used->ring[0].id=0;queue.q_Used->ring[0].len=24;
    queue.q_Used->idx=1;
    TEST(gpu_reap(&gpu)==1 && !gpu.pending && !gpu.failed);
    /* A malformed completion poisons the owner and cannot authorize Free. */
    gpu.pending=TRUE;gpu.pending_next=2;gpu.fence=8;
    queue.q_Used->ring[1].id=1;queue.q_Used->ring[1].len=24;
    queue.q_Used->idx=2;reply->fence=8;
    TEST(gpu_reap(&gpu)==-1 && gpu.failed);
    VirtioGPUBackingEnd(&gpu,&gpu,allocation.pointer);
    TEST(allocation.dma_active && !VirtioGPUFree(&gpu,&gpu,allocation.pointer));
    /* Complete fenced host rejection is scoped to its operation. */
    gpu.failed=FALSE;gpu.pending=TRUE;gpu.pending_next=3;gpu.fence=9;
    queue.q_Used->ring[2].id=0;queue.q_Used->ring[2].len=24;queue.q_Used->idx=3;
    reply->type=0x1200;reply->fence=9;
    TEST(gpu_reap(&gpu)==-2 && !gpu.failed && !gpu.pending);
    cleanup();puts("completion retention: PASS");return OK;
}
