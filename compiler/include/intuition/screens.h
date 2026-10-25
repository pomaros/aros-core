#ifndef INTUITION_SCREENS_H
#define INTUITION_SCREENS_H

/*
    Copyright © 1995-2025, The AROS Development Team. All rights reserved.
    $Id$

    Desc: Screen handling structures
    Lang: english
*/

#ifndef EXEC_TYPES_H
#   include <exec/types.h>
#endif
#ifndef GRAPHICS_CLIP_H
#   include <graphics/clip.h>
#endif
#ifndef GRAPHICS_GFX_H
#   include <graphics/gfx.h>
#endif
#ifndef GRAPHICS_LAYERS_H
#   include <graphics/layers.h>
#endif
#ifndef GRAPHICS_TEXT_H
#   include <graphics/text.h>
#endif
#ifndef GRAPHICS_RASTPORT_H
#   include <graphics/rastport.h>
#endif
#ifndef GRAPHICS_VIEW_H
#   include <graphics/view.h>
#endif
#ifndef UTILITY_TAGITEM_H
#   include <utility/tagitem.h>
#endif

struct Screen
{
    struct Screen * NextScreen;
    struct Window * FirstWindow;

    WORD LeftEdge;
    WORD TopEdge;
    WORD Width;
    WORD Height;

    WORD MouseY;
    WORD MouseX;

    UWORD   Flags;
    UBYTE * Title;
    UBYTE * DefaultTitle;

    BYTE BarHeight;
    BYTE BarVBorder;
    BYTE BarHBorder;
    BYTE MenuVBorder;
    BYTE MenuHBorder;
    BYTE WBorTop;
    BYTE WBorLeft;
    BYTE WBorRight;
    BYTE WBorBottom;

    struct TextAttr * Font;

    struct ViewPort   ViewPort;
    struct RastPort   RastPort;
    struct BitMap     BitMap_OBSOLETE;    /* OBSOLETE */
    struct Layer_Info LayerInfo;

    struct Gadget * FirstGadget;

    UBYTE DetailPen;
    UBYTE BlockPen;

    UWORD          SaveColor0;
    struct Layer * BarLayer;
    UBYTE        * ExtData;
    UBYTE        * UserData;
};

struct NewScreen
{
    WORD LeftEdge;
    WORD TopEdge;
    WORD Width;
    WORD Height;

    WORD  Depth;
    UBYTE DetailPen;
    UBYTE BlockPen;

    UWORD ViewModes;
    UWORD Type;      /* see below */

    struct TextAttr * Font;
    UBYTE           * DefaultTitle;
    struct Gadget   * Gadgets;
    struct BitMap   * CustomBitMap;
};

struct ExtNewScreen
{
    WORD LeftEdge;
    WORD TopEdge;
    WORD Width;
    WORD Height;

    WORD  Depth;
    UBYTE DetailPen;
    UBYTE BlockPen;

    UWORD ViewModes;
    UWORD Type;      /* see below */

    struct TextAttr * Font;
    UBYTE           * DefaultTitle;
    struct Gadget   * Gadgets;
    struct BitMap   * CustomBitMap;

/* ExtNewScreen specific extension */
    struct TagItem * Extension; /* see below */
};

/* Screen->Flags and (Ext)NewScreen->Type*/
#define WBENCHSCREEN (1<<0)
#define PUBLICSCREEN (1<<1)
#define CUSTOMSCREEN 0x000F
#define SCREENTYPE   0x000F
/* Screen->Flags */
#define SHOWTITLE    (1<<4)
#define BEEPING      (1<<5)
#define CUSTOMBITMAP (1<<6)
#define SCREENBEHIND (1<<7)
#define SCREENQUIET  (1<<8)
#define SCREENHIRES  (1<<9)
#define PENSHARED    (1<<10)
#define NS_EXTENDED  (1<<12)
#define AUTOSCROLL   (1<<14)

/* Height */
#define STDSCREENHEIGHT -1
/* Width */
#define STDSCREENWIDTH  -1

/* ExtNewScreen->Extension (Tags) */
#define SA_Dummy                (TAG_USER + 32 )
#define SA_Left                 (SA_Dummy + 1  ) /* [ISG] LONG            Left edge position */
#define SA_Top                  (SA_Dummy + 2  ) /* [ISG] LONG            Top edge position  */
#define SA_Width                (SA_Dummy + 3  ) /* [I.G] LONG            Screen width       */
#define SA_Height               (SA_Dummy + 4  ) /* [I.G] LONG            Screen height      */
#define SA_Depth                (SA_Dummy + 5  ) /* [I.G] LONG            Screen depth       */
#define SA_DetailPen            (SA_Dummy + 6  )
#define SA_BlockPen             (SA_Dummy + 7  )
#define SA_Title                (SA_Dummy + 8  )
#define SA_Colors               (SA_Dummy + 9  )
#define SA_ErrorCode            (SA_Dummy + 10 )
#define SA_Font                 (SA_Dummy + 11 )
#define SA_SysFont              (SA_Dummy + 12 )
#define SA_Type                 (SA_Dummy + 13 )
#define SA_BitMap               (SA_Dummy + 14 )
#define SA_PubName              (SA_Dummy + 15 ) /* [I.G] STRPTR          Public screen name */
#define SA_PubSig               (SA_Dummy + 16 )
#define SA_PubTask              (SA_Dummy + 17 )
#define SA_DisplayID            (SA_Dummy + 18 ) /* [I.G] ULONG           Display mode ID    */
#define SA_DClip                (SA_Dummy + 19 )
#define SA_Overscan             (SA_Dummy + 20 ) /* see below */

#define SA_ShowTitle            (SA_Dummy + 22 )
#define SA_Behind               (SA_Dummy + 23 ) /* [I.G] BOOL                               */
#define SA_Quiet                (SA_Dummy + 24 )
#define SA_AutoScroll           (SA_Dummy + 25 )
#define SA_Pens                 (SA_Dummy + 26 )
#define SA_FullPalette          (SA_Dummy + 27 )
#define SA_ColorMapEntries      (SA_Dummy + 28 )
#define SA_Parent               (SA_Dummy + 29 )
#define SA_Draggable            (SA_Dummy + 30 )
#define SA_Exclusive            (SA_Dummy + 31 )
#define SA_SharePens            (SA_Dummy + 32 )
#define SA_BackFill             (SA_Dummy + 33 )
#define SA_Interleaved          (SA_Dummy + 34 )
#define SA_Colors32             (SA_Dummy + 35 )
#define SA_VideoControl         (SA_Dummy + 36 )
#define SA_FrontChild           (SA_Dummy + 37 )
#define SA_BackChild            (SA_Dummy + 38 )
#define SA_LikeWorkbench        (SA_Dummy + 39 )
#define SA_MinimizeISG          (SA_Dummy + 41 )

#define SA_Displayed            (SA_Dummy + 101) /* [..G] BOOL            The screen is currently on display                 */
#define SA_MonitorName          (SA_Dummy + 102) /* [I.G] STRPTR          Monitor object name                                */
#define SA_MonitorObject        (SA_Dummy + 103) /* [..G] Object *        Monitorclass object                                */
#define SA_TopLeftScreen        (SA_Dummy + 112) /* [..G] struct Screen * Pointer to the screen displayed on another monitor */
#define SA_TopMiddleScreen      (SA_Dummy + 113)
#define SA_TopRightScreen       (SA_Dummy + 114)
#define SA_MiddleLeftScreen     (SA_Dummy + 115)
#define SA_MiddleRightScreen    (SA_Dummy + 117)
#define SA_BottomLeftScreen     (SA_Dummy + 118)
#define SA_BottomMiddleScreen   (SA_Dummy + 119)
#define SA_BottomRightScreen    (SA_Dummy + 120)
#define SA_StopBlanker          (SA_Dummy + 121) /* [.S.] BOOL            Reserved                                            */
#define SA_ShowPointer          (SA_Dummy + 122) /* [ISG] BOOL            Whether to show mouse pointer on custom screen      */
#define SA_GammaControl         (SA_Dummy + 123) /* [I..] BOOL            Enable custom gamma table for the screen            */
#define SA_GammaRed             (SA_Dummy + 124) /* [ISG] UBYTE *         Gamma table for red color                           */
#define SA_GammaBlue            (SA_Dummy + 125) /* [ISG] UBYTE *         Gamma table for blue color                          */
#define SA_GammaGreen           (SA_Dummy + 126) /* [ISG] UBYTE *         Gamma table for green color                         */
#define SA_3DSupport            (SA_Dummy + 127) /* [I..] BOOL            Make sure that screen has 3D support                */
#define SA_AdaptSize            (SA_Dummy + 128) /* [I..] BOOL            Adapt screen size to display size                   */
#define SA_DisplayWidth         (SA_Dummy + 129) /* [I.G] ULONG           Width of the display mode to search for             */ 
#define SA_DisplayHeight        (SA_Dummy + 130) /* [I.G] ULONG           Height of the display mode to search for            */ 
#define SA_OpacitySupport       (SA_Dummy + 131) /* [..G]                 Reserved                                            */
#define SA_SourceAlphaSupport   (SA_Dummy + 132) /* [..G]                 Reserved                                            */
#define SA_PixelFormat          (SA_Dummy + 133) /* [..G] ULONG           Pixel format of the screen                          */
#define SA_ScreenbarTextYPos    (SA_Dummy + 134) /* [..G] LONG            Y position (baseline) of the screen bar text        */
#define SA_ScreenbarTextPen     (SA_Dummy + 135) /* [..G] ULONG           Pen color for the screenbar text                    */
#define SA_ScreenbarTextFont    (SA_Dummy + 136) /* [..G] struct TextFont * Font for the screenbar text                       */
#define SA_ScreenbarSignal      (SA_Dummy + 137) /* [..G] ULONG           Reserved (MorphOS: signal bit for screenbar pluins) */
#define SA_ExactMatchMonitorName (SA_Dummy + 138)/* [I..] BOOL            Strictly obey SA_MonitorName                        */
#define SA_CompositingLayers    (SA_Dummy + 139) /* [I.G] BOOL            Reserved (MorphOS: use composition for layers)      */
#define SA_CompositingFlags     (SA_Dummy + 140) /* [ISG] ULONG           AROS: Allow openers to request compositing modes    */
#define SA_AlphaPreCompositingHook (SA_Dummy + 141) /* [ISG] Hook *       AROS: Hook to Pre-Process Composited buffers        */
#define SA_ScreenbarTextColor   (SA_Dummy + 160) /* [..G] ULONG           AROS: Raw color for the screenbar text              */
#define SA_Scale                (SA_Dummy + 161) /* [ISG] ULONG           AROS: the screen's scale in percent, 100..400 in
                                                                          steps of 25; one point is Scale/100 pixels. The
                                                                          physical raster/bar font is opened at its size times
                                                                          the scale; Screen.Font/GetScreenDrawInfo remain
                                                                          unscaled logical defaults. */
/* [..G] ULONG: actual legacy presentation factor, 1..4. Returns 1 on
 * screens without legacy composition; otherwise nearest integer percentage
 * with half steps rounded up. Independent of a future window's awareness. */
#define SA_LegacyScaleFactor    (SA_Dummy + 163)
struct DrawInfo;
struct ScreenRenderContext
{
    UWORD src_Scale;                 /* requested percentage,100..400 */
    UWORD src_LegacyScaleFactor;     /* actual integer canvas factor */
    ULONG src_PhysicalBarHeight;    /* full physical extent: BarHeight + 1 */
    UWORD src_PhysicalBarHBorder, src_PhysicalBarVBorder; /* physical pixels */
    UWORD src_PhysicalMenuHBorder, src_PhysicalMenuVBorder; /* physical pixels */
    UWORD src_LogicalBarHBorder, src_LogicalBarVBorder; /* unscaled pixels */
    UWORD src_LogicalMenuHBorder, src_LogicalMenuVBorder; /* unscaled pixels */
    struct TTextAttr src_BaseFont;   /*100% attributes, including owned tags */
    struct DrawInfo *src_LogicalDrawInfo;
    struct DrawInfo *src_PhysicalDrawInfo;
    BOOL src_PreparedScaleWindow;    /* exact window decorator admits native prepared frames */
};

/* [..G] const struct ScreenRenderContext *: one coherent render snapshot.
 * The snapshot, its attributes, fonts and images are immutable and borrowed
 * until CloseScreen; retain the normal window/public-screen lifetime lock.
 * Reacquire after IDCMP_CHANGESCALE for new rendering. Do not free it. */
#define SA_RenderContext       (SA_Dummy + 164)

/* [..S] struct ScreenScaleResult *: synchronous result of SA_Scale in this
 * SetAttrs call, independent of tag order. Caller-owned storage is cleared
 * for every attempt and is never retained. Zero initialization detects an
 * older implementation which does not report this tag. */
#define SA_ScaleResult         (SA_Dummy + 165)
#define SSR_UNREPORTED 0
#define SSR_SUCCESS 1
#define SSR_UNCHANGED 2
#define SSR_FAILED 3
#define SSR_REASON_NONE 0
#define SSR_REASON_INVALID_REQUEST 1
#define SSR_REASON_INVALID_CONTEXT 2
#define SSR_REASON_UNSUPPORTED 3
#define SSR_REASON_NO_MEMORY 4
#define SSR_REASON_PREPARE_FAILED 5
#define SSR_REASON_GEOMETRY 6
#define SSR_REASON_COMMIT_FAILED 7
#define SSR_DOMAIN_NONE 0
#define SSR_DOMAIN_LOGICAL 1
#define SSR_DOMAIN_PHYSICAL 2
#define SSR_AXIS_NONE 0
#define SSR_AXIS_WIDTH 1
#define SSR_AXIS_HEIGHT 2
#define SSR_STAGE_NONE 0
#define SSR_STAGE_VALIDATE 1
#define SSR_STAGE_FONT 2
#define SSR_STAGE_LOGICAL_SCREEN 3
#define SSR_STAGE_DECORATION 4
#define SSR_STAGE_RENDER_CONTEXT 5
#define SSR_STAGE_FRAME 6
#define SSR_STAGE_GEOMETRY 7
#define SSR_STAGE_ACTIVE_GADGET 8
#define SSR_STAGE_REQUESTER 9
#define SSR_STAGE_MENU 10
#define SSR_STAGE_POINTER 11
#define SSR_STAGE_LAYER_GEOMETRY 12
#define SSR_STAGE_CURSOR 13
#define SSR_STAGE_PIXELS 14
#define SSR_STAGE_NOTIFICATIONS 15
#define SSR_STAGE_COMMIT 16
struct ScreenScaleResult
{
    ULONG ssr_Status, ssr_Reason, ssr_Stage, ssr_Domain, ssr_Axis;
    ULONG ssr_RequestedScale, ssr_AppliedScale;
    IPTR ssr_Window; /* Opaque identity only: never dereference it. */
    /* Valid when Domain is not NONE. These are effective fitting limits in
     * that domain; maxima include the viewport cap, not an unlimited sentinel.
     * A reported zero maximum is an invalid zero constraint. */
    LONG ssr_MinWidth, ssr_MinHeight, ssr_MaxWidth, ssr_MaxHeight;
    LONG ssr_AvailableWidth, ssr_AvailableHeight;
    UBYTE ssr_WindowTitle[128]; /* Owned, truncated if necessary, always NUL. */
    /* The exact check that refused, SSR_DETAIL_*; NONE when Reason says all.
     * For SSR_DETAIL_DECORATOR, DetailMethod is the refusing WDM_* method and
     * DecorRefusal its WDP_REFUSE_* cause. DetailValue meanings are listed
     * per detail. DetailName is an owned copy (resource or key name), never a
     * pointer into a decorator, truncated if necessary and always NUL. */
    ULONG ssr_Detail, ssr_DetailMethod, ssr_DecorRefusal;
    LONG ssr_DetailValue[3];
    /* SSR_DETAIL_LAYER*: LayerRole is the SSR_LAYER_* role of the refusing
     * layer, Layer its opaque identity. For SSR_DETAIL_LAYER the remaining
     * fields copy the Layers refusal (graphics/layertransaction.h): LayerCheck
     * LGR_*, LayerFlags LGRF_*, LayerPixels LGR_PIXELS_*, LayerLease
     * LGR_LEASE_*, LayerDriver HIDD_PIXELS_REFUSE_* and LayerValue its four
     * values; DetailName is the refusing bitmap class or driver. */
    ULONG ssr_LayerRole, ssr_LayerCheck, ssr_LayerFlags;
    ULONG ssr_LayerPixels, ssr_LayerLease, ssr_LayerDriver;
    LONG ssr_LayerValue[4];
    IPTR ssr_Layer; /* Opaque identity only: never dereference it. */
    UBYTE ssr_DetailName[64];
};
#define SSR_DETAIL_NONE 0
#define SSR_DETAIL_FRAME_NO_GENERATION 1   /* window has no committed frame */
#define SSR_DETAIL_FRAME_CONTEXT 2         /* context draw infos absent or for another screen */
#define SSR_DETAIL_FRAME_CONTEXT_BORDERS 3 /* [0] 1 logical, 0 physical border set absent */
#define SSR_DETAIL_FRAME_NO_FONT 4         /* context has no frame font */
#define SSR_DETAIL_FRAME_BORDER_RANGE 5    /* [0] side 0..3 left/top/right/bottom, [1] value */
#define SSR_DETAIL_FRAME_SIZE_IMAGE 6      /* [0] query status, [1] width, [2] height */
#define SSR_DETAIL_FRAME_LAYOUT_SIZE 7     /* [0] width, [1] height of the proposed frame */
#define SSR_DETAIL_FRAME_USER_BUFFER 8     /* [0] decorator WDA_UserBuffer size */
#define SSR_DETAIL_FRAME_UNTYPED_GADGET 9  /* [0] system gadget slot without image type */
#define SSR_DETAIL_FRAME_IMAGE_QUERY 10    /* Name image; [0] status, [1] width, [2] height */
#define SSR_DETAIL_FRAME_IMAGE_EXTENT 11   /* Name image; [0] width, [1] height, [2] preferred height */
#define SSR_DETAIL_FRAME_IMAGE_OBJECT 12   /* Name image; [0] width, [1] height */
#define SSR_DETAIL_FRAME_METRICS 13        /* window absent from the measured frame set */
#define SSR_DETAIL_DECORATOR 14            /* see DetailMethod and DecorRefusal */
#define SSR_DETAIL_LAYER 15                /* the Layers batch refused: see the Layer fields */
#define SSR_DETAIL_LAYER_REQUEST 16        /* LayerValue [0] left, [1] top, [2] width, [3] height
                                            * of a proposed layer outside the WORD range */
#define SSR_DETAIL_COMPOSITION 17          /* legacy composition could not project to
                                            * [0] width x [1] height physical pixels */
/* LayerRole. NONE: the refusal named no layer. OTHER: a layer Intuition
 * holds no record for; Layer identifies it. */
#define SSR_LAYER_NONE 0
#define SSR_LAYER_OTHER 1
#define SSR_LAYER_ROOT 2            /* the screen's root layer (uncovered backdrop) */
#define SSR_LAYER_BAR 3             /* the screen title bar */
#define SSR_LAYER_WINDOW 4          /* a window's only layer */
#define SSR_LAYER_WINDOW_BORDER 5   /* a GIMMEZEROZERO window's border layer */
#define SSR_LAYER_WINDOW_CLIENT 6   /* a GIMMEZEROZERO window's client layer */
#define SSR_LAYER_REQUESTER 7       /* a requester layer of the window */
#define SSR_LAYER_PRESENTATION 8    /* a legacy window's physical presentation layer */
#define SSR_LAYER_CANVAS 9          /* a legacy window's layer in its logical canvas */
#define SSR_LAYER_CANVAS_ROOT 10    /* the root layer of a legacy window's logical canvas */
#define SSR_LAYER_PRIVATE 11        /* another layer in a private composed canvas */

#define SA_SCALE_MIN            100
#define SA_SCALE_MAX            400
#define SA_SCALE_STEP           25

/* SA_Overscan */
#define OSCAN_TEXT     1
#define OSCAN_STANDARD 2
#define OSCAN_MAX      3
#define OSCAN_VIDEO    4

                            /* Public Screens */

struct PubScreenNode
{
    struct Node psn_Node;

    struct Screen * psn_Screen;
    UWORD           psn_Flags;        /* see below */
    WORD            psn_Size;
    WORD            psn_VisitorCount;
    struct Task   * psn_SigTask;
    UBYTE           psn_SigBit;
};

/* psn_Flags */
#define PSNF_PRIVATE (1<<0)

/* Maximum length of public screen names. The buffers containing these strings
   must have a length of MAXPUBSCREENNAME+1. */   
#define MAXPUBSCREENNAME 139

#define SHANGHAI     (1<<0)
#define POPPUBSCREEN (1<<1)

                         /* Screen functions */

/* AllocScreenBuffer() */
#define SB_SCREEN_BITMAP 1
#define SB_COPY_BITMAP   2

struct ScreenBuffer
{
    struct BitMap   * sb_BitMap;
    struct DBufInfo * sb_DBufInfo;
};

/* ScreenDepth() */
#define SDEPTH_TOFRONT  0
#define SDEPTH_TOBACK   1
#define SDEPTH_INFAMILY 2

/* ScreenPosition() */
#define SPOS_RELATIVE    0x00
#define SPOS_ABSOLUTE    (1<<0)
#define SPOS_MAKEVISIBLE (1<<1)
#define SPOS_FORCEDRAG   (1<<2)

                            /* Draw Info */

struct DrawInfo
{
    UWORD             dri_Version; /* see below */
    UWORD             dri_NumPens;
    UWORD           * dri_Pens;    /* see below */
    struct TextFont * dri_Font;
    UWORD             dri_Depth;

    struct
    {
        UWORD X;
        UWORD Y;
    } dri_Resolution;

    ULONG dri_Flags; /* see below */

    /* The following fields are present if dri_Version >= 2 */
    struct Image * dri_CheckMark;
    struct Image * dri_AmigaKey;  

    /*
     * The following fields are compatible with AmigaOS v4.
     * Present if dri_Version >= 3.
     */
    struct Screen *dri_Screen;
    APTR           dri_Prefs;

    IPTR           dri_Reserved[3];
};

/* dri_Version */
#define DRI_VERSION 3

/* dri_Flags */
#define DRIF_NEWLOOK (1L<<0)

/* dri_Pens */
#define DETAILPEN        0
#define BLOCKPEN         1
#define TEXTPEN          2
#define SHINEPEN         3
#define SHADOWPEN        4
#define FILLPEN          5
#define FILLTEXTPEN      6
#define BACKGROUNDPEN    7
#define HIGHLIGHTTEXTPEN 8
#define BARDETAILPEN     9
#define BARBLOCKPEN      10
#define BARTRIMPEN       11
#define NUMDRIPENS       12

#define PEN_C0 0xFEFF
#define PEN_C1 0xFEFE
#define PEN_C2 0xFEFD
#define PEN_C3 0xFEFC

/* values for ChangeDecoration ID param */

#define DECORATION_SET     0x8001
#define DECORATION_DEFAULT 0x8000

/* Errors */
#define OSERR_NOMONITOR    1
#define OSERR_NOCHIPS      2
#define OSERR_NOMEM        3
#define OSERR_NOCHIPMEM    4
#define OSERR_PUBNOTUNIQUE 5
#define OSERR_UNKNOWNMODE  6
#define OSERR_TOODEEP      7
#define OSERR_ATTACHFAIL   8
#define OSERR_NOTAVAILABLE 9

#endif /* INTUITION_SCREENS_H */
