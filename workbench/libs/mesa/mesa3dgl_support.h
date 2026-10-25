/*
    Copyright 2009-2019, The AROS Development Team. All rights reserved.
*/

#ifndef MESA3DGL_SUPPORT_H
#define MESA3DGL_SUPPORT_H

#include "mesa3dgl_types.h"

VOID MESA3DGLSelectRastPort(struct mesa3dgl_drawable * d, struct TagItem * tagList);
BOOL MESA3DGLStandardInit(struct mesa3dgl_drawable * d, struct TagItem *tagList);
VOID MESA3DGLRecalculateBufferWidthHeight(struct mesa3dgl_drawable * d);
VOID MESA3DGLFreeContext(struct mesa3dgl_context * ctx);
VOID MESA3DGLFreeTarget(struct mesa3dgl_drawable * d);
BOOL MESA3DGLPrepareTarget(struct mesa3dgl_drawable * staged, struct TagItem * tagList);
VOID MESA3DGLCommitTarget(struct mesa3dgl_drawable * d, struct mesa3dgl_drawable * staged);
BOOL MESA3DGLVisualAccepts(const struct st_visual * stvis, ULONG bpp);

#endif /* MESA3DGL_SUPPORT_H */
