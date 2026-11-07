/* Test-only allocation-failure control for the context construction path.
 * Compiled in only when the build sets MESA3DGL_FAULT_INJECTION, which also
 * renames the module (mesa3dgl<build>fault) so it can never be mistaken for
 * the regular library. The failing point is selected at run time through the
 * global variable MESA3DGL_FAULT: screen, stmanager, shared or framebuffer. */
#ifndef MESA3DGL_FAULT_H
#define MESA3DGL_FAULT_H
#ifdef MESA3DGL_FAULT_INJECTION
#include <proto/dos.h>
#include <dos/var.h>
#include <string.h>
static inline BOOL mesa3dgl_fault(const char *point)
{
    char value[32];
    LONG length = GetVar("MESA3DGL_FAULT", value, sizeof(value), GVF_GLOBAL_ONLY | LV_VAR);
    return length > 0 && !strcmp(value, point);
}
#define MESA3DGL_FAULT(point) mesa3dgl_fault(point)
#else
#define MESA3DGL_FAULT(point) FALSE
#endif
#endif
