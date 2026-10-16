/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Licensed under the AROS Public License (APL) Version 1.1.
 * Read-only firmware-tree inspection through public OpenFirmware vectors.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/openfirmware.h>
#include <string.h>

const char version[] = "$VER: DeviceTree 1.0 (1.10.2026)";
int __nocommandline;
APTR OpenFirmwareBase;
#define MAX_DEPTH 64
#define MAX_NODES 16384
#define MAX_VALUE 256
#define MAX_PATH 2048
static char path[MAX_PATH];
static ULONG nodes;
static LONG result;
static BOOL all_properties, hex_values;
static ULONG max_depth = MAX_DEPTH;

/* Never send control sequences from firmware to the console. */
static void escaped(const unsigned char *s, ULONG n)
{
    ULONG i;
    for (i = 0; i < n; ++i) {
        if (s[i] >= 32 && s[i] < 127 && s[i] != '"' && s[i] != '\\')
            Printf("%lc", (LONG)s[i]);
        else Printf("\\x%02lx", (ULONG)s[i]);
    }
}
static void text(const char *s) { escaped((const unsigned char *)s, strlen(s)); }
static BOOL selected(const char *s)
{
    const char *names[] = {"model", "compatible", "device_type", "status",
        "reg", "ranges", "interrupts", "interrupt-parent", "interrupt-controller",
        "#address-cells", "#size-cells", "#interrupt-cells", "stdout-path", NULL};
    unsigned i;
    for (i = 0; names[i]; ++i) if (!strcmp(s, names[i])) return TRUE;
    return FALSE;
}
static BOOL string_property(const char *name)
{
    const char *names[] = {"model", "compatible", "device_type", "status",
        "name", "stdout-path", "stdin-path", "interrupt-names", "clock-names",
        "reg-names", "method", "enable-method", "bootloader-version", NULL};
    unsigned i;
    for (i = 0; names[i]; ++i) if (!strcmp(name, names[i])) return TRUE;
    return FALSE;
}
static void value(APTR prop)
{
    const char *name = OF_GetPropName(prop);
    ULONG len = OF_GetPropLen(prop), n = len < MAX_VALUE ? len : MAX_VALUE, i;
    const unsigned char *p = OF_GetPropValue(prop);
    /* Diagnostic-authority plumbing is not available yet. Do not expose these. */
    if (!strcmp(name, "bootargs") || !strcmp(name, ".state")) {
        PutStr("<withheld: diagnostic authority required>\n"); return;
    }
    if (!len) { PutStr("<present>\n"); return; }
    if (!p) { PutStr("<invalid value>\n"); result = RETURN_ERROR; return; }
    if (!hex_values && string_property(name) && len <= MAX_VALUE && !p[len-1]) {
        ULONG start = 0;
        for (i = 0; i < len; ++i) if (!p[i]) {
            if (start) PutStr(", ");
            PutStr("\""); escaped(p+start, i-start); PutStr("\""); start = i+1;
        }
    } else if (!hex_values && !(len % 4)) {
        PutStr("<");
        for (i = 0; i < n; i += 4) {
            ULONG cell = ((ULONG)p[i]<<24) | ((ULONG)p[i+1]<<16) |
                         ((ULONG)p[i+2]<<8) | p[i+3];
            if (i) PutStr(" ");
            Printf("0x%08lx", cell);
        }
        PutStr("> (BE32 cells)");
    } else {
        PutStr("[");
        for (i = 0; i < n; ++i) Printf(i ? " %02lx" : "%02lx", (ULONG)p[i]);
        PutStr("] (bytes)");
    }
    if (n < len) Printf(" ... (first %lu of %lu bytes)", n, len);
    PutStr("\n");
}
static BOOL interrupted(void)
{
    if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) {
        result = RETURN_WARN; return TRUE;
    }
    return FALSE;
}
static void walk(APTR node, ULONG depth)
{
    APTR prop = NULL, child = NULL;
    ULONG old_length = strlen(path), count = 0;
    if (interrupted() || result >= RETURN_ERROR) return;
    if (++nodes > MAX_NODES) {
        PutStr("DeviceTree: node limit reached\n"); result = RETURN_ERROR; return;
    }
    text(path); PutStr("\n");
    while ((prop = OF_GetProperty(node, prop))) {
        const char *name = OF_GetPropName(prop);
        if (interrupted()) return;
        if (++count > 16384) { PutStr("DeviceTree: property limit reached\n"); result = RETURN_ERROR; return; }
        if (all_properties || selected(name)) {
            PutStr("  "); text(name); Printf(" (%lu bytes) = ", (ULONG)OF_GetPropLen(prop));
            value(prop);
        }
    }
    if (depth == max_depth) {
        if (OF_GetChild(node, NULL)) PutStr("  ... (depth limit)\n");
        return;
    }
    while ((child = OF_GetChild(node, child))) {
        const char *name = OF_GetNodeName(child);
        ULONG length = name ? strlen(name) : 0;
        BOOL slash = old_length > 1;
        if (!length || length + old_length + slash >= sizeof(path)) {
            PutStr("DeviceTree: invalid or too long node path\n"); result = RETURN_ERROR; return;
        }
        if (slash) path[old_length] = '/';
        memcpy(path+old_length+slash, name, length+1);
        walk(child, depth+1);
        path[old_length] = 0;
        if (result != RETURN_OK) return;
    }
}
int main(void)
{
    IPTR args[5] = {0};
    struct RDArgs *rd = ReadArgs("PATH,PROPERTIES/S,PROPERTY/K,HEX/S,DEPTH/N/K", args, NULL);
    APTR node;
    const char *requested;
    if (!rd) { PrintFault(IoErr(), "DeviceTree"); return RETURN_FAIL; }
    requested = args[0] ? (const char *)args[0] : "/";
    if (requested[0] != '/' || strlen(requested) >= sizeof(path) ||
        (args[4] && (*(LONG *)args[4] < 0 || *(LONG *)args[4] > MAX_DEPTH))) {
        PutStr("DeviceTree: use an absolute path and DEPTH from 0 to 64\n");
        result = RETURN_ERROR; goto out;
    }
    strcpy(path, requested);
    while (strlen(path) > 1 && path[strlen(path)-1] == '/') path[strlen(path)-1] = 0;
    all_properties = args[1] != 0; hex_values = args[3] != 0;
    if (args[4]) max_depth = *(LONG *)args[4];
    OpenFirmwareBase = OpenResource("openfirmware.resource");
    if (!OpenFirmwareBase) {
        PutStr("DeviceTree: no OpenFirmware tree available on this system\n");
        result = RETURN_FAIL; goto out;
    }
    /* The command and resource ship with the same public OF API. A resource
     * loaded from disk has no ROM resident tag; OpenResource is authoritative. */
    node = OF_OpenKey(path);
    if (!node) { PutStr("DeviceTree: node not found: "); text(path); PutStr("\n"); result = RETURN_ERROR; goto out; }
    PutStr("DeviceTree - retained firmware description (not live driver state)\n");
    if (args[2]) {
        APTR prop = OF_FindProperty(node, (char *)args[2]);
        if (!prop) { PutStr("DeviceTree: property not found\n"); result = RETURN_ERROR; }
        else { text(path); PutStr(" : "); text((const char *)args[2]); PutStr(" = "); value(prop); }
    } else {
        walk(node, 0);
        Printf("%lu node(s) shown\n", nodes);
    }
    OF_CloseKey(node);
out:
    FreeArgs(rd);
    if (Flush(Output()) == DOSFALSE && result == RETURN_OK) result = RETURN_ERROR;
    return result;
}
