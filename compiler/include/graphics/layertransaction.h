#ifndef GRAPHICS_LAYERTRANSACTION_H
#define GRAPHICS_LAYERTRANSACTION_H
/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL) Version 1.1. */
#include <exec/types.h>
#include <graphics/gfx.h>

struct Layer;
struct Layer_Info;
struct Hook;
struct Region;
struct LayerGeometryBatch;
struct HIDD_PixelMap;
struct HIDD_CursorState;
/* Lossless transport for 8-bit and 10-bit native components. Significant
 * bits are high-aligned in each UWORD; destination padding is backend-owned. */
struct PreparedPixel { UWORD red, green, blue, alpha; };

/* Bounds are absolute in this layer's own Layer_Info coordinate space.
 * The replacement shape is layer-relative and borrowed until batch release;
 * NULL means rectangular. No client shape hook runs in a batch. A caller
 * resolving an internal hook beforehand must explicitly set RESOLVED_SHAPE. */
#define LGT_REPLACE_SHAPE   (1UL << 0)
#define LGT_RESOLVED_SHAPE  (1UL << 1)
struct LayerGeometryRequest
{
    struct Layer *layer;
    struct Rectangle bounds;
    struct Region *shape;
    ULONG flags;
    struct Hook *resolved_hook;
};

/* The exact check behind a refused batch call. The caller owns the record
 * and passes it to PrepareLayerGeometryBatch; the batch borrows it until
 * release and every refusing batch call (prepare, cursor, read, write, seal)
 * overwrites it once. lgr_Layer is an identity for the caller to match
 * against its own layers, never to dereference after release. lgr_Class is
 * the refusing bitmap class or driver name, owned and always NUL. */
struct LayerGeometryRefusal
{
    ULONG lgr_Check;   /* LGR_* */
    ULONG lgr_Flags;   /* LGRF_* about lgr_Layer */
    ULONG lgr_Pixels;  /* LGR_PIXELS_*: which pixel lease, for LGR_PIXELS */
    ULONG lgr_Lease;   /* LGR_LEASE_*: which lease step, for LGR_PIXELS */
    ULONG lgr_Driver;  /* HIDD_PIXELS_REFUSE_* reported by the driver */
    struct Layer *lgr_Layer;
    LONG lgr_Value[4];
    UBYTE lgr_Class[40];
};
#define LGRF_ROOT    (1UL << 0) /* lgr_Layer is the root layer of its stack */
#define LGRF_PRIVATE (1UL << 1) /* lgr_Layer is in a private composed stack */
/* Zero means that no batch call recorded a cause. Values per check: */
#define LGR_NONE              0
#define LGR_ARGUMENTS         1  /* [0] request count: no root, no array or too many */
#define LGR_ROOT_COMPOSED     2  /* the root Layer_Info is a private composed stack */
#define LGR_NO_MEMORY         3  /* [0] bytes (0 for a region), [1] LGR_MEM_* */
#define LGR_REQUEST_LAYER     4  /* [0] request index without a layer or Layer_Info */
#define LGR_FOREIGN_STACK     5  /* [0] request index outside the root and its composed stacks */
#define LGR_LAYER_COUNT       6  /* [0] layers counted before overflow */
#define LGR_UPDATING          7  /* the layer is between BeginUpdate and EndUpdate */
#define LGR_DUPLICATE_REQUEST 8  /* [0] request index naming an already requested layer */
#define LGR_REQUEST_FLAGS     9  /* [0] request index, [1] its flags */
#define LGR_HOOK_MISMATCH     10 /* [0] request index: resolved hook is not the layer's hook */
#define LGR_BOUNDS            11 /* [0] width, [1] height outside 1..32767 */
#define LGR_SHAPE_HOOK        12 /* unresolved shape hook: [0] width, [1] height, [2] 1 if shape replaced */
#define LGR_STALE_REQUEST     13 /* [0] request index: layer is in no pinned stack */
#define LGR_PARENT            14 /* parent outside the batch, or a parent cycle */
#define LGR_FRONT             15 /* a front layer is outside the batch */
#define LGR_STACK_BITMAP      16 /* [0] stack index (-1: root outside the batch): no root layer, RastPort, bitmap or pen */
#define LGR_FOREIGN_BITMAP    17 /* the layer's RastPort bitmap is not its stack's bitmap */
#define LGR_PIXELS            18 /* a pixel lease refused: see lgr_Pixels, lgr_Lease, lgr_Driver */
#define LGR_SUPER_BITMAP      19 /* a SUPER layer has no SuperBitMap */
#define LGR_SUPER_ALIAS       20 /* the SuperBitMap is a stack bitmap */
#define LGR_STATE             21 /* [0] batch state: called in the wrong state */
#define LGR_NOT_MEMBER        22 /* the named layer is not in the batch */
#define LGR_ROOT_CANVAS       23 /* a stack root layer has no client canvas */
#define LGR_SOURCE_SIZE       24 /* [0] stride, [1] rows given; [2] stride, [3] rows needed */
#define LGR_RECTANGLE         25 /* [0] MinX, [1] MinY, [2] MaxX, [3] MaxY outside the layer */
#define LGR_SOURCE_ALIAS      26 /* the source bitmap is a stack bitmap or a SuperBitMap */
#define LGR_SUPER_TARGET      27 /* a SUPER layer cannot take a bounded native paint */
#define LGR_CURSOR_OWNER      28 /* the cursor bitmap is not the physical root bitmap */
#define LGR_CURSOR_LEASE      29 /* the root lease cannot take a proposed cursor */
#define LGR_SCREEN_OFFSET     30 /* [0] x, [1] y: a visible rectangle is left of or above the bitmap */
/* LGR_NO_MEMORY [1] */
#define LGR_MEM_BATCH   1
#define LGR_MEM_STACKS  2
#define LGR_MEM_LAYERS  3
#define LGR_MEM_REGION  4
#define LGR_MEM_CONTENT 5 /* [2] width, [3] rows of the layer's detached canvas */
#define LGR_MEM_CLIP    6
#define LGR_MEM_BACKING 7 /* [2] width, [3] height of a hidden smart-refresh backing */
#define LGR_MEM_ROW     8
/* LGR_PIXELS lease */
#define LGR_PIXELS_STACK    1 /* the stack's bitmap: the display, or a private canvas (LGRF_PRIVATE) */
#define LGR_PIXELS_SUPER    2 /* a layer's SuperBitMap */
#define LGR_PIXELS_OBSCURED 3 /* a live hidden smart-refresh backing, read */
#define LGR_PIXELS_BACKING  4 /* a new hidden smart-refresh backing, written */
#define LGR_PIXELS_SOURCE   5 /* a caller's detached staging bitmap */
/* LGR_PIXELS step */
#define LGR_LEASE_NOT_HIDD  1 /* not a HIDD bitmap */
#define LGR_LEASE_NO_OBJECT 2 /* HIDD bitmap without an object */
#define LGR_LEASE_OOP       3 /* oop.library could not be opened */
#define LGR_LEASE_DRIVER    4 /* moHidd_BitMap_PreparePixels refused: lgr_Driver, [0] [1] its values */
#define LGR_LEASE_MAP       5 /* invalid map: [0] width, [1] height, [2] stride, [3] size */
#define LGR_LEASE_MEMORY    6 /* [0] bytes of a lease copy */
#define LGR_LEASE_RANGE     7 /* [0] x, [1] y, [2] width, [3] height outside the mapping */
#define LGR_LEASE_SEALED    8 /* staging is read-only or already sealed */
#define LGR_LEASE_CURSOR    9 /* moHidd_BitMap_RenderPreparedCursor refused: lgr_Driver, [0] [1] */

/* WriteLayerGeometryBitmap is a prepare-only native client paint operation.
 * bounds is layer-relative and also selects that region in a caller-owned
 * detached bitmap. Other canvas pixels and damage remain intact. Caller pins
 * the bitmap and excludes drawing throughout the call. Scene/SuperBitMap
 * aliases and SuperBitMap destinations are refused. Native 8/10-bit channels
 * retain all bits. A failed write requires releasing the uncommitted batch. */

/* Prepare pins the physical root and the complete private stacks referenced
 * by requests. Commit/rollback keep all public Layer/Layer_Info/RastPort
 * identities. The batch retains its locks and both generations until release.
 * An active BeginUpdate or unresolved external shape hook is refused before
 * changing any live state. Finish is irreversible postcommit drawing: it
 * releases direct mappings and invokes deferred custom backfills, so must
 * precede notification publication. Rollback is unavailable after Finish.
 * This interface is not a general hook transaction. */
#endif
