#ifndef INTUITION_WINDECORCLASS_H
#define INTUITION_WINDECORCLASS_H

/*
    Copyright  1995-2001, The AROS Development Team. All rights reserved.
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    $Id$

    Desc: Headerfile for Intuitions' WINDECORCLASS
    Lang: english
*/

#ifndef UTILITY_TAGITEM_H
#   include <utility/tagitem.h>
#endif

#ifndef INTUITION_IMAGECLASS_H
#   include <intuition/imageclass.h>
#endif

#ifndef INTUITION_INTUITION_H
#   include <intuition/intuition.h>
#endif

#ifndef INTUITION_SCREENS_H
#   include <intuition/screens.h>
#endif

/* Attributes for WINDECORCLASS */
#define WDA_Dummy		    (TAG_USER + 0x22000)
#define WDA_DrawInfo	    	    (WDA_Dummy + 1) 	    /* I.G */
#define WDA_Screen  	    	    (WDA_Dummy + 2) 	    /* I.G */
#define WDA_TrueColorOnly	    (WDA_Dummy + 3) 	    /* ..G */
#define WDA_UserBuffer              (WDA_Dummy + 4)         /* I.G */
/* Read-only BOOL: this exact decorator class implements prepared scale
 * window resources/layout. Custom subclasses explicitly implement this getter
 * to opt in; inheriting a built-in implementation returns FALSE. */
#define WDA_PreparedScaleWindow (WDA_Dummy + 5) /* ..G */


/* Methods for WINDECORCLASS */
#define WDM_Dummy   	    	    (WDA_Dummy + 500)

#define WDM_SETUP   	    	    (WDM_Dummy + 1)
#define WDM_CLEANUP 	    	    (WDM_Dummy + 2)
#define WDM_GETDEFSIZE_SYSIMAGE     (WDM_Dummy + 3)
#define WDM_DRAW_SYSIMAGE   	    (WDM_Dummy + 4)
#define WDM_DRAW_WINBORDER  	    (WDM_Dummy + 5)
#define WDM_LAYOUT_BORDERGADGETS    (WDM_Dummy + 6)
#define WDM_DRAW_BORDERPROPBACK     (WDM_Dummy + 7)
#define WDM_DRAW_BORDERPROPKNOB     (WDM_Dummy + 8)
#define WDM_INITWINDOW              (WDM_Dummy + 9)
#define WDM_EXITWINDOW              (WDM_Dummy + 10)
#define WDM_WINDOWSHAPE             (WDM_Dummy + 11)
#define WDM_PREPARESCALESHAPE       (WDM_Dummy + 12)
#define WDM_GETSCALEDEFSIZE_SYSIMAGE (WDM_Dummy + 13)
#define WDM_DRAWSCALE_SYSIMAGE      (WDM_Dummy + 14)
#define WDM_RELEASESCALE_SYSIMAGE   (WDM_Dummy + 15)
#define WDM_PREPARESCALEWINDOW      (WDM_Dummy + 16)
#define WDM_LAYOUTSCALEBORDERGADGETS (WDM_Dummy + 17)


struct wdpGetDefSizeSysImage
{
    STACKED ULONG	     MethodID;
    STACKED BYTE        wdp_TrueColor;
    STACKED struct DrawInfo *wdp_Dri;
    STACKED struct TextFont *wdp_ReferenceFont; /* In: */
    STACKED ULONG	     wdp_Which;  	/* In: One of CLOSEIMAGE, SIZEIMAGE, ... */
    STACKED ULONG	     wdp_SysiSize;	/* In: lowres/medres/highres */
    STACKED ULONG	        *wdp_Width;  	/* Out */
    STACKED ULONG	        *wdp_Height; 	/* Out */
    STACKED ULONG	     wdp_Flags;
    STACKED IPTR        wdp_UserBuffer;
    
};

/* The sdpDrawSysImage struct in scrdecorclass.h must match this one!!! */

struct wdpDrawSysImage
{
    STACKED ULONG	     MethodID;
    STACKED BYTE        wdp_TrueColor;
    STACKED struct DrawInfo *wdp_Dri;
    STACKED struct RastPort *wdp_RPort;
    STACKED LONG	     wdp_X;
    STACKED LONG	     wdp_Y;
    STACKED LONG	     wdp_Width;
    STACKED LONG	     wdp_Height;
    STACKED ULONG	     wdp_Which;
    STACKED ULONG	     wdp_State;
    STACKED ULONG	     wdp_Flags;
    STACKED IPTR        wdp_UserBuffer;
};

struct wdpDrawWinBorder
{
    STACKED ULONG	     MethodID;
    STACKED BYTE        wdp_TrueColor;
    STACKED struct DrawInfo *wdp_Dri;
    STACKED struct Window   *wdp_Window;
    STACKED struct RastPort *wdp_RPort;
    STACKED ULONG	     wdp_Flags;
    STACKED IPTR        wdp_UserBuffer;
};

struct wdpLayoutBorderGadgets
{
    STACKED ULONG	     MethodID;
    STACKED BYTE        wdp_TrueColor;
    STACKED struct DrawInfo *wdp_Dri;
    STACKED struct Window   *wdp_Window;
    STACKED struct Gadget   *wdp_Gadgets;
    STACKED ULONG	     wdp_Flags;
    STACKED ULONG       wdp_ExtraButtons;
    STACKED IPTR        wdp_UserBuffer;
};

struct wdpDrawBorderPropBack
{
    STACKED ULONG	      MethodID;
    STACKED BYTE         wdp_TrueColor;
    STACKED struct DrawInfo  *wdp_Dri;
    STACKED struct Window    *wdp_Window;
    STACKED struct RastPort  *wdp_RPort;
    STACKED struct Gadget    *wdp_Gadget;
    STACKED struct Rectangle *wdp_RenderRect;
    STACKED struct Rectangle *wdp_PropRect;
    STACKED struct Rectangle *wdp_KnobRect;
    STACKED ULONG	      wdp_Flags;
    STACKED IPTR         wdp_UserBuffer;
};

struct wdpDrawBorderPropKnob
{
    STACKED ULONG	      MethodID;
    STACKED BYTE         wdp_TrueColor;
    STACKED struct DrawInfo  *wdp_Dri;
    STACKED struct Window    *wdp_Window;
    STACKED struct RastPort  *wdp_RPort;
    STACKED struct Gadget    *wdp_Gadget;
    STACKED struct Rectangle *wdp_RenderRect;
    STACKED struct Rectangle *wdp_PropRect;
    STACKED ULONG	      wdp_Flags;
    STACKED IPTR         wdp_UserBuffer;
};

struct wdpInitWindow
{
    STACKED ULONG	     MethodID;
    STACKED BYTE        wdp_TrueColor;
    STACKED IPTR        wdp_UserBuffer;
    STACKED struct Screen   *wdp_Screen;
    STACKED IPTR        wdp_ScreenUserBuffer;
};

struct wdpExitWindow
{
    STACKED ULONG       MethodID;
    STACKED BYTE        wdp_TrueColor;
    STACKED IPTR	     wdp_UserBuffer;
};

/* Caller-owned refusal record for the typed scale methods below. The caller
 * clears it; a refusing provider may store one WDP_REFUSE_* cause, up to three
 * cause values and a NUL-terminated resource/key name. Intuition copies it into
 * the scale receipt and never retains it. NULL means no record is wanted. */
struct wdpScaleRefusal
{
    ULONG wsr_Code;
    LONG wsr_Value[3];
    UBYTE wsr_Name[64];
};
#define WDP_REFUSE_NONE 0         /* provider stated no cause */
#define WDP_REFUSE_UNSUPPORTED 1  /* class does not implement the prepared method */
#define WDP_REFUSE_ARGUMENTS 2    /* message invalid: [0] scale, [1] width, [2] height */
#define WDP_REFUSE_STATE 3        /* userbuffer absent, already prepared or not prepared */
#define WDP_REFUSE_NO_MEMORY 4    /* [0] bytes requested */
#define WDP_REFUSE_CONFIG 5       /* Name key; [0] theme value, [1] scale */
#define WDP_REFUSE_IMAGE_GRID 6   /* Name image; [0] width, [1] height, [2] cols*100+rows */
#define WDP_REFUSE_IMAGE_EXTENT 7 /* Name image; [0] cell width, [1] cell height */
#define WDP_REFUSE_ELEMENT 8      /* Name image; [0] offset, [1] size, [2] image extent */
#define WDP_REFUSE_FONT 9         /* [0] font height, [1] theme bar height */
#define WDP_REFUSE_SUPERCLASS 10  /* superclass refused the delegated method [0] */

/* Explicit immutable-generation methods. Existing message layouts remain
 * unchanged. Unsupported inherited subclasses must refuse typed preparation.
 * A successful prepare owns only the detached caller-provided userbuffer.
 * Failure disposes all partial provider resources before returning; callers
 * free the empty buffer without invoking an unsupported old Exit method.
 * Admission is exact-class at WDA_PreparedScaleWindow and
 * WDM_PREPARESCALEWINDOW only: a subclass that implements the typed methods
 * may delegate WDM_LAYOUTSCALEBORDERGADGETS to its superclass. */
struct wdpPrepareScaleWindow
{
    struct wdpInitWindow base;
    STACKED const struct ScreenRenderContext *context;
    STACKED BOOL logical;
    STACKED struct TextFont *font;
    STACKED LONG width, height, borderTop;
    STACKED struct wdpScaleRefusal *refusal;
};
struct wdpLayoutScaleBorderGadgets
{
    struct wdpLayoutBorderGadgets base;
    STACKED const struct ScreenRenderContext *context;
    STACKED BOOL logical;
    STACKED struct wdpScaleRefusal *refusal;
};
struct wdpGetScaleDefSizeSysImage
{
    struct wdpGetDefSizeSysImage base;
    STACKED const struct ScreenRenderContext *context;
    STACKED BOOL logical;
    STACKED ULONG requestedWidth, requestedHeight;
    STACKED APTR *prepared;
    STACKED struct wdpScaleRefusal *refusal;
};
struct wdpDrawScaleSysImage
{
    struct wdpDrawSysImage base;
    STACKED const struct ScreenRenderContext *context;
    STACKED BOOL logical;
    STACKED APTR prepared;
};
struct wdpReleaseScaleSysImage
{
    STACKED ULONG MethodID;
    STACKED APTR prepared;
};
#define WDP_SCALE_FAILED 0
#define WDP_SCALE_READY 1
#define WDP_SCALE_UNAVAILABLE 2

/* Read-only preparation for an explicitly admitted default window shape hook.
 * TRUE with *Result==NULL means rectangular; FALSE means unsupported/OOM.
 * A returned Region is caller-owned. All geometry/font inputs are proposed,
 * not yet installed. The live Window and userbuffer/cache must stay unchanged.
 * Inheriting subclasses must explicitly implement their own capability.
 */
struct wdpPrepareScaleShape
{
    STACKED ULONG MethodID;
    STACKED struct Window *wdp_Window;
    STACKED LONG wdp_Width, wdp_Height, wdp_BorderTop;
    STACKED struct TextFont *wdp_Font;
    STACKED IPTR wdp_UserBuffer;
    STACKED struct Region **wdp_Result;
};

struct wdpWindowShape
{
    STACKED ULONG       MethodID;
    STACKED BYTE        wdp_TrueColor;
    STACKED struct Window   *wdp_Window;
    STACKED LONG        wdp_Width;
    STACKED LONG        wdp_Height;
    STACKED IPTR        wdp_UserBuffer;
};

/* WinDecor DrawWindowBorder Flags */ 
#define  WDF_DWB_TOP_ONLY   	1   /* Draw top border only */

/* WinDecor DrawWinTitle Title Align */
#define WD_DWTA_LEFT 	    	0
#define WD_DWTA_RIGHT 	    	1
#define WD_DWTA_CENTER      	2

/* WinDecor LayourBorderGadgets Flags */
#define WDF_LBG_INITIAL     	1   /* First time == During OpenWindow */
#define WDF_LBG_SYSTEMGADGET	2   /* Is a system gadget (close/depth/zoom) */
#define WDF_LBG_INGADLIST   	4   /* Gadget is already in window gadget list */
#define WDF_LBG_MULTIPLE    	8   /* There may be multiple gadgets (linked
                                       together through NextGadget. Follow it) */
/* WinDecor DrawBorderPropKnob Flags */
#define WDF_DBPK_HIT	    	1   /* Knob is hit / in use by user*/

#endif /* INTUITION_WINDECORCLASS_H */
