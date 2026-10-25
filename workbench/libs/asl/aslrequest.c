/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Copyright (C) 1995-2015, The AROS Development Team. All rights reserved.

    Desc:
*/
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <string.h>
#include <intuition/intuition.h>
#include "asl_intern.h"

struct LayoutData *AllocCommon(ULONG, struct IntReq *, APTR, struct AslBase_intern *);
VOID FreeCommon(struct LayoutData *, struct AslBase_intern *);
BOOL HandleEvents(struct LayoutData *, struct AslReqInfo *, struct AslBase_intern *);


#define SDEBUG 0
#define DEBUG 0


#  include <aros/debug.h>

/* Where Asl prefs put a requester whose program gave no position. The
   NewWindow size is the window's outer size; WA_AutoAdjust keeps it on
   screen. */
static VOID PrefsPosition(struct IntReq *intreq, struct LayoutData *ld, struct NewWindow *nw,
                          struct AslBase_intern *AslBase)
{
    struct Screen *scr = ld->ld_Screen;
    struct Window *parent = intreq->ir_Window;
    WORD outerw = nw->Width;
    WORD outerh = nw->Height;
    LONG factor = ld->ld_ScaleAware ? 1 : ld->ld_RenderContext->src_LegacyScaleFactor;
    WORD left = (scr->ViewPort.DWidth / factor - outerw) / 2 - scr->LeftEdge / factor;
    WORD top = (scr->ViewPort.DHeight / factor - outerh) / 2 - scr->TopEdge / factor;
    LONG parentLeft = 0, parentTop = 0, parentWidth = 0, parentHeight = 0;
    if (parent && parent->WScreen == scr)
    {
        struct Layer *layer;
        ULONG lock = LockIBase(0);
        LockLayerInfo(&scr->LayerInfo);
        for (layer = scr->LayerInfo.top_layer; layer; layer = layer->back)
            if (layer->Window == parent)
            {
                parentLeft = layer->bounds.MinX / factor;
                parentTop = layer->bounds.MinY / factor;
                parentWidth = layer->Width / factor;
                parentHeight = layer->Height / factor;
                break;
            }
        UnlockLayerInfo(&scr->LayerInfo);
        UnlockIBase(lock);
        if (!parentWidth) parent = NULL;
    }

    if (parent && parent->WScreen != scr)
        parent = NULL;

    switch (AslBase->Prefs.ap_SizePosition & ASLPREFS_POS_MASK)
    {
        case ASLPREFS_POS_CENTERWIN:
            if (parent)
            {
                left = parentLeft + (parentWidth - outerw) / 2;
                top = parentTop + (parentHeight - outerh) / 2;
            }
            break;
        case ASLPREFS_POS_TOPLEFTWIN:
            if (parent)
            {
                left = parentLeft + AslScaleValue(ld, AslBase->Prefs.ap_RelativeLeft);
                top = parentTop + AslScaleValue(ld, AslBase->Prefs.ap_RelativeTop);
                break;
            }
            /* fall through: no parent window, the screen's corner */
        case ASLPREFS_POS_TOPLEFTSCR:
            left = AslScaleValue(ld, AslBase->Prefs.ap_RelativeLeft) - scr->LeftEdge / factor;
            top = AslScaleValue(ld, AslBase->Prefs.ap_RelativeTop) - scr->TopEdge / factor;
            break;
        case ASLPREFS_POS_POINTER:
            left = scr->MouseX / factor - outerw / 2;
            top = scr->MouseY / factor - outerh / 2;
            break;
        default:
            break;
    }
    nw->LeftEdge = left < 0 ? 0 : left;
    nw->TopEdge = top < 0 ? 0 : top;
}

/*****************************************************************************

    NAME */
#include <proto/asl.h>
#include <utility/tagitem.h>

        AROS_LH2(BOOL, AslRequest,

/*  SYNOPSIS */
        AROS_LHA(APTR            , requester, A0),
        AROS_LHA(struct TagItem *, tagList, A1),

/*  LOCATION */
        struct Library *, AslBase, 10, Asl)

/*  FUNCTION

    INPUTS

    RESULT

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct ReqNode      *reqnode;
    struct IntReq       *intreq;
    struct AslReqInfo   *reqinfo;
    struct LayoutData   *ld;
    struct Window       *win;


    BOOL success = FALSE;
    BOOL initialized = FALSE;

    D(bug("AslRequest(requester=%p, tagList=%p)\n", requester, tagList));

    if (!requester)
    {
        SetIoErr(ERROR_NO_FREE_STORE);
        return (FALSE);
    }

    /* Find the correspondent internal structure */
    reqnode = FindReqNode(requester, ASLB(AslBase));
    if (!reqnode)
    {
        return (FALSE);
    }

    intreq  = reqnode->rn_IntReq;
    reqinfo = &(ASLB(AslBase)->ReqInfo[intreq->ir_ReqType]);

/*  stegerg: this must be done when the Requester is terminated
**           before setting the new output variables in the Requester struct.
**
**    StripRequester(requester, intreq->ir_ReqType, ASLB(AslBase));
*/

    /* Parse new tags if supplied */
    if (tagList)
    {
        struct ParseTagArgs pta;

        ParseCommonTags(intreq, tagList, ASLB(AslBase));

        /* Parse requester specific tags */
        pta.pta_IntReq  = intreq;
        pta.pta_Req     = reqnode->rn_Req;
        pta.pta_Tags    = tagList;

        CallHookPkt(&(reqinfo->ParseTagsHook), &pta, ASLB(AslBase));
    }

    /* Now do the layout stuff */


    /* Do some allocations common for all requesters */
    ld = AllocCommon(reqinfo->UserDataSize, intreq, requester, ASLB(AslBase));
    if (ld)
    {
        D(bug("Common stuff allocated\n"));

        /* Tell the specific requester to initialize */

        ld->ld_Command = LDCMD_INIT;

        initialized = CallHookPkt(&(reqinfo->GadgetryHook), ld, ASLB(AslBase));
        if (initialized && !ld->ld_GenerationFailed)
        {
            struct NewWindow nw;
            struct ScaleWindowGeometry scaleGeometry;
            struct TagItem   wintags[] =
            {
                {WA_CustomScreen        , (IPTR)ld->ld_Screen       }, /* stegerg: requesters should not use WA_PubScreen */
                {WA_AutoAdjust          , TRUE                      },
                {WA_NewLookMenus        , TRUE                      },
                {WA_ScaleAware, ld->ld_ScaleAware},
                {WA_RenderContext, (IPTR)ld->ld_RenderContext},
                {WA_Hidden, TRUE},
                {WA_ScaleGeometry, 0},
            #if AVOID_FLICKER
                {WA_BackFill            , (IPTR)LAYERS_NOBACKFILL   },
            #endif
                {TAG_DONE                                           }
            };
            ULONG           idcmp;
            BOOL            privateidcmp;

            memset(&nw, 0L, sizeof (struct NewWindow));

            /* Size */
            if (intreq->ir_Flags & IF_SIZE_REL)
            {
                nw.Width    = (ld->ld_Screen->ViewPort.DWidth / (ld->ld_ScaleAware ? 1 : ld->ld_RenderContext->src_LegacyScaleFactor)) * ASLB(AslBase)->Prefs.ap_RelativeWidth / 100;
                nw.Height   = (ld->ld_Screen->ViewPort.DHeight / (ld->ld_ScaleAware ? 1 : ld->ld_RenderContext->src_LegacyScaleFactor)) * ASLB(AslBase)->Prefs.ap_RelativeHeight / 100;
                /* Used only once. Next time requester is opened, it will remember it positions and size */
                intreq->ir_Flags &= ~IF_SIZE_REL;
            }
            else
            {
                nw.Width    = AslWindowValue(ld, intreq->ir_Width);
                nw.Height   = AslWindowValue(ld, intreq->ir_Height);
            }

            if (nw.Width < ld->ld_MinWidth) nw.Width = ld->ld_MinWidth;
            if (nw.Height < ld->ld_MinHeight) nw.Height = ld->ld_MinHeight;

            /* Position: the program's, else the one Asl prefs chose */
            PrefsPosition(intreq, ld, &nw, ASLB(AslBase));
            if (intreq->ir_LeftEdge >= 0)
                nw.LeftEdge = AslWindowValue(ld, intreq->ir_LeftEdge);
            if (intreq->ir_TopEdge >= 0)
                nw.TopEdge = AslWindowValue(ld, intreq->ir_TopEdge);

            D(bug("MinWidth: %d, MinHeight: %d\n", ld->ld_MinWidth, ld->ld_MinHeight));

            nw.Title = intreq->ir_TitleText;
            if (!nw.Title)
            {
                nw.Title = GetString(intreq->ir_TitleID, intreq->ir_Catalog, ASLB(AslBase));
            }

            D(bug("\tWindow title: %s", nw.Title));

/*          nw.FirstGadget      = ld->ld_GList;
*/
            nw.Flags    =  WFLG_DRAGBAR    | WFLG_DEPTHGADGET   | WFLG_CLOSEGADGET    |
                           WFLG_SIZEGADGET | WFLG_SIZEBBOTTOM   | WFLG_SIMPLE_REFRESH |
                           WFLG_NOCAREREFRESH;

            /* Binding108 requires a hidden, nonactivating constructor. */


            idcmp = IDCMP_CLOSEWINDOW | IDCMP_GADGETUP      | IDCMP_MOUSEMOVE  |
                    IDCMP_NEWSIZE     | IDCMP_REFRESHWINDOW | IDCMP_GADGETDOWN |
                    IDCMP_MENUPICK    | IDCMP_RAWKEY        | IDCMP_VANILLAKEY |
                    IDCMP_MOUSEBUTTONS | IDCMP_CHANGESCALE |
                    (ld->ld_ScaleAware ? IDCMP_CHANGEWINDOW : 0);


            privateidcmp = ((intreq->ir_Flags & IF_PRIVATEIDCMP) || (!intreq->ir_Window));
            if (privateidcmp)
            {
                nw.IDCMPFlags = idcmp;
            }

            if (ld->ld_ScaleAware)
            {
                AslPrepareWindowGeometry(ld, &nw, &scaleGeometry);
                FindTagItem(WA_ScaleGeometry, wintags)->ti_Data = (IPTR)&scaleGeometry;
            }
            else FindTagItem(WA_ScaleGeometry, wintags)->ti_Tag = TAG_IGNORE;
            win = OpenWindowTagList(&nw, wintags);
            if (win)
            {
                struct Window *oldwinptr = (struct Window *) -1;
                struct Process *Self;

                if (!privateidcmp)
                {
                    win->UserPort = intreq->ir_Window->UserPort;
                    ModifyIDCMP(win, idcmp);
                }

                ld->ld_Window = win;
                ld->ld_ReqNode = reqnode;

                ObtainSemaphore( &(ASLB(AslBase)->ReqListSem) );
                reqnode->rn_ReqWindow = win;
                ReleaseSemaphore(&(ASLB(AslBase)->ReqListSem));

                D(bug("Window opened\n"));

                D(bug
                (
                    "\tLeft: %d\n\tTop: %d\n\tWidth: %d\n\tHeight: %d\n",
                    win->LeftEdge,
                    win->TopEdge,
                    win->Width,
                    win->Height
                 ));


                /* Constraint the window minsize */

                WindowLimits
                (
                    win,
                    ld->ld_MinWidth,
                    ld->ld_MinHeight,
                    ~0,
                    ~0
                );

                D(bug("Window limits set\n"));

                SetFont(win->RPort, ld->ld_Font);


                /* Set process windowpointer to catch possible requesters
                    to correct screen & relative to requester window. - Piru
                    */
                Self = (struct Process *) FindTask(NULL);
                if (Self->pr_WindowPtr != (struct Window *) -1)
                {
                    oldwinptr = Self->pr_WindowPtr;
                    Self->pr_WindowPtr = win;
                }


                /* Layout the requester */
                if (AslRelayout(ld, reqinfo, ASLB(AslBase))) ld->ld_Command = LDCMD_LAYOUT;
                else ld->ld_GenerationFailed = TRUE;
                if (!ld->ld_GenerationFailed && CallHookPkt(&(reqinfo->GadgetryHook), ld, ASLB(AslBase)))
                {
                    D(bug("Gadgetry layout done\n"));

                    if (ld->ld_GList)
                    {
                        D(bug("Adding glist\n"));
                        AddGList(win, ld->ld_GList, -1, -1, NULL);
                        ld->ld_GadgetsAttached = TRUE;
                        D(bug("Refreshing glist\n"));
                        RefreshGList(ld->ld_GList, win, NULL, -1);

                        D(bug("Gadgetlist refreshed\n"));
                    }

                    if (ld->ld_Menu) SetMenuStripContext(win, ld->ld_Menu, ld->ld_RenderContext, !ld->ld_ScaleAware);

                    ld->ld_Command = LDCMD_WINDOWOPENED;
                    CallHookPkt(&(reqinfo->GadgetryHook), ld, ASLB(AslBase));

                    if (intreq->ir_Flags & IF_POPTOFRONT)
                    {
                        struct Screen   *frontscr;
                        ULONG            ilock;

                        ilock = LockIBase(0);
                        frontscr = IntuitionBase->FirstScreen;
                        UnlockIBase(ilock);

                        if (frontscr != win->WScreen)
                        {
                            ScreenToFront(win->WScreen);
                            intreq->ir_Flags |= IF_POPPEDTOFRONT;
                        }
                    }

                    /* Wait for the user to do something */
                    if (ShowWindow(win, NULL))
                    {
                        if (!(intreq->ir_Flags & IF_OPENINACTIVE)) ActivateWindow(win);
                        success = HandleEvents(ld, reqinfo, ASLB(AslBase));
                    }
                    else SetIoErr(ERROR_OBJECT_IN_USE);

                    /* Finished with event handling, clean up requester
                    specific stuff */



                } /* if (CallHookPkt(&(reqinfo->GadgetryHook), ld, ASLB(AslBase))) */

                if (oldwinptr != (struct Window *) -1)
                {
                    Self->pr_WindowPtr = oldwinptr;
                }

                /* win is closed in FreeCommon */

            } /* if (win) */

        } /* if (CallHookPkt( &(reqinfo->GadgetryHook), ld, ASLB(AslBase))) */

        ObtainSemaphore( &(ASLB(AslBase)->ReqListSem) );
        reqnode->rn_ReqWindow = NULL;
        ReleaseSemaphore(&(ASLB(AslBase)->ReqListSem));

        if (initialized)
        {
            ld->ld_Command = LDCMD_CLEANUP;
            CallHookPkt(&reqinfo->GadgetryHook, ld, ASLB(AslBase));
        }
        FreeCommon(ld, ASLB(AslBase));

    } /* if (ld) */

    ReturnBool ("AslRequest", success);

    AROS_LIBFUNC_EXIT
} /* AslRequest */
