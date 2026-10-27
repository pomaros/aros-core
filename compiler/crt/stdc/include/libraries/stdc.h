#ifndef _LIBRARIES_STDC_H
#define _LIBRARIES_STDC_H

/*
    Copyright © 2012-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    $Id$

    Public part of StdC libbase.
    Rebuild the complete runtime and its consumers when changing this ABI.
*/

#include <exec/libraries.h>

#include <setjmp.h>

struct StdCBase
{
    struct Library lib;

    /* ctype.h */
    const unsigned short int * __ctype_b;
    const unsigned char * __ctype_toupper;
    const unsigned char * __ctype_tolower;

    /* errno.h */
    /* Native task-local error context, shared by all same-task openers. */
    LONG _errno_slot;
    APTR _errno_task_resource;

    /* math.h */
    int _signgam;

    /* stdlib.h */
    int mb_cur_max;

    /* signal.h */
    unsigned char sigrunning, sigpending;

    /* time.h - constant tables shared with the re-entrant (_r) variants in
       posixc.library (initialised by stdc.library, see __stdc_time.c) */
    const char         *__time_monthdays;    /* [11] days per month        */
    const char        (*__time_wday_name)[3];/* [7][3] weekday name table   */
    const char        (*__time_mon_name)[3]; /* [12][3] month name table    */
};

__BEGIN_DECLS

struct StdCBase *__aros_getbase_StdCBase(void);

/* Some internal support functions */
void __stdc_program_startup(jmp_buf exitjmp, int *errorptr);
void __stdc_program_end(void);
int *__stdc_set_errorptr(int *errorptr);
int *__stdc_get_errorptr(void);
void __stdc_set_exitjmp(jmp_buf exitjmp, jmp_buf previousjmp);
void __stdc_jmp2exit(int normal, int returncode) __noreturn;
void *__stdc_set_fpuprivate(void *fpuprivate);
void *__stdc_get_fpuprivate(void);
int __stdc_mb_cur_max(void);

__END_DECLS

#endif /* _LIBRARIES_STDC_H */
