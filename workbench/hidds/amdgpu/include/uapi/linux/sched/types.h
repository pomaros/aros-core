/*
    Copyright 2026, The AROS Development Team. All rights reserved.
*/
#ifndef _UAPI_LINUX_SCHED_TYPES_H_
#define _UAPI_LINUX_SCHED_TYPES_H_

/* <sys/types.h> defines the same structure for the pthread types; one
   definition serves both. */
#ifndef __AROS_SCHED_PARAM_DEFINED
#define __AROS_SCHED_PARAM_DEFINED
struct sched_param {
    int sched_priority;
};
#endif

#endif
