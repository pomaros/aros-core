/* Owner state-machine regression: the actual transport.c compiled against a
 * synthetic used ring in ordinary memory. It drives gpu_reap and the owned
 * state transitions through late, rejected and malformed completions. It
 * never touches a PCI device, never waits on a timer and therefore proves
 * nothing about a real host hang, a lost interrupt or the 1 second deadline;
 * those remain the responsibility of runtime tests on a live owner. */
#include "../../../../../rom/hidds/virtiogfx/transport.c"
#include "../../crt/test.h"
static struct VirtioGPU gpu;
static struct virtio_queue queue;
static struct GPUAllocation allocation;
static APTR used_memory;
void cleanup(void)
{
    /* No DMA registrations or PCI allocations are made by this fixture. */
    while(gpu.objects){struct GPUObject *o=gpu.objects;gpu.objects=o->next;FreeMem(o,sizeof(*o));}
    gpu.owned_pending=NULL;
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
    /* Cache interval state alone must not authorize Free while the host
     * owns backing. These are synthetic registrations, not live PCI DMA. */
    allocation.dma_active=FALSE;allocation.host_refs=1;
    TEST(!VirtioGPUFree(&gpu,&gpu,allocation.pointer));
    struct GPUObject *object=AllocMem(sizeof(*object),MEMF_PUBLIC|MEMF_CLEAR);
    TEST(object!=NULL);gpu.objects=object;object->owner=&gpu;object->id=2;
    object->live=TRUE;object->backing=&allocation;object->backing_bytes=4096;gpu.resources=1;
    object->uncertain=TRUE;gpu.owned_pending=object;gpu.owned_pending_type=0x106;
    gpu.pending=TRUE;gpu.pending_next=4;gpu.fence=10;
    TEST(gpu_reap(&gpu)==0 && allocation.host_refs==1);
    queue.q_Used->ring[3].id=0;queue.q_Used->ring[3].len=24;queue.q_Used->idx=4;
    reply->type=0x1200;reply->fence=10;
    TEST(gpu_reap(&gpu)==-2 && !object->uncertain && !object->backing && !allocation.host_refs);
    /* A late successful detach removes exactly its provisional host ref. */
    allocation.host_refs=1;object->backing=&allocation;object->uncertain=TRUE;
    gpu.owned_pending=object;gpu.owned_pending_type=0x107;
    gpu.pending=TRUE;gpu.pending_next=5;gpu.fence=11;
    TEST(gpu_reap(&gpu)==0 && allocation.host_refs==1);
    queue.q_Used->ring[4].id=0;queue.q_Used->ring[4].len=24;queue.q_Used->idx=5;
    reply->type=0x1100;reply->fence=11;
    TEST(gpu_reap(&gpu)==1 && !object->backing && !allocation.host_refs);
    /* Unauthorized envelopes must not publish or consume a fence. */
    gpu.ready=TRUE;ULONG peer=0;UQUAD fence=gpu.fence;
    struct {struct GPUHeader header;ULONG resource,pad;} unref={.header.type=0x102,.resource=2};
    struct GPUHeader answer;
    TEST(!VirtioGPUCommand(&gpu,&peer,&unref,sizeof(unref),&answer,24,0x1100));
    struct GPUObject *context=AllocMem(sizeof(*context),MEMF_PUBLIC|MEMF_CLEAR);
    TEST(context!=NULL);context->owner=&gpu;context->id=7;context->context=TRUE;context->live=TRUE;
    context->next=gpu.objects;gpu.objects=context;gpu.contexts=1;
    struct GPUHeader destroy={.type=0x201,.context=7};
    TEST(!VirtioGPUCommand(&gpu,&peer,&destroy,24,&answer,24,0x1100));
    struct {struct GPUHeader header;ULONG resource,count;UQUAD address;ULONG bytes,pad;} attach=
        {.header.type=0x106,.resource=2,.count=1,.address=1,.bytes=4096};
    TEST(!VirtioGPUCommand(&gpu,&gpu,&attach,sizeof(attach),&answer,24,0x1100));
    struct GPUHeader display={.type=0x100};
    TEST(!VirtioGPUCommand(&gpu,&gpu,&display,24,&answer,24,0x1101));
    struct {struct GPUHeader header;ULONG x,y,z,w,h,d;UQUAD offset;ULONG resource,level,stride,layer;} transfer=
        {.header.type=0x205,.header.context=7,.w=1,.h=1,.d=1,.resource=2};
    object->width=object->height=object->depth=object->array_size=1;
    object->backing=&allocation;object->backing_bytes=4096;allocation.dma_active=TRUE;allocation.host_refs=1;
    object->owner=&peer;
    TEST(!VirtioGPUCommand(&gpu,&gpu,&transfer,sizeof(transfer),&answer,24,0x1100));
    object->owner=&gpu;transfer.x=~(ULONG)0;
    TEST(!VirtioGPUCommand(&gpu,&gpu,&transfer,sizeof(transfer),&answer,24,0x1100));
    transfer.x=0;transfer.offset=~(UQUAD)0;
    TEST(!VirtioGPUCommand(&gpu,&gpu,&transfer,sizeof(transfer),&answer,24,0x1100));
    transfer.offset=0;transfer.resource=1;
    TEST(!VirtioGPUCommand(&gpu,&gpu,&transfer,sizeof(transfer),&answer,24,0x1100));
    TEST(gpu.fence==fence && allocation.host_refs==1);
    /* Invalid completion and owner disposal cannot adopt or free the backing. */
    allocation.dma_active=FALSE;object->uncertain=TRUE;gpu.owned_pending=object;gpu.owned_pending_type=0x107;
    gpu.pending=TRUE;gpu.pending_next=6;gpu.fence=12;
    queue.q_Used->ring[5].id=1;queue.q_Used->ring[5].len=24;queue.q_Used->idx=6;
    reply->fence=12;
    TEST(gpu_reap(&gpu)==-1 && object->uncertain && allocation.host_refs==1);
    VirtioGPUForgetOwner(&gpu,&gpu);
    TEST(object->owner==NULL && context->owner==NULL && allocation.owner==NULL);
    TEST(gpu.allocations==&allocation && allocation.host_refs==1);
    cleanup();puts("completion retention: PASS");return OK;
}
