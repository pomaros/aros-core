/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Copyright (C) 1995-2013, The AROS Development Team. All rights reserved.
*/

#ifndef _ICONLIST_PRIVATE_H_
#define _ICONLIST_PRIVATE_H_

#include "iconlist.h"

extern struct MUI_CustomClass *IconListScaleClass;

/*** Instance data **********************************************************/

struct IconViewModeAttribs
{
    IPTR            ivma_Temp;
};

struct ListViewModeAttribs
{
    ULONG            lvma_Flags;

    struct DiskObject        *lvma_IconDrawer;
    struct DiskObject        *lvma_IconFile;

    LONG            lmva_HeaderHeight;
    LONG            lmva_RowHeight;

    BYTE            lmva_SortColumn;
    BYTE            lmva_LastSelectedColumn;

    /* TODO: Convert Column arrays into a list ... */
#define NUM_COLUMNS 6
    LONG                        lmva_ColumnPos[NUM_COLUMNS];
    LONG                        lmva_ColumnMaxWidth[NUM_COLUMNS];
    LONG                        lmva_ColumnWidth[NUM_COLUMNS];
    ULONG            lmva_ColumnFlags[NUM_COLUMNS];
    BYTE                        lmva_ColumnHAlign[NUM_COLUMNS];
    STRPTR                      lmva_ColumnTitle[NUM_COLUMNS];
};

#define LVMAF_NOHEADER         (1<<0)
#define LVMAF_HEADERDRAWTOEND    (1<<1)
#define LVMAF_ROWDRAWTOEND    (1<<2)

#define LVMCF_COLVISIBLE    (1<<0)
#define LVMCF_COLCLICKABLE    (1<<1)
#define LVMCF_COLSORTABLE    (1<<2)

struct IconList_DATA
{
    BOOL                          icld_TileMotion; /* keep IDCMP_MOUSEMOVE for shown tiles */
    BOOL                          icld_TilesShown; /* successful Show until Hide/Cleanup */
    IPTR                          icld_ContextRecalc;
    ULONG                         icld_RenderScale;
    ULONG                         icld_BaseIconHorizontalSpacing;
    ULONG                         icld_BaseIconVerticalSpacing;
    ULONG                         icld_BaseIconImageSpacing;
    ULONG                         icld_BaseLabelTextHorizontalPadding;
    ULONG                         icld_BaseLabelTextVerticalPadding;
    ULONG                         icld_BaseLabelTextBorderWidth;
    ULONG                         icld_BaseLabelTextBorderHeight;
    APTR                          icld_Pool;                          /* Pool to allocate data from */

    struct RastPort               *icld_DisplayRastPort;
    struct RastPort               *icld_BufferRastPort;
    struct TextFont               *icld_IconLabelFont;
    struct TextFont               *icld_IconInfoFont;
    IPTR                          icld_IconLabelFontRequest;
    struct TextFont               *icld_IconInfoFontRequest;
    struct TextFont               *icld_LabelBaseFont;
    struct TextFont               *icld_InfoBaseFont;
    struct TextFont               *icld_ScaledLabelFont;
    struct TextFont               *icld_ScaledInfoFont;

    struct List                   icld_IconList;                      /* IconEntry(s)     */
    struct List                   icld_SelectionList;                 /* Selected Icon(s) */
    struct IconEntry              *icld_SelectionLastClicked;
    struct IconEntry              *icld_FocusIcon;

    LONG                          icld_ViewX,                         /* the leftmost/upper coordinates of the view */
                                  icld_ViewY;
    ULONG                         icld_ViewWidth,                     /* dimensions of the view (_mwidth(obj) and _mheight(obj)) */
                                  icld_ViewHeight,
                                  icld_AreaWidth,                     /* The whole width/height */
                                  icld_AreaHeight;

    /* Drag/Drop Info .. */

    struct IconList_Drop_Event    *icld_DragDropEvent;                /* the icon where the icons have been dropped */
    struct IconList_Click         icld_ClickEvent;

    /* Input / Event Information */
    struct MUI_EventHandlerNode   ehn;

    LONG                          click_x;
    LONG                          click_y;

    ULONG                         last_secs;                          /* DoubleClick stuff */
    ULONG                         last_mics;

    /* RENDERING DATA! ###### */
    LONG                          icld_DrawOffsetX,                   /* coordinates to render to */
                                  icld_DrawOffsetY;
    ULONG                         icld_DisplayFlags;                  /* Internal Sorting related stuff */
    ULONG                         icld_SortFlags;
    ULONG                         icld_IconAreaLargestWidth;          /* Used for icon/label rendering & */
    ULONG                         icld_IconAreaLargestHeight;         /* Positioning                     */
    ULONG                         icld_IconLargestHeight;
    ULONG                         icld_LabelLargestHeight;

    /*
    Valid values for icld_UpdateMode - :

    UPDATE_SINGLEENTRY = draw the given single icon only
    UPDATE_SCROLL     = scroll the view by update_scrolldx/update_scrolldy
    UPDATE_RESIZE    = resizing window
    
    list view-mode additionaly accepts -:
    
    UPDATE_HEADERENTRY

    */

    ULONG                         icld_UpdateMode;
    WORD                          update_scrolldx;
    WORD                          update_scrolldy;
    WORD                          update_oldwidth;
    WORD                          update_oldheight;

    struct IconEntry              *update_entry;
    struct Rectangle              *update_rect1;
    struct Rectangle              *update_rect2;
    struct Rectangle              view_rect;

    struct Rectangle              icld_LassoRectangle;                /* lasso data */
    BOOL                          icld_LassoActive;


    struct ListViewModeAttribs    *icld_LVMAttribs;

    /* TODO: move config options to a seperate struct */
    /* IconList configuration settings ... */
    ULONG                         icld_LabelPen;        
    ULONG                         icld_LabelShadowPen;
    ULONG                         icld_InfoPen;
    ULONG                         icld_InfoShadowPen;

    ULONG                         icld__Option_LabelTextMaxLen;                   /* max no. of chars to display in a line */
    ULONG                         icld__Option_LastLabelTextMaxLen;

    UBYTE                         icld__Option_IconListMode;                     /* */
    UBYTE                         icld__Option_LabelTextMode;                     /* */
    BOOL                          icld__Option_IconListFixedBackground;          /* */
    BOOL                          icld__Option_IconListScaledBackground;         /* */
    ULONG                         icld__Option_LabelTextMultiLine;               /* No. of lines to display for labels*/
    ULONG                         icld__Option_LastLabelTextMultiLine;
    BOOL                          icld__Option_DragImageTransparent;             /* Should dragging with source alpha be used */

    BOOL                          icld__Option_TrimVolumeNames;

    BOOL                          icld__Option_LabelTextMultiLineOnFocus;        /* Only show "multiline" label for focused icon */
    ULONG                         icld__Option_IconBorderOverlap;
    ULONG                         icld__Option_IconHorizontalSpacing;            /* Horizontal/Vert Space between Icon "Areas" */
    ULONG                         icld__Option_IconVerticalSpacing;
    ULONG                         icld__Option_IconImageSpacing;                 /* Space between Icon Image and Label Frame */
    ULONG                         icld__Option_LabelTextHorizontalPadding;       /* Outer padding between label text and frame */
    ULONG                         icld__Option_LabelTextVerticalPadding;
    ULONG                         icld__Option_LabelTextBorderWidth;             /* Label frame dimensions */
    ULONG                         icld__Option_LabelTextBorderHeight;

    UBYTE                         mouse_pressed;
};

#endif /* _ICONLIST_PRIVATE_H_ */
