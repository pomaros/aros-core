/* CRT-free native metadata and memory operations. UtilityBase is borrowed
 * from the native module root, which is pinned through winsys destruction. */
#ifndef AROS_VIRGL_NATIVE_H
#define AROS_VIRGL_NATIVE_H
#include <stddef.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/utility.h>
static inline APTR avgl_native_alloc(size_t bytes)
{
    if (!bytes || bytes > (size_t)(~(ULONG)0)) return NULL;
    return AllocVec((ULONG)bytes, MEMF_ANY | MEMF_CLEAR);
}
static inline void avgl_native_clear(struct Library *UtilityBase, APTR ptr, ULONG bytes)
{
    SetMem(ptr, 0, bytes);
}
static inline APTR avgl_native_grow(APTR old, size_t old_bytes, size_t new_bytes)
{
    if (new_bytes < old_bytes) return NULL;
    APTR next = avgl_native_alloc(new_bytes);
    if (!next) return NULL;
    if (old && old_bytes) CopyMem(old, next, old_bytes);
    FreeVec(old);
    return next;
}
#endif
