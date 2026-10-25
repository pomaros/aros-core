/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.
*/
#include <aros/libcall.h>
#include <proto/layers.h>
#include "layers_intern.h"

/* Associate an empty NewLayerInfo stack with a borrowed physical screen root.
 * A screen cannot close until its windows and their private stacks are gone.
 * The root is an embedded ordinary stack initialized with InitLayers, never a
 * NewLayerInfo stack. It therefore cannot subsequently acquire an association.
 * The caller exclusively owns the unpublished stack. No chain, reassignment,
 * reserve-field alias or public structure extension is permitted. */
AROS_LH2(BOOL, SetLayerInfoCompositionRoot,
    AROS_LHA(struct Layer_Info *, li, A0),
    AROS_LHA(struct Layer_Info *, root, A1),
    struct LayersBase *, LayersBase, 47, Layers)
{
    AROS_LIBFUNC_INIT
    struct LayerInfo_extra *extra, *rootextra;
    BOOL success = FALSE;
    if (!li || !root || li == root || (root->Flags & NEWLAYERINFO_CALLED)) return FALSE;
    LockLayerInfo(li);
    extra = li->LayerInfo_extra;
    rootextra = root->LayerInfo_extra;
    if ((li->Flags & NEWLAYERINFO_CALLED) && !li->top_layer && !li->check_lp &&
        extra && !extra->lie_CompositionRoot &&
        (!rootextra || !rootextra->lie_CompositionRoot))
    {
        extra->lie_CompositionRoot = root;
        success = TRUE;
    }
    UnlockLayerInfo(li);
    return success;
    AROS_LIBFUNC_EXIT
}
