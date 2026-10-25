/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.
*/
#include <aros/libcall.h>
#include "layers_intern.h"

/* Internal composition coordination. An ordinary stack is its own root.
 * The association is established before layer creation and never changes,
 * so resolving it requires no additional lock in the drawing lock path. */
AROS_LH1(struct Layer_Info *, GetLayerInfoCompositionRoot,
    AROS_LHA(struct Layer_Info *, li, A0),
    struct LayersBase *, LayersBase, 46, Layers)
{
    AROS_LIBFUNC_INIT
    struct LayerInfo_extra *extra;
    if (!li) return NULL;
    extra = li->LayerInfo_extra;
    return extra && extra->lie_CompositionRoot ? extra->lie_CompositionRoot : li;
    AROS_LIBFUNC_EXIT
}
