/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: SNTPSync - set the system clock from an NTP server, once.
 */

/******************************************************************************

    NAME

        SNTPSync

    SYNOPSIS

        SERVER,PORT/K/N,WAIT/K/N,TIMEOUT/K/N,NOSET/S,QUIET/S

    LOCATION

        C:

    FUNCTION

        Asks one NTP server for the time (SNTP, RFC 4330) and sets the
        system clock from its transmit timestamp. Meant for the startup
        sequence of a machine that gets its address from DHCP: it waits,
        for at most WAIT seconds, until bsdsocket.library is up and the
        server name resolves, then sends up to three requests of TIMEOUT
        seconds each. Everything is bounded; nothing ever opens a
        requester. One line reports the outcome.

        The server comes from SERVER, else from ENV:SNTP/Server, else
        pool.ntp.org. The time is converted to the local zone with the
        locale preferences, as SetClock does.

    INPUTS

        SERVER   --  NTP server name or address
        PORT     --  UDP port, default 123
        WAIT     --  seconds to wait for the network, default 60
        TIMEOUT  --  seconds to wait for each reply, default 5
        NOSET    --  report the time without setting the clock
        QUIET    --  print only failures

    RESULT

        RETURN_OK when the clock was set (or read with NOSET), RETURN_WARN
        when no usable reply arrived in time, RETURN_FAIL on a bad argument.

    EXAMPLE

        SNTPSync
        SNTPSync SERVER time.apple.com WAIT 30

    SEE ALSO

        SetClock, Date

******************************************************************************/

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <devices/timer.h>
#include <libraries/locale.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/locale.h>
#include <proto/timer.h>
#include <proto/bsdsocket.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>

#include "sntp_packet.h"

const char version[] = "$VER: SNTPSync 1.0 (30.9.2026)";

#define TEMPLATE "SERVER,PORT/K/N,WAIT/K/N,TIMEOUT/K/N,NOSET/S,QUIET/S"
#define DEFAULT_SERVER "pool.ntp.org"
#define ENV_SERVER "ENV:SNTP/Server"
#define ENV_LASTSYNC "ENV:SNTPSync/LastSync"
#define DEFAULT_WAIT 60
#define DEFAULT_TIMEOUT 5
#define ATTEMPTS 3

struct Library *SocketBase;
struct Device *TimerBase;

struct Args {
    STRPTR server;
    IPTR *port;
    IPTR *wait;
    IPTR *timeout;
    IPTR noset;
    IPTR quiet;
};

static struct timerequest *timer_request;
static ULONG clock_before_secs;
static ULONG clock_before_micro;
static int clock_before_valid;
static struct MsgPort *timer_port;
static char server_buffer[256];

static int open_timer(void)
{
    timer_port = CreateMsgPort();
    if (timer_port == NULL)
        return 0;
    timer_request = (struct timerequest *)CreateIORequest(
        timer_port, sizeof(*timer_request));
    if (timer_request == NULL)
        return 0;
    if (OpenDevice("timer.device", UNIT_VBLANK,
            (struct IORequest *)timer_request, 0) != 0) {
        DeleteIORequest((struct IORequest *)timer_request);
        timer_request = NULL;
        return 0;
    }
    TimerBase = timer_request->tr_node.io_Device;
    return 1;
}

static void close_timer(void)
{
    if (timer_request != NULL) {
        CloseDevice((struct IORequest *)timer_request);
        DeleteIORequest((struct IORequest *)timer_request);
    }
    if (timer_port != NULL)
        DeleteMsgPort(timer_port);
}

/* Minutes to subtract from UTC for local time, from the locale prefs. */
static LONG local_offset_minutes(void)
{
    struct Library *LocaleBase = OpenLibrary("locale.library", 0);
    struct Locale *locale;
    LONG minutes = 0;

    if (LocaleBase == NULL)
        return 0;
    locale = OpenLocale(NULL);
    if (locale != NULL) {
        minutes = locale->loc_GMTOffset;
        CloseLocale(locale);
    }
    CloseLibrary(LocaleBase);
    return minutes;
}

static const char *choose_server(const struct Args *args)
{
    BPTR file;
    LONG length;

    if (args->server != NULL && args->server[0] != '\0')
        return args->server;
    file = Open(ENV_SERVER, MODE_OLDFILE);
    if (file != BNULL) {
        length = Read(file, server_buffer, sizeof(server_buffer) - 1);
        Close(file);
        while (length > 0 && (server_buffer[length - 1] == '\n' ||
                server_buffer[length - 1] == '\r' ||
                server_buffer[length - 1] == ' '))
            --length;
        if (length > 0) {
            server_buffer[length] = '\0';
            return server_buffer;
        }
    }
    return DEFAULT_SERVER;
}

/* One resolve attempt; the stack may not be up yet, so failures are quiet. */
static int resolve(const char *name, struct in_addr *address)
{
    struct hostent *host;
    ULONG numeric = inet_addr(name);

    if (numeric != INADDR_NONE) {
        address->s_addr = numeric;
        return 1;
    }
    host = gethostbyname((STRPTR)name);
    if (host == NULL || host->h_addrtype != AF_INET ||
            host->h_length != (int)sizeof(*address) ||
            host->h_addr_list == NULL || host->h_addr_list[0] == NULL)
        return 0;
    memcpy(address, host->h_addr_list[0], sizeof(*address));
    return 1;
}

static void nonce(struct sntp_time *out)
{
    struct timeval now;

    GetSysTime(&now);
    out->seconds = (uint32_t)(now.tv_secs + SNTP_AMIGA_EPOCH_OFFSET +
        SNTP_UNIX_EPOCH_OFFSET);
    out->fraction = (uint32_t)(now.tv_micro * 4295U) ^
        (uint32_t)(IPTR)FindTask(NULL);
}

/* Send one request and wait TIMEOUT seconds for a valid reply. */
static enum sntp_status exchange(struct in_addr address, UWORD port,
    LONG timeout, struct sntp_reply *reply, const char **failure)
{
    uint8_t packet[SNTP_PACKET_SIZE];
    uint8_t answer[SNTP_PACKET_SIZE + 16];
    struct sockaddr_in to;
    struct sockaddr_in from;
    socklen_t from_length = sizeof(from);
    struct timeval limit;
    struct sntp_time sent;
    fd_set readable;
    LONG sock;
    LONG got;
    enum sntp_status status = SNTP_SHORT;

    *failure = NULL;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        *failure = "no UDP socket";
        return SNTP_SHORT;
    }
    memset(&to, 0, sizeof(to));
    to.sin_len = sizeof(to);
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr = address;
    nonce(&sent);
    sntp_build_request(packet, sent);
    if (sendto(sock, packet, sizeof(packet), 0,
            (struct sockaddr *)&to, sizeof(to)) != (LONG)sizeof(packet)) {
        *failure = "send failed";
        CloseSocket(sock);
        return SNTP_SHORT;
    }
    FD_ZERO(&readable);
    FD_SET(sock, &readable);
    limit.tv_secs = timeout;
    limit.tv_micro = 0;
    if (WaitSelect(sock + 1, &readable, NULL, NULL, &limit, NULL) <= 0 ||
            !FD_ISSET(sock, &readable)) {
        *failure = "no reply";
        CloseSocket(sock);
        return SNTP_SHORT;
    }
    got = recvfrom(sock, answer, sizeof(answer), 0,
        (struct sockaddr *)&from, &from_length);
    CloseSocket(sock);
    if (got < 0) {
        *failure = "receive failed";
        return SNTP_SHORT;
    }
    status = sntp_parse_reply(answer, (unsigned)got, sent, reply);
    if (status != SNTP_OK)
        *failure = sntp_status_name(status);
    return status;
}

/* ENV:SNTPSync/LastSync: "<unix seconds> <server>\n", the mark a hardware
 * report reads to know the network set the clock and when. */
static void record_sync(uint64_t unix_seconds, const char *server)
{
    BPTR lock;
    BPTR file;
    char line[300];
    LONG length;

    lock = Lock("ENV:SNTPSync", SHARED_LOCK);
    if (lock == BNULL)
        lock = CreateDir("ENV:SNTPSync");
    if (lock == BNULL)
        return;
    UnLock(lock);
    file = Open("ENV:SNTPSync/LastSync", MODE_NEWFILE);
    if (file == BNULL)
        return;
    length = snprintf(line, sizeof(line), "%llu %s\n",
        (unsigned long long)unix_seconds, server);
    if (length > 0)
        Write(file, line, length);
    Close(file);
}

static void report(const struct Args *args, const char *server,
    uint64_t unix_seconds, LONG offset_minutes, int set)
{
    unsigned year, month, day, hour, minute, second;

    if (args->quiet)
        return;
    sntp_civil(unix_seconds, &year, &month, &day, &hour, &minute, &second);
    Printf("[sntp] %s %04lu-%02lu-%02lu %02lu:%02lu:%02lu UTC from %s"
        " (local offset %ld min)",
        (IPTR)(set ? "set" : "read"), (IPTR)year, (IPTR)month, (IPTR)day,
        (IPTR)hour, (IPTR)minute, (IPTR)second, (IPTR)server,
        (IPTR)offset_minutes);
    if (clock_before_valid) {
        /* clock_before_secs holds old minus new, as a signed count. */
        LONG skew = (LONG)clock_before_secs;
        ULONG milli = clock_before_micro / 1000U;

        if (skew < 0 && milli != 0) {
            /* -3 s + 0.250 s is -2.750 s */
            skew += 1;
            milli = 1000U - milli;
        }
        Printf("; the clock was %s%ld.%03lu s",
            (IPTR)(skew < 0 ? "-" : "+"),
            (IPTR)(skew < 0 ? -skew : skew), (IPTR)milli);
    }
    PutStr("\n");
}

int main(void)
{
    struct Args args;
    struct RDArgs *rda;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR old_window;
    const char *server;
    const char *failure = "network never came up";
    struct in_addr address;
    struct sntp_reply reply;
    LONG port = SNTP_PORT;
    LONG wait = DEFAULT_WAIT;
    LONG timeout = DEFAULT_TIMEOUT;
    LONG waited = 0;
    LONG attempts = 0;
    int resolved = 0;
    int result = RETURN_WARN;

    memset(&args, 0, sizeof(args));
    rda = ReadArgs(TEMPLATE, (IPTR *)&args, NULL);
    if (rda == NULL) {
        PrintFault(IoErr(), "SNTPSync");
        return RETURN_FAIL;
    }
    if (args.port != NULL)
        port = (LONG)*args.port;
    if (args.wait != NULL)
        wait = (LONG)*args.wait;
    if (args.timeout != NULL)
        timeout = (LONG)*args.timeout;
    if (port <= 0 || port > 65535 || wait < 0 || timeout <= 0) {
        FreeArgs(rda);
        PutStr("SNTPSync: bad PORT, WAIT or TIMEOUT\n");
        return RETURN_FAIL;
    }
    server = choose_server(&args);
    /* No "please insert volume" requesters: this runs unattended. */
    old_window = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;

    if (!open_timer()) {
        me->pr_WindowPtr = old_window;
        FreeArgs(rda);
        PutStr("[sntp] failed: timer.device\n");
        return RETURN_FAIL;
    }

    /*
     * Wait for the stack: the library appears when AROSTCP starts, and
     * the name resolves once DHCP has installed the address and DNS.
     */
    for (;;) {
        if (SocketBase == NULL)
            SocketBase = OpenLibrary("bsdsocket.library", 4);
        if (SocketBase != NULL && resolve(server, &address)) {
            resolved = 1;
            break;
        }
        if (waited >= wait)
            break;
        Delay(50);
        ++waited;
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) {
            failure = "interrupted";
            break;
        }
    }
    if (!resolved && SocketBase != NULL)
        failure = "server name did not resolve";

    while (resolved && attempts < ATTEMPTS) {
        enum sntp_status status;

        ++attempts;
        status = exchange(address, (UWORD)port, timeout, &reply, &failure);
        if (status == SNTP_OK) {
            uint64_t unix_seconds = sntp_to_unix_seconds(
                reply.transmit.seconds);
            LONG offset_minutes = local_offset_minutes();
            int set = 0;

            if (unix_seconds >= SNTP_AMIGA_EPOCH_OFFSET && !args.noset) {
                uint64_t amiga = unix_seconds - SNTP_AMIGA_EPOCH_OFFSET;
                struct timeval before;

                GetSysTime(&before);
                /* The clock as it was, in the same (local) scale as the
                 * value about to be set, so the line shows the skew. */
                clock_before_secs = before.tv_secs;
                clock_before_micro = before.tv_micro;

                if (offset_minutes < 0 ||
                        amiga > (uint64_t)offset_minutes * 60U)
                    amiga -= (uint64_t)((int64_t)offset_minutes * 60);
                timer_request->tr_node.io_Command = TR_SETSYSTIME;
                timer_request->tr_time.tv_secs = (ULONG)amiga;
                timer_request->tr_time.tv_micro =
                    sntp_fraction_to_micro(reply.transmit.fraction);
                timer_request->tr_node.io_Flags = IOF_QUICK;
                DoIO((struct IORequest *)timer_request);
                set = timer_request->tr_node.io_Error == 0;
                if (set) {
                    /* old minus new, in microseconds, signed. */
                    LONG delta_micro = (LONG)clock_before_micro -
                        (LONG)timer_request->tr_time.tv_micro;
                    LONG delta_secs = (LONG)clock_before_secs - (LONG)amiga;

                    if (delta_micro < 0) {
                        delta_micro += 1000000;
                        delta_secs -= 1;
                    }
                    clock_before_secs = (ULONG)delta_secs;
                    clock_before_micro = (ULONG)delta_micro;
                    clock_before_valid = 1;
                }
                if (!set)
                    failure = "TR_SETSYSTIME refused";
            }
            if (set)
                record_sync(unix_seconds, server);
            if (set || args.noset) {
                report(&args, server, unix_seconds, offset_minutes, set);
                result = RETURN_OK;
            }
            break;
        }
    }

    if (result != RETURN_OK)
        Printf("[sntp] failed: %s (server %s, waited %ld s, attempts %ld)\n",
            (IPTR)failure, (IPTR)server, (IPTR)waited, (IPTR)attempts);

    if (SocketBase != NULL)
        CloseLibrary(SocketBase);
    close_timer();
    me->pr_WindowPtr = old_window;
    FreeArgs(rda);
    return result;
}
