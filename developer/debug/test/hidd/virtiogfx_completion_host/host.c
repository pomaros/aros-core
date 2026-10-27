/* Copyright (C) 2026 John KNIPPER <code@jkn.me>. APL-1.1. */
#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef uint16_t UWORD;
typedef uint32_t ULONG; typedef uint64_t UQUAD; typedef int BOOL; typedef void *APTR;
#define TRUE 1
#define FALSE 0
#define PA_SIGNAL 0
#define PA_IGNORE 2
#define TR_ADDREQUEST 1
#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2
struct Task { int unused; };
struct SignalSemaphore { int held; };
struct IORequest { int io_Command; };
struct timerequest { struct IORequest tr_node; struct { int tv_secs,tv_micro; } tr_time; };
struct MsgPort { struct Task *mp_SigTask; int mp_SigBit,mp_Flags; };
struct desc { uint64_t addr; uint32_t len; uint16_t flags,next; };
struct avail { uint16_t idx,ring[8]; };
struct used { uint16_t idx; struct { uint32_t id,len; } ring[8]; };
struct virtio_queue { volatile struct avail *q_Avail; volatile struct used *q_Used;
 volatile struct desc *q_Desc; volatile uint16_t *q_Notify; };
struct Device { struct virtio_queue *dev_VQ; }; typedef struct Device *device_t;
struct VirtioGPU {
 struct Device device; struct SignalSemaphore lock; void *message; uint64_t message_bus,fence;
 BOOL failed,ready,irq_reported,pending,repaint; UWORD pending_next;
 unsigned pending_bytes; uint32_t pending_type; struct Task *waiter;
 ULONG wait_mask,interrupts,irq_users; struct timerequest *timer; struct MsgPort timer_port;
};
static struct VirtioGPU g;
static struct avail av; static struct used us; static struct desc ds[2];
static struct virtio_queue queue; static uint16_t notify;
static unsigned char memory[512]; static struct Task task; static struct timerequest timer;
static int mode,bad,expired,allocated,drained,cleared,waits,submits,alloc_fail;
static unsigned last_rect[4];
enum { NORMAL, NOIRQ, EXPIRED_COMPLETE, TIMEOUT, SPURIOUS };
static void bug(const char *fmt,...) { (void)fmt; }
static void Disable(void) {} static void Enable(void) {}
static struct Task *FindTask(void *p) { (void)p;return &task; }
static BYTE AllocSignal(int bit) { (void)bit;if(alloc_fail){alloc_fail--;return -1;}assert(!allocated);allocated=1;drained=cleared=0;return 4; }
static ULONG SetSignal(ULONG a,ULONG b) { (void)a;(void)b;cleared=1;return 0; }
static void FreeSignal(int b) { assert(b==4 && allocated && drained && cleared && !g.waiter && g.timer_port.mp_Flags==PA_IGNORE);allocated=0; }
static void complete(void)
{
 uint32_t *r=(void *)(memory+64); unsigned slot=(uint16_t)(av.idx-1)%8;
 r[0]=bad==3?0x1200:g.pending_type;r[1]=bad==4?0:1;
 uint64_t fence=g.fence+(bad==5);memcpy(r+2,&fence,8);
 us.ring[slot].id=bad==1?1:0;us.ring[slot].len=g.pending_bytes+(bad==2);
 us.idx=av.idx;
}
static void SendIO(struct IORequest *r)
{
 assert(r==&timer.tr_node && av.idx==g.pending_next && notify==0 && g.pending);
 assert(ds[0].addr==g.message_bus && ds[1].addr==g.message_bus+64);
 submits++;expired=0;notify=99;
 memcpy(last_rect,memory+24,16);
 if(mode==NORMAL || mode==NOIRQ || mode==EXPIRED_COMPLETE)complete();
 if(mode==NORMAL)g.interrupts++;
 if(mode==TIMEOUT || mode==EXPIRED_COMPLETE)expired=1;
}
static struct IORequest *CheckIO(struct IORequest *r) { return expired?r:NULL; }
static void AbortIO(struct IORequest *r) { assert(r==&timer.tr_node);expired=1; }
static void WaitIO(struct IORequest *r) { assert(r==&timer.tr_node && !g.waiter);expired=1;drained=1;cleared=0; }
static void Wait(ULONG mask) { assert(mask==(1u<<4) && g.waiter);assert(++waits<4);if(mode==SPURIOUS)g.interrupts++;expired=1; }
static void ObtainSemaphore(struct SignalSemaphore *s) { assert(!s->held);s->held=1; }
static void ReleaseSemaphore(struct SignalSemaphore *s) { assert(s->held);s->held=0; }
#include "actual.c"
static void reset(void)
{
 memset(&g,0,sizeof(g));memset(memory,0,sizeof(memory));memset(&av,0,sizeof(av));memset(&us,0,sizeof(us));
 queue=(struct virtio_queue){&av,&us,ds,&notify};g.device.dev_VQ=&queue;g.message=memory;
 g.message_bus=0x1000;g.timer=&timer;g.ready=TRUE;notify=99;
 mode=NORMAL;bad=expired=allocated=drained=cleared=waits=submits=alloc_fail=0;
}
static int command(void) { gpu_request(&g,0x104);return gpu_command(&g,48,24,0x1100); }
int main(void)
{
 reset();assert(command() && !g.pending && !g.failed);
 reset();mode=NOIRQ;assert(command() && !g.failed && !g.interrupts);
 reset();mode=EXPIRED_COMPLETE;assert(command() && !g.failed);
 reset();mode=SPURIOUS;assert(!command() && g.pending && !g.failed);
 unsigned char retained[512];memcpy(retained,memory,sizeof(memory));int submitted=submits;
 assert(!VirtioGPUPresent(&g,10,20,30,40));assert(!memcmp(retained,memory,sizeof(memory)) && submits==submitted);
 complete();mode=NORMAL;assert(VirtioGPUPresent(&g,10,20,30,40));
 assert(last_rect[0]==0 && last_rect[1]==0 && last_rect[2]==1024 && last_rect[3]==768 && !g.repaint);
 reset();mode=TIMEOUT;assert(!command() && g.pending);complete();mode=NORMAL;alloc_fail=1;
 assert(!VirtioGPUPresent(&g,10,20,30,40) && g.repaint && !g.pending && !g.failed);
 assert(VirtioGPUPresent(&g,90,90,8,8));assert(last_rect[2]==1024 && last_rect[3]==768);
 reset();alloc_fail=1;assert(!VirtioGPUPresent(&g,1,1,10,10) && !g.failed && g.repaint);
 assert(VirtioGPUPresent(&g,80,80,8,8));assert(last_rect[0]==0 && last_rect[2]==1024 && last_rect[3]==768);
 for(int i=1;i<=5;i++){reset();bad=i;assert(!command() && g.failed && !g.pending);assert(!VirtioGPUPresent(&g,1,1,10,10));}
 reset();av.idx=us.idx=65535;assert(command() && av.idx==0 && us.idx==0);
 reset();mode=TIMEOUT;assert(!command());complete();us.idx--;assert(gpu_reap(&g)==0 && g.pending);
 puts("PASS actual GPU command/reap/present: noIRQ, timeout race, pending DMA, late full repaint, allocation damage, corrupt responses and index wrap");
}
