/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    QFRZLIST: the task list on the kernel log, for the QFRZ pass. One line
    per task, "[QFRZ] <tag> task=<name> state=<n>" (a CLI process carries
    its command name), then "[QFRZ] <tag> summary tasks=<n> frozen=<n>".
    Run before and after C:Sleep; the runner compares the two sets.
*/
#include <string.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <workbench/startup.h>
#include <aros/debug.h>

int __nowbsupport = 1;
int __nostdiowin = 1;
struct WBStartup *WBenchMsg;

#define MAX_TASKS 96
#define NAME_SIZE 64

static char names[MAX_TASKS][NAME_SIZE];
static LONG states[MAX_TASKS];

static void copy_name(char *out, struct Task *t)
{
    const char *name = t->tc_Node.ln_Name ? t->tc_Node.ln_Name : "-";
    size_t n = strlen(name);

    if (n > NAME_SIZE / 2 - 1)
        n = NAME_SIZE / 2 - 1;
    memcpy(out, name, n);
    out[n] = 0;
    if (t->tc_Node.ln_Type == NT_PROCESS && ((struct Process *)t)->pr_CLI)
    {
        struct CommandLineInterface *cli = BADDR(((struct Process *)t)->pr_CLI);
        if (cli && cli->cli_CommandName && AROS_BSTR_strlen(cli->cli_CommandName))
        {
            size_t len = AROS_BSTR_strlen(cli->cli_CommandName), room = NAME_SIZE - n - 2;
            if (len > room)
                len = room;
            out[n] = ':';
            memcpy(out + n + 1, AROS_BSTR_ADDR(cli->cli_CommandName), len);
            out[n + 1 + len] = 0;
        }
    }
}

static ULONG collect(struct List *list, ULONG count)
{
    struct Task *t;

    for (t = (struct Task *)list->lh_Head; t->tc_Node.ln_Succ && count < MAX_TASKS;
         t = (struct Task *)t->tc_Node.ln_Succ)
    {
        copy_name(names[count], t);
        states[count] = t->tc_State;
        count++;
    }
    return count;
}

int main(void)
{
    IPTR args[1] = { 0 };
    struct RDArgs *rda = ReadArgs("TAG/A", args, NULL);
    const char *tag = rda ? (const char *)args[0] : "?";
    ULONG count = 0, frozen = 0, i;

    Disable();
    count = collect(&SysBase->TaskReady, count);
    count = collect(&SysBase->TaskWait, count);
    Enable();
    for (i = 0; i < count; i++)
    {
        kprintf("[QFRZ] %s task=%s state=%ld\n", tag, names[i], (long)states[i]);
        if (states[i] == TS_FROZEN)
            frozen++;
    }
    kprintf("[QFRZ] %s summary tasks=%lu frozen=%lu\n", tag, (unsigned long)count, (unsigned long)frozen);
    if (rda)
        FreeArgs(rda);
    return 0;
}
