/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER

    POSIX.1-2008 function system().
*/

#include <aros/debug.h>

#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/var.h>
#include <stdlib.h>

#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "__posixc_intbase.h"

#include "__fdesc.h"
#include "__upath.h"
#include "__cwd_runtime.h"
#include "__posixc_env.h"

static int system_sh(const char *string, char **environment);
static int system_no_sh(const char *string, char **environment);
static void system_environment_free(char **environment)
{
    if (environment) {
        for (size_t i = 0; environment[i]; i++) free(environment[i]);
        free(environment);
    }
}
/* Environ emulation is optional. Without it, a real Process still owns
 * native LV_VAR local variables, which the DOS helper must receive by value. */
static char **system_environment(void)
{
    char ***source = __posixc_get_environptr(), **environment;
    struct Task *task = FindTask(NULL);
    struct Process *process = NULL;
    struct LocalVar *variable;
    size_t count = 0, copied = 0;
    if (source) {
        if (*source) while ((*source)[count]) count++;
    } else if (task->tc_Node.ln_Type == NT_PROCESS) {
        process = (struct Process *)task;
        ForeachNode(&process->pr_LocalVars, variable)
            if (variable->lv_Node.ln_Type == LV_VAR) count++;
    }
    if (count > (size_t)-1 / sizeof(char *) - 1) { errno = E2BIG; return NULL; }
    environment = calloc(count + 1, sizeof(char *));
    if (!environment) { errno = ENOMEM; return NULL; }
    if (source) {
        for (; copied < count; copied++) {
            environment[copied] = strdup((*source)[copied]);
            if (!environment[copied]) goto no_memory;
        }
    } else if (process) {
        ForeachNode(&process->pr_LocalVars, variable) {
            size_t name_size, value_size;
            char *item;
            if (variable->lv_Node.ln_Type != LV_VAR) continue;
            name_size = strlen(variable->lv_Node.ln_Name);
            value_size = variable->lv_Len;
            if (name_size > (size_t)-1 - value_size - 2) goto no_memory;
            item = malloc(name_size + value_size + 2);
            if (!item) goto no_memory;
            memcpy(item, variable->lv_Node.ln_Name, name_size);
            item[name_size] = '=';
            memcpy(item + name_size + 1, variable->lv_Value, value_size);
            item[name_size + value_size + 1] = 0;
            environment[copied++] = item;
        }
    }
    return environment;
no_memory:
    system_environment_free(environment);errno = ENOMEM;return NULL;
}



/*****************************************************************************

    NAME */
#include <stdlib.h>

        int system (

/*  SYNOPSIS */
        const char *string)

/*  FUNCTION
        Executes a command specified by the string argument.

        If the string is `NULL`, the function returns 1 to indicate that
        a command processor is available.

        If the string is non-NULL, the command is passed to a shell (if
        available) or executed directly if no shell is present.

    INPUTS
        string - Command string to execute, or NULL to check for shell support.

    RESULT
        On success, returns the command's exit status.
        On error, returns -1 and sets `errno` appropriately.
        If `string` is NULL, returns 1 (indicating a shell is available).

    NOTES
        - On AROS, if the environment supports it, the command will be
          executed through `/bin/sh` (or `bin:sh`).
        - If no shell is found, the command is executed directly using the
          AROS `SystemTags()` interface.
        - The implementation ensures standard input/output/error are passed
          through to the new process.
        - Command paths and arguments are translated from UNIX-style to
          Amiga-style if necessary.

    EXAMPLE
        system("ls -l /");

    BUGS
        - Argument splitting and quoting are simplistic; edge cases may break.
        - Behavior depends on the shell being available in `bin:sh`.

    SEE ALSO
        exec(), popen(), fork(), execl(), waitpid()

    INTERNALS
        - Checks if the shell is available by trying to lock `bin:sh`.
        - Uses `vfork()` and `execl()` when a shell is available.
        - Falls back to `SystemTags()` for direct command execution.
        - Retains standard file descriptions throughout direct execution.

******************************************************************************/
{
    BPTR lock = BNULL;
    char **environment;
    int result, saved_errno;
    if (!string || !*string) return 1;
    environment = system_environment();
    if (!environment) return -1;
    /* The path provider's helper suppresses requesters; no Process fields
       of an ordinary worker Task are accessed here. */
    if (__cwd_runtime_conversion()) lock = Lock("bin:sh", SHARED_LOCK);
    if (lock) {
        UnLock(lock);
        result = system_sh(string, environment);
    } else result = system_no_sh(string, environment);
    saved_errno = errno;
    system_environment_free(environment);
    errno = saved_errno;
    return result;
} /* system */


static int system_sh(const char *string, char **environment)
{
    pid_t pid = vfork();
    int status;

    D(bug("system_sh(%s)\n", string));
    
    if(pid > 0)
    {
        if(waitpid(pid, &status, 0) == -1)
            return -1;
        return status;
    }
    else if(pid == 0)
    {
        char *arguments[] = { "sh", "-c", (char *)string, NULL };
        execve(__cwd_runtime_conversion() ? "/bin/sh" : "bin:sh",
            arguments, environment);
        _exit(127);
    }
    else
    {
        return -1;
    }
}
        
static int system_no_sh(const char *string, char **environment)
{
    POSIXC_CWD_SCOPE_OR_RETURN(-1);
    const char *apath;
    char *copy = strdup(string), *args, *command, *fullcmd = NULL;
    fcb *files[3] = {NULL, NULL, NULL};
    BPTR handles[3] = {BNULL, BNULL, BNULL};
    SIPTR native_args[6], error = 0, result;
    BOOL entered = FALSE;
    int ret = -1, saved_errno;
    if (!copy) { errno = ENOMEM; return -1; }
    args = copy;
    command = strsep(&args, " \t\n");
    if (!args) args = "";
    apath = __path_u2a(command);
    if (!apath) goto done;
    if (strlen(apath) > (size_t)-1 - strlen(args) - 2) { errno = E2BIG; goto done; }
    fullcmd = malloc(strlen(apath) + strlen(args) + 2);
    if (!fullcmd) { errno = ENOMEM; goto done; }
    strcpy(fullcmd, apath);strcat(fullcmd, " ");strcat(fullcmd, args);
    for (unsigned i = 0; i < 3; i++) {
        int admission_error = 0;
        files[i] = __holdfcb(i, &admission_error);
        if (admission_error) { errno = admission_error; goto done; }
        if (files[i]) {
            __fcb_lock(files[i]);handles[i] = files[i]->handle;__fcb_unlock(files[i]);
        }
    }
    if (handles[1] == handles[2]) handles[2] = BNULL;
    native_args[0] = (SIPTR)fullcmd;
    native_args[1] = (SIPTR)handles[0];native_args[2] = (SIPTR)handles[1];native_args[3] = (SIPTR)handles[2];
    native_args[4] = (SIPTR)environment;
    native_args[5] = (SIPTR)&entered;
    result = __cwd_runtime_native(POSIXC_CONTEXT_SYSTEM, native_args, &error);
    SetIoErr((LONG)error);
    if (!entered || result == -1) {
        errno = error ? __stdc_ioerr2errno((LONG)error) : EIO;
        goto done;
    }
    ret = (int)result;
done:
    saved_errno = errno;
    for (unsigned i = 0; i < 3; i++) if (files[i]) __releasefcb(files[i]);
    free(fullcmd);free(copy);
    errno = saved_errno;
    return ret;
}
