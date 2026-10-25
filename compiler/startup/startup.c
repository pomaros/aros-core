
/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    $Id$

    Desc: Common startup code
    Lang: english
*/
#define DEBUG 0

#include <aros/config.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/task.h>
#include <proto/dos.h>
#include <aros/asmcall.h>
#include <aros/debug.h>
#include <aros/symbolsets.h>
#include <aros/startup.h>
#include <resources/task.h>

/*
 * the following symbols may be replaced by versions in
 * external code, so need to be declared weak
 */
extern const LONG __aros_libreq_DOSBase __attribute__((weak));
struct DosLibrary       *DOSBase __attribute__((weak));

/*
 * A program that needs a larger stack than it may be started with defines
 *
 *     ULONG __stack = 16 * 1024 * 1024;
 *
 * and runs on a stack of at least that many bytes, whatever the Shell's
 * Stack setting or the icon's tooltype. Only the low 32 bits are read, so
 * the LONG and long definitions of existing sources keep working on
 * little-endian CPUs.
 */
ULONG                   __stack __attribute__((weak)) = 0;

APTR                    _TaskResBase;


THIS_PROGRAM_HANDLES_SYMBOLSET(PROGRAM_ENTRIES)

extern int main(int argc, char ** argv);
int (*__main_function_ptr)(int argc, char ** argv) __attribute__((__weak__)) = main;

/* if the programmer hasn't defined a symbol with the name __nocommandline
   then the code to handle the commandline will be included from the autoinit.lib
*/
extern int __nocommandline;
asm(".set __importcommandline, __nocommandline");

/* if the programmer hasn't defined a symbol with the name __nostdiowin
   then the code to open a window for stdio will be included from the autoinit.lib
*/
extern int __nostdiowin;
asm(".set __importstdiowin, __nostdiowin");

/* if the programmer hasn't defined a symbol with the name __nowbsupport
   then the code to handle support for programs started from WB will be included from
   the autoinit.lib
*/
extern int __nowbsupport;
asm(".set __importnowbsupport, __nowbsupport");

/* if the programmer hasn't defined a symbol with the name __noinitexitsets
   then the code to handle support for calling the INIT, EXIT symbolset functions
   and the autoopening of libraries is called from the autoinit.lib
*/
extern int __noinitexitsets;
asm(".set __importnoinitexitsets, __noinitexitsets");

extern void __startup_entries_init(void);

static int __startup_latehook_dispatcher(struct Hook *_latehook);
static SIPTR __attribute__((noinline)) __startup_entry_body(STRPTR argstr, ULONG argsize,
    struct ExecBase *sysBase);

/*
 * Guarantee that __startup_entry is placed at the beginning of the binary.
 *
 * On darwin-aarch64 hosted builds the final module link keeps relocations for
 * .text but can drop relocations from the special .aros.startup input section
 * after merging it to the front of .text. Keep this section as a tiny
 * relocation-free trampoline; the real startup body lives in normal .text.
 */
__startup AROS_PROCH(__startup_entry, argstr, argsize, sysBase)
{
    AROS_PROCFUNC_INIT
    return __startup_entry_body(argstr, argsize, sysBase);
    AROS_PROCFUNC_EXIT
} /* entry */

static SIPTR __attribute__((noinline)) __startup_entry_body(STRPTR argstr, ULONG argsize,
    struct ExecBase *sysBase)
{
    APTR TaskResBase;
    SysBase = sysBase;

    D(bug("%s(\"%s\", %d, %x)\n", __func__, argstr, argsize, sysBase));

    _TaskResBase = OpenResource("task.resource");
    if ((TaskResBase = _TaskResBase) != NULL)
        InitTaskHooks(__startup_latehook_dispatcher, TASKHOOK_TYPE_LATEINIT, THF_IAR);

    /*
        No one program will be able to do anything useful without the dos.library,
        so we open it here instead of using the automatic opening system
    */
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)DOSNAME, 0);
    if (!DOSBase) return RETURN_FAIL;
    if (((struct Library *)DOSBase)->lib_Version < __aros_libreq_DOSBase)
        return RETURN_FAIL;

    __argstr  = (char *)argstr;
    __argsize = argsize;
    __startup_error = RETURN_FAIL;

    __startup_entries_init();
    ___startup_entries_next(sysBase);

    CloseLibrary((struct Library *)DOSBase);

    D(bug("%s: returning %d\n", __func__, __startup_error));

    return __startup_error;
}

static int __startup_latehook_dispatcher(struct Hook *_latehook)
{
    D(bug("%s: hook @ 0x%p\n", __func__, _latehook);)
    return (int)CallHookA(_latehook, NULL, NULL);
}

#define STARTUP_STACK_ALIGN 16

static IPTR __startup_stack_entry(struct ExecBase *SysBase)
{
    __startup_entries_next();
    return 0;
}

/* Runs after detaching and before any other initialization, so that the C
   library's exit() and the program's main() share the new stack. */
static void __startup_stack(struct ExecBase *SysBase)
{
    struct Task *me = FindTask(NULL);
    IPTR current = (IPTR)me->tc_SPUpper - (IPTR)me->tc_SPLower;
    IPTR size = ((IPTR)__stack + STARTUP_STACK_ALIGN - 1) & ~(IPTR)(STARTUP_STACK_ALIGN - 1);
    struct StackSwapStruct sss;
    struct StackSwapArgs args;
    APTR stack;

    if (size <= current)
    {
        __startup_entries_next();
        return;
    }

    D(bug("%s: %lu byte stack, %lu requested\n", __func__, (ULONG)current, (ULONG)size));
    stack = AllocVec(size + STARTUP_STACK_ALIGN, MEMF_ANY);
    if (!stack)
    {
        PutStr((CONST_STRPTR)me->tc_Node.ln_Name);
        PutStr((CONST_STRPTR)": not enough memory for its stack\n");
        __startup_error = RETURN_FAIL;
        return;
    }

    sss.stk_Lower = (APTR)(((IPTR)stack + STARTUP_STACK_ALIGN - 1) & ~(IPTR)(STARTUP_STACK_ALIGN - 1));
    sss.stk_Upper = (UBYTE *)sss.stk_Lower + size;
    sss.stk_Pointer = sss.stk_Upper;
    args.Args[0] = (IPTR)SysBase;
    NewStackSwap(&sss, (APTR)__startup_stack_entry, &args);

    FreeVec(stack);
}

ADD2SET(__startup_stack, PROGRAM_ENTRIES, -90);

static void __startup_main(struct ExecBase *SysBase)
{
    APTR TaskResBase = _TaskResBase;

    D(bug("%s: entering main ...\n", __func__));

    /* run the late init hooks if we have task.resource */
    if (TaskResBase)
        RunTaskHooks(__startup_latehook_dispatcher, TASKHOOK_TYPE_LATEINIT);

    /* Invoke the main function. A weak symbol is used as function name so that
       it can be overridden (for *nix stuff, for instance).  */
    __startup_error = (*__main_function_ptr) (__argc, __argv);

    D(bug("%s: returned\n", __func__));
}

ADD2SET(__startup_main, PROGRAM_ENTRIES, 127);

/*
    Stub function for GCC __main().

    The __main() function is originally used for C++ style constructors
    and destructors in C. This replacement does nothing and gets rid of
    linker-errors about references to __main().
*/
#ifdef AROS_NEEDS___MAIN
void __main(void)
{
    /* Do nothing. */
}
#endif
