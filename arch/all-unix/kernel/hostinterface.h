#ifndef _HOSTINTERFACE_H
#define _HOSTINTERFACE_H

#include <stdint.h>

#define HOSTINTERFACE_VERSION 6

struct HostInterface
{
    char *System;
    unsigned int Version;

    void *(*hostlib_Open)(const char *, char **);
    int   (*hostlib_Close)(void *, char **);
    void *(*hostlib_GetPointer)(void *, const char *, char **);
    int   (*KPutC)(int chr);
    int   (*host_GetTime)(int, uint64_t *, uint64_t *);
    void  (*host_Lock)(void);
    void  (*host_Unlock)(void);
    struct MinList **ModListPtr;
    void  (*jit_write_protect)(int enabled);  /* W^X toggle for MAP_JIT (NULL if not needed) */


};

#endif /* !_HOSTINTERFACE_H */
