/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#ifndef DOS_TASK_PATH_RESOLVER_H
#define DOS_TASK_PATH_RESOLVER_H
#include <dos/context.h>
struct DosPathResolver *__dos_task_path_resolver(void);
SIPTR __dos_path_operation(LONG operation, const SIPTR *args, BOOL *handled,
    struct DosLibrary *dos);
/* Dispatch before any native Process-field access. No resolver means the
 * original DOS implementation runs unchanged. Arguments live until return. */
#define DOS_PATH_DISPATCH(operation, result_type, ...) do { \
    const SIPTR __path_args[] = {__VA_ARGS__}; \
    BOOL __path_handled; \
    SIPTR __path_result = __dos_path_operation((operation), __path_args, \
        &__path_handled, DOSBase); \
    if (__path_handled) return (result_type)__path_result; \
} while (0)
#endif
