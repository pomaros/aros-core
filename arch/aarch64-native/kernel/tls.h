#ifndef ASM_TLS_H
#define ASM_TLS_H

typedef struct tls
{
    struct ExecBase     *SysBase;
    void *              *KernelBase;    /* Base of kernel.resource */
    struct Task         *ThisTask;      /* Currently running task on this core */
    ULONG               ScheduleFlags;
    BYTE                IDNestCnt;
    BYTE                TDNestCnt;
    UWORD               Quantum;
    UWORD               Elapsed;
    ULONG               CPUNumber;      /* Logical id, set by the boot CPU;
                                         * MPIDR_EL1 is unreliable on Pi 3 */
} tls_t;

#define TLSSF_Quantum   (1 << 0)
#define TLSSF_Switch    (1 << 1)
#define TLSSF_Dispatch  (1 << 2)

#define TLS_OFFSET(name) ((char *)&(((tls_t *)0)->name)-(char *)0)

/* Native default; EL0 platforms supply their own CPU-anchor accessor. */
#ifndef TLS_PTR_GET

#define TLS_PTR_GET() \
    ({ \
        tls_t *__tls; \
        asm volatile("mrs %0, tpidr_el1":"=r"(__tls)); \
        __tls;  \
    })

#endif /* TLS_PTR_GET */

#define TLS_GET(name) \
    ({ \
        tls_t *__tls = TLS_PTR_GET(); \
        typeof(__tls -> name) __ret = (__tls -> name); \
        __ret;  \
    })

#define TLS_SET(name, val) \
    do { \
        tls_t *__tls = TLS_PTR_GET(); \
        (__tls -> name) = val; \
    } while(0);

#endif
