/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#ifndef DOS_CONTEXT_H
#define DOS_CONTEXT_H
#include <exec/types.h>
struct DosLibrary;
/* Runs synchronously in a temporary real Process with owned cwd/progdir
 * copies. Use the supplied resident DOS base; return normally. Do not call
 * per-Task CRT services without initializing a runtime in this Process, change
 * its directories, or retain its Process/locks/data beyond the call. The caller
 * keeps its callback code and request data alive until RunWithContext returns.
 * No cancellation or forced-caller-removal guarantee is provided. */
typedef SIPTR (*DosContextCallback)(APTR data, struct DosLibrary *dos);
/* DupAssignLock231 (DOS 50.87) captures the primary lock of a bound assign.
 * The result is caller-owned; use its explicit handler and secondary result.
 * Device/volume/deferred/nonbinding kinds return no lock and zero error so
 * resolution can occur outside the namespace guard. No iteration, cancellation
 * or forced-removal guarantee is implied. */
struct DevProc;
#define DOS_PATH_RESOLVER_VERSION 2
enum {
    DOS_PATH_GET_DEVICE = 1, DOS_PATH_LOCK, DOS_PATH_OPEN, DOS_PATH_CREATE_DIR,
    DOS_PATH_DELETE, DOS_PATH_RENAME, DOS_PATH_MAKE_LINK, DOS_PATH_READ_LINK,
    DOS_PATH_SET_PROTECTION, DOS_PATH_SET_DATE, DOS_PATH_SET_COMMENT,
    DOS_PATH_SET_OWNER, DOS_PATH_REPLACE_OBJECT
};
/* Each Task registers a distinct caller-owned instance. Data may refer to a
 * shared, independently retained path context. error and active are private
 * to the registered Task; initialize them to zero before registration. */
struct DosPathResolver {
    ULONG version;
    /* Pure same-Task policy: FALSE preserves the original native operation.
     * DOS permits declining only for a real Process, never an ordinary Task.
     * No DOS calls, registration changes or blocking in this callback. */
    BOOL (*accept)(APTR data);
    SIPTR (*invoke)(APTR data, LONG operation, const SIPTR *args,
        SIPTR *error, struct DosLibrary *dos);
    APTR data;
    SIPTR error;
    BOOL active;
};
#endif
