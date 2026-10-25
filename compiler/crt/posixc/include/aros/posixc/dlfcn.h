#ifndef _POSIXC_DLFCN_H_
#define _POSIXC_DLFCN_H_

/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: POSIX.1-2008 header file dlfcn.h

    dlopen() loads modules linked with dlmodule-link (see <aros/dlmodule.h>):
    LoadSeg()-loadable objects that carry their own export table. Symbols
    are bound when the module is loaded, so RTLD_LAZY and RTLD_NOW behave
    the same. A module never binds to another module's symbols: RTLD_LOCAL
    is what every module gets, and dlopen() refuses RTLD_GLOBAL. The values
    are Linux's.
*/

#include <aros/system.h>

#define RTLD_LAZY       0x0001
#define RTLD_NOW        0x0002
#define RTLD_NOLOAD     0x0004
#define RTLD_GLOBAL     0x0100
#define RTLD_LOCAL      0x0000
#define RTLD_NODELETE   0x1000

#define RTLD_DEFAULT    ((void *)0)
#define RTLD_NEXT       ((void *)-1L)

/* dladdr(), a GNU and BSD extension */
typedef struct
{
    const char *dli_fname;      /* the module's full DOS name */
    void       *dli_fbase;      /* the start of its first hunk */
    const char *dli_sname;      /* the nearest export at or below, or NULL */
    void       *dli_saddr;      /* that export's address, or NULL */
} Dl_info;

__BEGIN_DECLS

int dlclose(void *);
char *dlerror(void);
void *dlopen(const char *, int);
void *dlsym(void *__restrict, const char *__restrict);
int dladdr(const void *, Dl_info *);

__END_DECLS

#endif /* !_POSIXC_DLFCN_H_ */
