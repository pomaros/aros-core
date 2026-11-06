/* Driver cores and their allocations live in the client Mesa module. */
#include LC_LIBDEFS_FILE
#include "mesa_screen_factory.h"
#include "pipe/p_screen.h"
#ifdef MESA3DGL_HAVE_VIRGL
#include "virgl/virgl_public.h"
#include "virgl/virgl_winsys.h"
#endif
#include "util/format/u_format.h"

static APTR create_screen(struct Library *module, ULONG backend, APTR winsys)
{
    char *previous = __aros_getoffsettable();
    APTR screen = NULL;
    __aros_setoffsettable(module);
    switch (backend) {
#ifdef MESA3DGL_HAVE_VIRGL
    case GALLIUM_SCREEN_BACKEND_VIRGL:
        screen = virgl_create_screen(winsys, NULL); break;
#endif
    }
    __aros_setoffsettable(previous);
    return screen;
}
static void destroy_screen(struct Library *module, APTR screen)
{
    char *previous = __aros_getoffsettable();
    __aros_setoffsettable(module);
    ((struct pipe_screen *)screen)->destroy(screen);
    __aros_setoffsettable(previous);
}
static ULONG format_stride(struct Library *module, ULONG format, ULONG width)
{
    char *previous = __aros_getoffsettable();
    ULONG stride;
    __aros_setoffsettable(module);
    stride = util_format_get_stride(format, width);
    __aros_setoffsettable(previous);
    return stride;
}
static ULONG native_format(struct Library *module, ULONG backend, ULONG format)
{
    ULONG result = GALLIUM_SCREEN_FORMAT_INVALID;
#ifdef MESA3DGL_HAVE_VIRGL
    if (backend == GALLIUM_SCREEN_BACKEND_VIRGL && format < PIPE_FORMAT_COUNT) {
        char *previous = __aros_getoffsettable();
        __aros_setoffsettable(module);
        result = pipe_to_virgl_format(format);
        if (format != PIPE_FORMAT_NONE && result == 0)
            result = GALLIUM_SCREEN_FORMAT_INVALID;
        __aros_setoffsettable(previous);
    }
#endif
    return result;
}
void mesa_screen_factory_init(struct GalliumScreenFactory *factory)
{
    factory->version = GALLIUM_SCREEN_FACTORY_VERSION;
    factory->size = sizeof(*factory);
    factory->backends = 0;
#ifdef MESA3DGL_HAVE_VIRGL
    factory->backends |= GALLIUM_SCREEN_BACKEND_VIRGL;
#endif
    factory->module = (struct Library *)__aros_getoffsettable();
    factory->create = create_screen;
    factory->destroy = destroy_screen;
    factory->format = native_format;
    factory->stride = format_stride;
}
