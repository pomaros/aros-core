/*
    Copyright (C) 2026 John KNIPPER.
    This file is distributed under the terms of the AROS Public License 1.1.
*/
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int calls, legacy_calls, reset_calls;
static void detailed(int signal, siginfo_t *info, void *context)
{
    assert(signal == SIGUSR1);
    assert(info != NULL);
    assert(info->si_signo == signal);
    assert(info->si_code == SI_USER);
    assert(info->si_errno == 0);
    assert(info->si_pid == getpid());
    assert(info->si_addr == NULL);
    assert(info->si_value.sigval_int == 0);
    assert(context == NULL);
    ++calls;
    if (calls == 1)
    {
        assert(raise(signal) == 0);
        assert(calls == 1); /* recursive delivery remains deferred */
        assert(info->si_signo == signal);
    }
}
static void reset_handler(int signal, siginfo_t *info, void *context)
{
    struct sigaction current;
    assert(signal == SIGUSR1 && info && info->si_signo == signal);
    assert(context == NULL);
    assert(sigaction(signal, NULL, &current) == 0);
    assert(current.sa_handler == SIG_DFL);
    ++reset_calls;
}
static void legacy(int signal)
{
    assert(signal == SIGUSR1);
    ++legacy_calls;
}
int main(void)
{
    struct sigaction action, previous;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = detailed;
    action.sa_flags = SA_SIGINFO;
    assert(sigemptyset(&action.sa_mask) == 0);
    assert(sigaction(SIGUSR1, &action, &previous) == 0);
    assert(raise(SIGUSR1) == 0 && calls == 2);
    action.sa_sigaction = reset_handler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    assert(sigaction(SIGUSR1, &action, NULL) == 0);
    assert(raise(SIGUSR1) == 0 && reset_calls == 1);
    assert(signal(SIGUSR1, legacy) != SIG_ERR);
    assert(raise(SIGUSR1) == 0 && legacy_calls == 1);
    assert(sigaction(SIGUSR1, &previous, NULL) == 0);
    puts("RAISE_SIGINFO_PASS");
    return 0;
}
