/* Native HIDD / client Mesa screen ownership contract. */
#ifndef GALLIUM_SCREENFACTORY_H
#define GALLIUM_SCREENFACTORY_H
#include <exec/types.h>
#include <exec/libraries.h>
#define GALLIUM_SCREEN_FACTORY_VERSION 2
#define GALLIUM_SCREEN_BACKEND_SOFTPIPE 1U
#define GALLIUM_SCREEN_BACKEND_VIRGL 2U
#define GALLIUM_SCREEN_FORMAT_INVALID ((ULONG)~0U)
/* Borrowed input record, copied by the pipe. The pipe opens module itself
 * before invoking callbacks and closes it after destroying the screen.
 * Callbacks select and restore their module table. The screen owns and joins
 * its workers; no callback or worker may outlive screen destruction. */
struct GalliumScreenFactory {
    ULONG version;
    ULONG size;
    ULONG backends;
    struct Library *module;
    APTR (*create)(struct Library *module, ULONG backend, APTR winsys);
    void (*destroy)(struct Library *module, APTR screen);
    ULONG (*format)(struct Library *module, ULONG backend, ULONG format);
    ULONG (*stride)(struct Library *module, ULONG format, ULONG width);
};
#endif
