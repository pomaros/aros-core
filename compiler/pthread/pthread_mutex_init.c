/*
  Copyright (C) 2014 Szilard Biro
  Copyright (C) 2018 Harry Sintonen
  Copyright (C) 2019 Stefan "Bebbo" Franke - AmigaOS 3 port

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* Altered (2026 John KNIPPER): publish static initialization through Exec. */
#include "pthread_intern.h"
#include "debug.h"

/* Altered for AROS: Exec supplies one initialization domain across linklibs. */
int _pthread_mutex_ensure_init(pthread_mutex_t *mutex, BOOL nonblocking)
{
    if (mutex == NULL)
        return EINVAL;
    if (__atomic_load_n(&mutex->initialized, __ATOMIC_ACQUIRE) == 1)
        return 0;
    if (!InitSemaphoreOnce(&mutex->semaphore, &mutex->initialized,
                           nonblocking ? SSINITF_TRY : 0))
        return nonblocking ? EBUSY : EINVAL;
    return 0;
}

int _pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr, BOOL staticinit)
{
    DB2(bug("%s(%p, %p)\n", __FUNCTION__, mutex, attr));

    if (mutex == NULL)
        return EINVAL;

    if (attr)
        mutex->kind = attr->kind;
    else if (!staticinit)
        mutex->kind = PTHREAD_MUTEX_DEFAULT;
    InitSemaphore(&mutex->semaphore);
    mutex->incond = 0;
    __atomic_store_n(&mutex->initialized, 1, __ATOMIC_RELEASE);

    return 0;
}

/****************************************************************************
    NAME */
        int pthread_mutex_init(

/*  SYNOPSIS */
        pthread_mutex_t *mutex,
        const pthread_mutexattr_t *attr)

/*  FUNCTION
        Initialize a mutex's embedded Exec semaphore, copy its kind from attr
        (or use PTHREAD_MUTEX_DEFAULT), and clear its condition-wait count.

    INPUTS
        mutex - Non-NULL writable mutex storage that is not already in use.
        attr - Initialized mutex attributes, or NULL for the default kind.

    RESULT
        0 on success; EINVAL if mutex is NULL. Errors are returned directly.

    NOTES
        Include <pthread.h>; link with pthread. The caller owns the storage and
        must retain it until the unlocked, unused mutex has been destroyed.
        No heap storage is allocated. Initialize before publishing the mutex
        to other tasks. Attributes are copied and may then be destroyed.
        Reinitializing a live mutex is invalid; no busy check is performed.

    BUGS
        The supplied kind is not validated here. Process-sharing attributes are
        ignored. Concurrent explicit initialization or destruction remains
        invalid. Static first use is serialized separately.

    SEE ALSO
        pthread_mutex_lock(),
        pthread_mutex_unlock(),
        pthread_mutex_destroy(),
        pthread_mutexattr_settype()

*****************************************************************************/
{
    D(bug("%s(%p, %p)\n", __FUNCTION__, mutex, attr));

    return _pthread_mutex_init(mutex, attr, FALSE);
}
