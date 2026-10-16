/*
    Copyright (C) 1995-2020, The AROS Development Team. All rights reserved.

    Initialization of workbench.library.
*/

#include <aros/debug.h>
#include <aros/atomic.h>
#include <aros/symbolsets.h>

#include <string.h>

#include "workbench_intern.h"
#include LC_LIBDEFS_FILE
#include "handler.h"
#include "support.h"
#include "tile.h"
#define __TASK_NOLIBBASE__
#include <proto/task.h>
#include <resources/task.h>

AROS_UFH3(IPTR, TileOwnerHook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(struct Task *, task, A2),
    AROS_UFHA(struct TaskNotifyMsg *, message, A1))
{
    AROS_USERFUNC_INIT
    if (message->tnm_Action == TNA_REMOVED)
        WBTileOwnerRemoved(task, hook->h_Data);
    return 0;
    AROS_USERFUNC_EXIT
}


static int WBInit(LIBBASETYPEPTR LIBBASE)
{
    /* Make sure that the libraries are opened in L_OpenLib() --------------*/
    WorkbenchBase->wb_Initialized = FALSE;

    /* Initialize our private lists ----------------------------------------*/
    NEWLIST(&(WorkbenchBase->wb_AppWindows));
    NEWLIST(&(WorkbenchBase->wb_AppIcons));
    NEWLIST(&(WorkbenchBase->wb_AppMenuItems));
    NEWLIST(&(WorkbenchBase->wb_HiddenDevices));

    /* Initialize our semaphores -------------------------------------------*/
    InitSemaphore(&(WorkbenchBase->wb_WorkbenchPortSemaphore));
    InitSemaphore(&(WorkbenchBase->wb_InitializationSemaphore));
    InitSemaphore(&(WorkbenchBase->wb_BaseSemaphore));
    
    /* Initialize handler message port -------------------------------------*/
    memset( &WorkbenchBase->wb_HandlerPort, 0, sizeof( WorkbenchBase->wb_HandlerPort ) );
    WorkbenchBase->wb_HandlerPort.mp_SigBit  = SIGBREAKB_CTRL_F;
    WorkbenchBase->wb_HandlerPort.mp_Flags   = PA_IGNORE;
    NEWLIST(&(WorkbenchBase->wb_HandlerPort.mp_MsgList));

    /* Tile replies outlive their application and Wanderer. No task signal. */
    WorkbenchBase->wb_TileReplyPort.mp_Flags = PA_IGNORE;
    NEWLIST(&WorkbenchBase->wb_TileReplyPort.mp_MsgList);

    /* Initialize miscellanous variables -----------------------------------*/
    WorkbenchBase->wb_DefaultStackSize = AROS_STACKSIZE;
D(bug("[WBLIB] WBInit: Using %d Bytes for DefaultStackSize\n", WorkbenchBase->wb_DefaultStackSize));

    return TRUE;
}

/* REMOVE THESE CALLS ONCE ABIv1 STABALIZES */
static BOOL openall(LIBBASETYPEPTR LIBBASE)
{
    if ((UtilityBase = TaggedOpenLibrary(TAGGEDOPEN_UTILITY))) {
        if ((DOSBase = TaggedOpenLibrary(TAGGEDOPEN_DOS))) {
            if ((IntuitionBase = TaggedOpenLibrary(TAGGEDOPEN_INTUITION))) {
                if ((IconBase = TaggedOpenLibrary(TAGGEDOPEN_ICON))) {
                    return TRUE;
                }
                CloseLibrary(IntuitionBase);
            }
            CloseLibrary(DOSBase);
        }
        CloseLibrary(UtilityBase);
    }
    return FALSE;
}

static void closeall(LIBBASETYPEPTR LIBBASE)
{
    CloseLibrary(IconBase);
    CloseLibrary(IntuitionBase);
    CloseLibrary(DOSBase);
    CloseLibrary(UtilityBase);
}

static int WBOpen(LIBBASETYPEPTR LIBBASE)
{
    D(bug("[WBLIB] Open\n"));
    ObtainSemaphore(&(WorkbenchBase->wb_InitializationSemaphore));

    while (!(WorkbenchBase->wb_Initialized))
    {
        struct CommandLineInterface *cli;

        /* Initialize the libraries */
        if (!openall(LIBBASE))
            break;
        
        /* Duplicate the search path ---------------------------------------*/
        if ((cli = Cli()) != NULL)
        {
            WorkbenchBase->wb_SearchPath = DuplicateSearchPath
            (
                cli->cli_CommandDir
            );
        }
    
        const struct TagItem         tags[]=
        {
            {NP_Entry    , (IPTR)WorkbenchHandler      },
            {NP_Name     , (IPTR)"Workbench Handler"   },
                        {NP_UserData , (IPTR)WorkbenchBase      },
                        {NP_StackSize, 8129                                },
            {TAG_DONE    , 0                               }
        };
        
        /* Start workbench handler -----------------------------------------*/
        if ((CreateNewProc(tags)) != NULL)
        {
            /* Prevent expunging while the handler is running */
            AROS_ATOMIC_INC(WorkbenchBase->LibNode.lib_OpenCnt);
        }
        else
        {
            closeall(LIBBASE);
            break;
        }
        
        /* The resident handler pins this library before the hook is installed.
         * Registration never holds wb_BaseSemaphore, avoiding notify-lock inversion. */
        {
            APTR TaskResBase = OpenResource("task.resource");
            WorkbenchBase->wb_TileOwnerHook.h_Entry = (HOOKFUNC)TileOwnerHook;
            WorkbenchBase->wb_TileOwnerHook.h_Data = WorkbenchBase;
            if (TaskResBase && AddTaskNotifyHook(&WorkbenchBase->wb_TileOwnerHook))
                WorkbenchBase->wb_TileTaskResource = TaskResBase;
        }
        WorkbenchBase->wb_Initialized = TRUE;
    }

    ReleaseSemaphore(&(WorkbenchBase->wb_InitializationSemaphore));

    return WorkbenchBase->wb_Initialized;
} /* L_OpenLib */

ADD2INITLIB(WBInit, 0);
ADD2OPENLIB(WBOpen, 0);
