/*
    Copyright (C) 2014-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License, version 1.1.
*/


#ifndef GLA_H
#define GLA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <intuition/intuition.h>
#include <GL/gl.h>

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

enum GLAError
{
   GLA_OUT_OF_MEM = 1,
   GLA_RASTPORT_TAG_MISSING,
   GLA_SCREEN_TAG_MISSING,
   GLA_WINDOW_TAG_MISSING
};

/*
 * This is the GLA context. This is an opaque pointer.
 * All information is hidden away from user.
 */


typedef APTR GLAContext;
/* What a context draws to apart from its own target (glACreateDrawable). */
typedef APTR GLADrawable;

/*
 * GLA Attribute tag ID's.  These are used in the ti_Tag field of
 * TagItem arrays passed to glACreateContext()
 */
#define GLA_Dummy               (TAG_USER + 32)

/*
 * Offsets from rastport layer dimensions. 
 * Typically GLA_Left = window->BorderLeft, GLA_Top = window->BorderTop
 * Defaults: if rendering to non-GZZ window BorderLeft, BorderTop, in all other
 *           cases 0,0
 */
#define GLA_Left                (GLA_Dummy + 0x0002)
#define GLA_Right               (GLA_Dummy + 0x0003)
#define GLA_Top                 (GLA_Dummy + 0x0004)
#define GLA_Bottom              (GLA_Dummy + 0x0005)

/*
 * Size in pixel of requested drawing area. Used to calculate GLA_Right, GLA_Bottom
 * if they are not passed. No defaults. Size of render area is calculated using:
 * rastport layer width - left - right, rastport layer height - top - bottom
 */
#define GLA_Width               (GLA_Dummy + 0x0006)
#define GLA_Height              (GLA_Dummy + 0x0007)

/*
 * Target to which rendering will be done. Without any of the three the context
 * renders offscreen, into a bitmap of GLA_Width by GLA_Height (1 by 1 by
 * default) it allocates as a friend of the default public screen's.
 */
#define GLA_Screen              (GLA_Dummy + 0x0011)
#define GLA_Window              (GLA_Dummy + 0x0012)
#define GLA_RastPort            (GLA_Dummy + 0x0013)

/*
 * Following boolean flags are ignored and always considered GL_TRUE
 */
#define GLA_DoubleBuf           (GLA_Dummy + 0x0030)
#define GLA_RGBMode             (GLA_Dummy + 0x0031)
#define GLA_AlphaFlag           (GLA_Dummy + 0x0032)

/* 
 * GLA_NoDepth:    don't allocate ZBuffer if GL_TRUE, default is GL_FALSE
 * GLA_NoStencil:  don't allocate StencilBuffer if GL_TRUE, default is GL_FALSE
 * GLA_NoAccum:    don't allocate AccumulationBuffer if GL_TRUE, default is GL_FALSE
 */
#define GLA_NoDepth             (GLA_Dummy + 0x0039)
#define GLA_NoStencil           (GLA_Dummy + 0x003a)
#define GLA_NoAccum             (GLA_Dummy + 0x003b)

/*
 * GLA_ContextProfile:      GLA_PROFILE_COMPAT (the default), GLA_PROFILE_CORE,
 *                          GLA_PROFILE_ES1, or GLA_PROFILE_ES2.
 *                          A core profile context can offer a newer GL than a
 *                          compatibility one (Mesa only raises compatibility
 *                          contexts as far as the driver allows).
 * GLA_ContextMajorVersion,
 * GLA_ContextMinorVersion: the lowest GL version the context must support;
 *                          creation fails if it cannot. The context gets the
 *                          highest version available. Core needs 3.2 at least.
 *                          ES1 accepts 1.0 or 1.1; ES2 accepts 2.0 or 3.0-3.2.
 *                          Their defaults are 1.0 and 2.0 respectively. These
 *                          select real GLES APIs, never desktop GL emulation.
 *                          Unsupported profiles or versions fail creation.
 */
#define GLA_ContextProfile      (GLA_Dummy + 0x0040)
#define GLA_ContextMajorVersion (GLA_Dummy + 0x0041)
#define GLA_ContextMinorVersion (GLA_Dummy + 0x0042)

/*
 * GLA_ShareContext: a context whose textures, buffers, shaders and programs
 *                   the new context shares; it then uses that context's pipe
 *                   screen.
 */
#define GLA_ShareContext        (GLA_Dummy + 0x0043)

/*
 * GLA_ShareScreen:  a context whose pipe screen the new context uses without
 *                   sharing its objects, so that drawables made for one
 *                   (glACreateDrawable) can be bound to the other.
 */
#define GLA_ShareScreen         (GLA_Dummy + 0x0044)

#define GLA_PROFILE_COMPAT      0
#define GLA_PROFILE_CORE        1
#define GLA_PROFILE_ES1         2
#define GLA_PROFILE_ES2         3

/*
 * GLA API calls
 */

typedef void (*GLAProc)();

GLAPI GLAContext            GLAPIENTRY glACreateContextTags(Tag Tag1, ...);
GLAPI GLAContext            GLAPIENTRY glACreateContext(struct TagItem *tagList);
GLAPI void                  GLAPIENTRY glADestroyContext(GLAContext ctx);
GLAPI void                  GLAPIENTRY glAMakeCurrent(GLAContext ctx);
GLAPI void                  GLAPIENTRY glASwapBuffers(GLAContext ctx);
GLAPI GLAProc               GLAPIENTRY glAGetProcAddress(const GLubyte * procname);
GLAPI GLAContext            GLAPIENTRY glAGetCurrentContext();
GLAPI BOOL                  GLAPIENTRY glASetRast(GLAContext ctx, struct TagItem *tagList);
GLAPI void                  GLAPIENTRY glAGetConfig(GLAContext ctx, GLenum pname, GLint* params);
GLAPI GLADrawable           GLAPIENTRY glACreateDrawable(GLAContext ctx, struct TagItem *tagList);
GLAPI void                  GLAPIENTRY glADestroyDrawable(GLADrawable drawable);
GLAPI BOOL                  GLAPIENTRY glAMakeCurrentDrawables(GLAContext ctx, GLADrawable draw, GLADrawable read);

#ifdef __cplusplus
}
#endif

#endif /* GLA_H */
