/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    Desc:
*/

#define DEBUG 0
#include <aros/debug.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <proto/gadtools.h>
#include <proto/asl.h>

#include <exec/memory.h>
#include <dos/dos.h>
#include <utility/utility.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <devices/rawkeycodes.h>

#include "con_handler_intern.h"
#include "support.h"
#include "completion.h"

#include <string.h>
#include <stdlib.h>

/****************************************************************************************/

struct matchnode
{
    struct Node node;
    UBYTE       name[1];
};

/****************************************************************************************/

struct completioninfo
{
    struct filehandle   *fh;
    struct List         matchlist;
    APTR                pool;
    WORD                nummatchnodes;
    WORD                wordstart;
    BOOL                wordquoted;
    UBYTE               filepart[256];
    UBYTE               dirpart[256];
    UBYTE               pattern[256 * 2 + 3];
    UBYTE               match[256];
    BOOL                withinfo;
};

/****************************************************************************************/

/* Delay opening of the gadtools.library to the first time InitCompletion is called */

static struct completioninfo *InitCompletion(struct filehandle *fh, BOOL withinfo)
{
    struct completioninfo *ci = NULL;
    APTR pool;

    if (fh->gtbase == NULL)
        fh->gtbase = TaggedOpenLibrary(TAGGEDOPEN_GADTOOLS);

    if (fh->gfxbase == NULL)
        fh->gfxbase = (APTR)TaggedOpenLibrary(TAGGEDOPEN_GRAPHICS);

    if (fh->lastwritetask && GadToolsBase)
        if (fh->lastwritetask->tc_Node.ln_Type == NT_PROCESS)
        {
            if ((pool = CreatePool(MEMF_CLEAR, 1024, 1024)))
            {
                BOOL ok = FALSE;

                if ((ci = (struct completioninfo *) AllocPooled(pool, sizeof(*ci))))
                {
                    ci->pool = pool;
                    ci->fh = fh;
                    ci->withinfo = withinfo;
                    NewList(&ci->matchlist);

                    ok = TRUE;
                }

                if (!ok)
                {
                    DeletePool(pool);
                    ci = NULL;
                }
            }
        }

    return ci;
}

/****************************************************************************************/

static void CleanupCompletion(struct completioninfo *ci)
{
    DeletePool(ci->pool);
}

/****************************************************************************************/

static BOOL PrepareCompletion(struct filehandle *fh, struct completioninfo *ci)
{
    WORD i;
    ULONG len;
    UBYTE c;
    BOOL in_quotes = FALSE;
    BOOL escaped = FALSE;

    /* Find word start */

    ci->wordstart = i = ci->fh->inputstart;

    while (i != ci->fh->inputpos)
    {
        c = ci->fh->inputbuffer[i++];

        switch (c)
        {
        case '"':
            if (!escaped)
            {
                in_quotes = !in_quotes;
                if (in_quotes)
                    ci->wordstart = i;
            }
            break;

        case ' ':
        case '>':
        case '<':
            if (!in_quotes && !escaped)
                ci->wordstart = i;
            break;
        }

        if (c == '*' && !escaped)
            escaped = TRUE;
        else
            escaped = FALSE;
    }

    len = ci->fh->inputpos - ci->wordstart;
    if (len >= sizeof(ci->dirpart))
        return FALSE;

    CopyMem(&ci->fh->inputbuffer[ci->wordstart], ci->dirpart, len);
    ci->dirpart[len] = '\0';
    strcpy(ci->filepart, FilePart(ci->dirpart));

    *(PathPart(ci->dirpart)) = '\0';

    ci->wordquoted = in_quotes;

    D(bug("[con:handler] %s: dirpart = \"%s\"  filepart = \"%s\"\n", __func__, ci->dirpart, ci->filepart));

    return TRUE;
}

/****************************************************************************************/

static BOOL AddQuotes(struct completioninfo *ci, STRPTR s, LONG s_size)
{
    size_t len;
    BOOL closequote;
    size_t needed;

    if (s_size <= 0)
        return FALSE;

    len = strnlen(s, (size_t)s_size);
    if ((len == 0) || (len == (size_t)s_size))
        return FALSE;

    closequote = (s[len - 1] != '/') && (s[len - 1] != ':');
    needed = len + 1 + (ci->wordquoted ? 0 : 1) + (closequote ? 1 : 0);
    if (needed > (size_t)s_size)
        return FALSE;

    if (!ci->wordquoted)
    {
        memmove(s + 1, s, len + 1);
        s[0] = '"';
        len++;
    }

    if (closequote)
    {
        s[len] = '"';
        s[len + 1] = '\0';
    }

    return TRUE;
}

/****************************************************************************************/

static void InsertIntoConBuffer(struct completioninfo *ci, STRPTR s)
{
    WORD i = ci->fh->conbufferpos;

    /* Lame code, but speed is no issue here */
    while ((ci->fh->conbuffersize < CONSOLEBUFFER_SIZE) && *s)
    {
        memmove(&ci->fh->consolebuffer[i + 1], &ci->fh->consolebuffer[i], ci->fh->conbuffersize - i);

        ci->fh->consolebuffer[i++] = *s++;
        ci->fh->conbuffersize++;
    }
}

/****************************************************************************************/

static void DoFileReq(struct filehandle *fh, struct completioninfo *ci)
{
    struct Library *AslBase;
    BPTR lock, olddir;

    if ((lock = DupLock(((struct Process *) ci->fh->lastwritetask)->pr_CurrentDir)))
    {
        olddir = CurrentDir(lock);

        if ((AslBase = OpenLibrary("asl.library", 36)))
        {
            struct FileRequester *fr;
            struct TagItem tags[] =
            {
            { ASLFR_Window, (IPTR) ci->fh->window },
            { ASLFR_DoPatterns, (IPTR) TRUE },
            { ASLFR_InitialPattern, 0 },
            { TAG_DONE } };

            tags[2].ti_Data = (IPTR) (ci->withinfo ? "#?" : "~(#?.info)");

            if ((fr = AllocAslRequest(ASL_FileRequest, tags)))
            {
                if (AslRequest(fr, NULL))
                {
                    UBYTE c;

                    if (Strlcpy(ci->match, fr->fr_Drawer, sizeof(ci->match)) >= sizeof(ci->match))
                        ci->match[0] = '\0';
                    else
                        AddPart(ci->match, fr->fr_File, sizeof(ci->match));

                    if (ci->match[0])
                    {
                        if (!strchr(ci->match, ' ') ||
                            AddQuotes(ci, ci->match, sizeof(ci->match)))
                        {
                            c = ci->match[strlen(ci->match) - 1];
                            if ((c != '/') && (c != ':'))
                            {
                                Strlcat(ci->match, " ", sizeof(ci->match));
                            }

                            InsertIntoConBuffer(ci, ci->match);
                        }
                    }

                }
                FreeAslRequest(fr);
            }

            CloseLibrary(AslBase);

        } /* if ((AslBase = OpenLibrary("asl.library", 36))) */

        CurrentDir(olddir);
        UnLock(lock);
    }
}

/****************************************************************************************/

static BOOL PreparePattern(struct filehandle *fh, struct completioninfo *ci)
{
    WORD parsecode;

    parsecode = ParsePatternNoCase(ci->filepart, ci->pattern, sizeof(ci->pattern));
    if (parsecode != -1)
    {
        if (parsecode == 0)
        {
            CONST_STRPTR suffix = ci->withinfo ? "#?" : "~(#?.info)";

            if (Strlcat(ci->filepart, suffix, sizeof(ci->filepart)) >= sizeof(ci->filepart))
                return FALSE;

            parsecode = ParsePatternNoCase(ci->filepart, ci->pattern, sizeof(ci->pattern));
        }
    }

    return (parsecode == 1);

}

/****************************************************************************************/

static void AddMatchNode(struct filehandle *fh, struct completioninfo *ci, STRPTR name, WORD type)
{
    struct matchnode *mn;
    struct Node *prev, *check;
    WORD size;
    BOOL exists = FALSE;

    size = strlen(name) + 1 + sizeof(struct matchnode) + ((type > 0) ? 1 : 0);

    if ((mn = AllocPooled(ci->pool, size)))
    {
        strcpy(mn->name, name);
        mn->node.ln_Name = mn->name;

        if (type == 1)
            strcat(mn->name, "/");
        if (type == 2)
            strcat(mn->name, ":");

        /* Sort into matchlist */

        prev = NULL;
        ForeachNode(&ci->matchlist, check)
        {
            WORD match;

            match = Stricmp(mn->name, ((struct matchnode *) check)->name);
            if (match < 0)
                break;
            if (match == 0)
            {
                exists = TRUE;
                break;
            }

            prev = check;
        }

        if (!exists)
        {
            Insert(&ci->matchlist, (struct Node *) mn, prev);
            ci->nummatchnodes++;
        }
        else
        {
            FreePooled(ci->pool, mn, size);
        }
    }
}

/****************************************************************************************/

static void ScanDir(struct filehandle *fh, struct completioninfo *ci)
{
    struct FileInfoBlock *fib;
    BPTR lock;

    if ((fib = AllocDosObject(DOS_FIB, 0)))
    {
        if ((lock = Lock(ci->dirpart, SHARED_LOCK)))
        {
            if (Examine(lock, fib))
            {
                while (ExNext(lock, fib))
                {
                    if (MatchPatternNoCase(ci->pattern, fib->fib_FileName))
                    {
                        BOOL isdir = (fib->fib_DirEntryType > 0);

                        AddMatchNode(fh, ci, fib->fib_FileName, (isdir ? 1 : 0));
                    }
                }
            }

            UnLock(lock);
        }

        FreeDosObject(DOS_FIB, fib);
    }
}

/****************************************************************************************/

static void ScanVol(struct filehandle *fh, struct completioninfo *ci)
{
    struct DosList *dlist;

    dlist = LockDosList(LDF_READ | LDF_VOLUMES | LDF_DEVICES | LDF_ASSIGNS);

    while ((dlist = NextDosEntry(dlist, LDF_VOLUMES | LDF_ASSIGNS | LDF_DEVICES)) != NULL)
    {
        STRPTR devname = AROS_BSTR_ADDR(dlist->dol_Name);

        if (MatchPatternNoCase(ci->pattern, devname))
        {
            AddMatchNode(fh, ci, devname, 2);

        }
    }
    UnLockDosList(LDF_READ | LDF_VOLUMES | LDF_DEVICES | LDF_ASSIGNS);
}

static void DoScan(struct filehandle *fh, struct completioninfo *ci)
{
    BPTR lock, olddir;

    if ((lock = DupLock(((struct Process *) ci->fh->lastwritetask)->pr_CurrentDir)))
    {
        olddir = CurrentDir(lock);

        if (ci->dirpart[0] == 0)
            ScanVol(fh, ci);
        ScanDir(fh, ci);

        CurrentDir(olddir);
        UnLock(lock);
    }
}

/****************************************************************************************/

#define BUTTON_EXTRA_WIDTH      16
#define BUTTON_EXTRA_HEIGHT     6
#define BORDER_X                4
#define BORDER_Y                4
#define BUTTON_SPACING_X        8
#define LV_BUTTON_SPACING_Y     4
#define LV_EXTRA_HEIGHT         4

#define ID_LISTVIEW 1
#define ID_OK       2
#define ID_CANCEL   3

/****************************************************************************************/

/* Completion is an unadapted logical100 dialog, independently of CON's
   sharp physical rendering. Convert physical pointer/viewport once. */
static LONG completion_logical_coord(LONG physical, ULONG factor)
{
    return physical < 0 ? -(LONG)((-(QUAD)physical + factor - 1) / factor) :
        physical / factor;
}

static APTR completion_visual_info(struct filehandle *fh, struct Screen *screen,
    const struct ScreenRenderContext **captured)
{
    IPTR value = 0;
    const struct ScreenRenderContext *context;
    struct TagItem tags[] = {
        { GTVI_RenderContext, 0 }, { GTVI_Logical, TRUE }, { TAG_DONE, 0 }
    };
    APTR vi;
    *captured = NULL;
    if (!GetAttr(SA_RenderContext, (Object *)screen, &value) || !value) return NULL;
    context = (const struct ScreenRenderContext *)value;
    if (!context->src_LogicalDrawInfo || !context->src_PhysicalDrawInfo ||
        context->src_LogicalDrawInfo->dri_Screen != screen ||
        context->src_PhysicalDrawInfo->dri_Screen != screen ||
        context->src_LegacyScaleFactor < 1 || context->src_LegacyScaleFactor > 4)
        return NULL;
    tags[0].ti_Data = value;
    vi = GetVisualInfoA(screen, tags);
    if (vi) *captured = context;
    return vi;
}

static struct Window *completion_open_window(struct filehandle *fh, struct Screen *screen,
    const struct ScreenRenderContext *context, LONG winleft, LONG wintop,
    LONG winwidth, LONG winheight)
{
    struct TagItem wintags[] =
    {
        { WA_CustomScreen, (IPTR) screen   },
        { WA_Title, (IPTR) "Select filename"                        },
        { WA_CloseGadget, TRUE                              },
        { WA_DragBar, TRUE                                  },
        { WA_DepthGadget, TRUE                              },
        { WA_Activate, FALSE                                },
        { WA_AutoAdjust, TRUE                               },
        { WA_Left, winleft                                  },
        { WA_Top, wintop                                    },
        { WA_InnerWidth, winwidth                           },
        { WA_InnerHeight, winheight                         },
        { WA_IDCMP, IDCMP_CHANGESCALE | IDCMP_CLOSEWINDOW | IDCMP_RAWKEY | IDCMP_VANILLAKEY | LISTVIEWIDCMP | BUTTONIDCMP },
        { WA_Hidden, TRUE },
        { WA_ScaleAware, FALSE },
        { WA_RenderContext, (IPTR)context },
        { TAG_DONE                                          }
    };

    return OpenWindowTagList(NULL, wintags);
}

static BOOL completion_publish_window(struct filehandle *fh, struct Window *window, struct Gadget *gadlist)
{
    if (AddGList(window, gadlist, (UWORD)-1, (UWORD)-1, NULL) == (UWORD)-1)
        return FALSE;
    RefreshGList(gadlist, window, NULL, (UWORD)-1);
    GT_RefreshWindow(window, NULL);
    if (!ShowWindow(window, NULL)) return FALSE;
    ActivateWindow(window);
    return TRUE;
}

static BOOL DoChooseReq(struct filehandle *fh, struct completioninfo *ci)
{
    static const char oktext[] = "Ok";
    static const char canceltext[] = "Cancel";

    struct RastPort temprp;
    struct DrawInfo *dri;
    const struct ScreenRenderContext *context;
    struct Window *win;
    struct Gadget *gadlist, *gad, *lvgad;
    struct NewGadget ng;
    APTR vi;
    WORD i, buttonwidth, buttonheight, visible_lv_lines;
    WORD winwidth, winheight, winleft, wintop;
    LONG sec, micro, secold, microold;
    BOOL retval = FALSE;

    if ((vi = completion_visual_info(fh, ci->fh->window->WScreen, &context)))
    {
        dri = context->src_LogicalDrawInfo;
        InitRastPort(&temprp);
        SetFont(&temprp, dri->dri_Font);

        buttonwidth = TextLength(&temprp, oktext, strlen(oktext));
        i = TextLength(&temprp, canceltext, strlen(canceltext));
        buttonwidth = (buttonwidth > i) ? buttonwidth : i;
        buttonwidth += BUTTON_EXTRA_WIDTH;

        buttonheight = dri->dri_Font->tf_YSize + BUTTON_EXTRA_HEIGHT;

        i = ci->nummatchnodes > 15 ? 15 : ci->nummatchnodes;
        if (i < 4)
            i = 4;

        winheight = i * (dri->dri_Font->tf_YSize + 1) + LV_EXTRA_HEIGHT + LV_BUTTON_SPACING_Y + buttonheight
                + BORDER_Y * 2;

        winwidth = buttonwidth * 2 + BUTTON_SPACING_X + BORDER_X * 2;
        i = ci->fh->window->WScreen->Width / context->src_LegacyScaleFactor / 3;
        if (i > winwidth)
            winwidth = i;

        winleft = completion_logical_coord(ci->fh->window->WScreen->MouseX,
            context->src_LegacyScaleFactor) - winwidth / 2;
        wintop = completion_logical_coord(ci->fh->window->WScreen->MouseY,
            context->src_LegacyScaleFactor) - winheight / 2;

        win = completion_open_window(fh, ci->fh->window->WScreen, context,
            winleft, wintop, winwidth, winheight);
        if (!win) goto done_visual;

        gadlist = NULL;
        gad = CreateContext(&gadlist);
        if (!gad) goto done_window;
        winwidth = win->Width - win->BorderLeft - win->BorderRight;
        winheight = win->Height - win->BorderTop - win->BorderBottom;

        ng.ng_LeftEdge = win->BorderLeft + BORDER_X;
        ng.ng_TopEdge = win->BorderTop + BORDER_Y;
        ng.ng_Width = winwidth - BORDER_X * 2;
        ng.ng_Height = winheight - BORDER_Y * 2 - buttonheight - LV_BUTTON_SPACING_Y;
        ng.ng_GadgetText = NULL;
        ng.ng_TextAttr = NULL;
        ng.ng_GadgetID = ID_LISTVIEW;
        ng.ng_Flags = 0;
        ng.ng_VisualInfo = vi;
        ng.ng_UserData = 0;

        {
            struct TagItem lvtags[] =
            {
            { GTLV_Labels, (IPTR) &ci->matchlist },
            { GTLV_ShowSelected, 0 },
            { GTLV_Selected, 0 },
            { TAG_DONE } };

            gad = lvgad = CreateGadgetA(LISTVIEW_KIND, gad, &ng, lvtags);

            if (!gad) goto done_window;
            visible_lv_lines = (ng.ng_Height - LV_EXTRA_HEIGHT) / (dri->dri_Font->tf_YSize + 1);
        }

        ng.ng_TopEdge += ng.ng_Height + LV_BUTTON_SPACING_Y;
        ng.ng_Width = buttonwidth;
        ng.ng_Height = buttonheight;
        ng.ng_GadgetText = oktext;
        ng.ng_GadgetID = ID_OK;

        gad = CreateGadgetA(BUTTON_KIND, gad, &ng, NULL);
        if (!gad) goto done_window;

        ng.ng_LeftEdge += winwidth - buttonwidth - BORDER_X * 2;
        ng.ng_GadgetText = canceltext;
        ng.ng_GadgetID = ID_CANCEL;

        gad = CreateGadgetA(BUTTON_KIND, gad, &ng, NULL);

        if (gad)
        {
            if (completion_publish_window(fh, win, gadlist))
            {
                BOOL done = FALSE;
                BOOL doit = FALSE;
                BOOL scalechanged = FALSE;

                CurrentTime(&secold, &microold);

                while (!done && !doit)
                {
                    struct IntuiMessage *msg;

                    WaitPort(win->UserPort);

                    while ((msg = GT_GetIMsg(win->UserPort)))
                    {
                        if (scalechanged) { GT_ReplyIMsg(msg); continue; }
                        switch (msg->Class)
                        {
                        case IDCMP_CHANGESCALE:
                            scalechanged = done = TRUE;
                            doit = FALSE;
                            break;
                        case IDCMP_CLOSEWINDOW:
                            done = TRUE;
                            break;

                        case IDCMP_VANILLAKEY:
                            switch (msg->Code)
                            {
                            case 27:
                                done = TRUE;
                                break;

                            case 13:
                                doit = TRUE;
                                break;
                            }
                            break;

                        case IDCMP_RAWKEY:
                        {
                            WORD scroll = 0;
                            BOOL page = FALSE;
                            BOOL extreme = FALSE;

                            switch (msg->Code)
                            {
                            case RAWKEY_UP:
                                scroll = -1;
                                break;

                            case RAWKEY_DOWN:
                                scroll = 1;
                                break;

                            case RAWKEY_PAGEUP:
                                scroll = -1;
                                page = TRUE;
                                break;

                            case RAWKEY_PAGEDOWN:
                                scroll = 1;
                                page = TRUE;
                                break;

                            case RAWKEY_HOME:
                                scroll = -1;
                                extreme = TRUE;
                                break;

                            case RAWKEY_END:
                                scroll = 1;
                                extreme = TRUE;
                                break;

                            case RAWKEY_NM_WHEEL_UP:
                                scroll = -1;
                                break;

                            case RAWKEY_NM_WHEEL_DOWN:
                                scroll = 1;
                                break;
                            }

                            if (msg->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
                            {
                                page = TRUE;
                            }

                            if (msg->Qualifier & (IEQUALIFIER_LALT | IEQUALIFIER_RALT | IEQUALIFIER_CONTROL))
                            {
                                extreme = TRUE;
                            }

                            if (scroll)
                            {
                                IPTR getsel;
                                struct TagItem tags[] =
                                {
                                { GTLV_Selected, (IPTR) &getsel },
                                { TAG_IGNORE, TRUE },
                                { TAG_DONE } };
                                WORD sel;

                                GT_GetGadgetAttrsA(lvgad, win, NULL, tags);
                                sel = (WORD) getsel;

                                if (extreme)
                                {
                                    scroll *= ci->nummatchnodes;
                                }
                                else if (page)
                                {
                                    scroll *= visible_lv_lines;
                                }

                                sel += scroll;
                                if (sel < 0)
                                    sel = ci->nummatchnodes - 1;
                                if (sel >= ci->nummatchnodes)
                                    sel = 0;

                                tags[0].ti_Data = (IPTR) sel;
                                tags[1].ti_Tag = GTLV_MakeVisible;
                                tags[1].ti_Data = (IPTR) sel;

                                GT_SetGadgetAttrsA(lvgad, win, NULL, tags);
                            }
                        }
                            break;

                        case IDCMP_GADGETUP:
                            gad = (struct Gadget *) msg->IAddress;

                            switch (gad->GadgetID)
                            {
                            case ID_OK:
                                doit = TRUE;
                                break;

                            case ID_CANCEL:
                                done = TRUE;
                                break;

                            case ID_LISTVIEW:
                                CurrentTime(&sec, &micro);
                                if (DoubleClick(secold, microold, sec, micro))
                                    doit = TRUE;
                                secold = sec;
                                microold = micro;
                                break;
                            }
                            break;

                        } /* switch(msg->Class) */

                        GT_ReplyIMsg(msg);

                    } /* while((msg = GT_GetIMsg(win->UserPort))) */

                } /* while (!done && !doit) */

                if (doit)
                {
                    struct Node *node;
                    IPTR sel;

                    struct TagItem gettags[] =
                    {
                    { GTLV_Selected, (IPTR) &sel },
                    { TAG_DONE } };

                    GT_GetGadgetAttrsA(lvgad, win, NULL, gettags);

                    i = 0;
                    ForeachNode(&ci->matchlist, node)
                    {
                        if ((WORD) sel == i)
                        {
                            AddPart(ci->match, node->ln_Name, sizeof(ci->match));

                            retval = TRUE;
                            break;
                        }
                        i++;
                    }

                } /* if (doit) */


            } /* completion published */

        } /* if (gad) */

done_window:
        CloseWindow(win);
        FreeGadgets(gadlist);
done_visual:
        DeinitRastPort(&temprp);
        FreeVisualInfo(vi);

    } /* borrowed context remains owned by the Screen */

    return retval;
}

/****************************************************************************************/

void Completion(struct filehandle *fh, BOOL withinfo)
{
    struct completioninfo *ci;

    if ((ci = InitCompletion(fh, withinfo)))
    {
        if (!PrepareCompletion(fh, ci))
        {
            CleanupCompletion(ci);
            return;
        }

        if (!ci->dirpart[0] && !ci->filepart[0])
        {
            /* With nothing typed yet there is nothing to expand, and all that
               is left to offer is a file requester. A console running over a
               device has no window to put one over, so leave the line alone
               rather than opening a requester somewhere the user is not. */
            if (fh->window)
                DoFileReq(fh, ci);
        }
        else
        {
            if (PreparePattern(fh, ci))
            {
                BOOL doprint = FALSE;

                DoScan(fh, ci);

                strncpy(ci->match, ci->dirpart, sizeof(ci->match));

                if (ci->nummatchnodes == 1)
                {
                    AddPart(ci->match, ((struct matchnode *) GetHead(&ci->matchlist))->name, sizeof(ci->match));

                    doprint = TRUE;
                }
                else if (ci->nummatchnodes > 1 && fh->window)
                {
                    /* Choosing between matches is a windowed requester, and it
                       reads the screen out of fh->window. */
                    doprint = DoChooseReq(fh, ci);
                }

                if (doprint)
                {
                    WORD backspaces;
                    size_t matchlen;
                    UBYTE c;

                    if ((ci->wordquoted || strchr(ci->match, ' ')) &&
                        !AddQuotes(ci, ci->match, sizeof(ci->match)))
                    {
                        CleanupCompletion(ci);
                        return;
                    }

                    /* Insert as many backspaces in front of the string,
                     to erase whole "word" first (starting at ci->wordstart)
                     before reprinting expanded filename */

                    backspaces = ci->fh->inputpos - ci->wordstart;
                    matchlen = strnlen(ci->match, sizeof(ci->match));

                    if ((matchlen == sizeof(ci->match)) ||
                        ((size_t)backspaces + matchlen >= sizeof(ci->match)))
                    {
                        CleanupCompletion(ci);
                        return;
                    }

                    memmove(ci->match + backspaces, ci->match, matchlen + 1);
                    SetMem(ci->match, 8, backspaces);

                    c = ci->match[strlen(ci->match) - 1];
                    if ((c != '/') && (c != ':'))
                    {
                        Strlcat(ci->match, " ", sizeof(ci->match));
                    }

                    InsertIntoConBuffer(ci, ci->match);

                }
            }
        }

        CleanupCompletion(ci);

    } /* if ((ci = InitCompletion())) */

}

/****************************************************************************************/
