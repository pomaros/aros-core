/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Licensed under the AROS Public License (APL), Version 1.1.
 * Bounded loopback TCP exchange; launch each role with SMP-Run affinity.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#ifdef SMP_NET_HOST
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#else
#include <devices/timer.h>
#include <sys/errno.h>
#endif
#ifdef SMP_NET_HOST
#include <unistd.h>
#define CloseSocket close
#define IoctlSocket ioctl
#define socket_error() errno
#define REPORT(...) printf(__VA_ARGS__)
#define FLUSH() fflush(stdout)
static unsigned current_cpu(void) { return 0; }
static int net_open(void) { return 1; }
static void net_close(void) {}
#else
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <proto/kernel.h>
#include <aros/debug.h>
struct Library *SocketBase;
APTR KernelBase;
#define socket_error() Errno()
#define REPORT(...) do { Printf(__VA_ARGS__); bug(__VA_ARGS__); } while (0)
#define FLUSH() Flush(Output())
static unsigned current_cpu(void) { return KrnGetCPUNumber(); }
static int net_open(void) {
    KernelBase = OpenResource("kernel.resource");
    if (!KernelBase) return 0;
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    return SocketBase != NULL;
}
static void net_close(void) { CloseLibrary(SocketBase); }
#endif

#define ROUNDS 16
#define FRAME 256
static unsigned expected_cpu, samples, wrong_cpu, completed, transferred;
static unsigned first_cpu, last_cpu;

static void sample(void) {
    last_cpu = current_cpu();
    if (!samples) first_cpu = last_cpu;
    samples++;
    if (last_cpu != expected_cpu) wrong_cpu++;
}

static int ready(int fd, int writing) {
    fd_set set;
    struct timeval timeout;
    int result;
    if (fd < 0 || fd >= FD_SETSIZE) return 0;
    timeout.tv_sec = 2; timeout.tv_usec = 0;
    FD_ZERO(&set); FD_SET(fd, &set);
    sample();
#ifdef SMP_NET_HOST
    result = select(fd + 1, writing ? NULL : &set,
                    writing ? &set : NULL, NULL, &timeout);
#else
    result = WaitSelect(fd + 1, writing ? NULL : &set,
                        writing ? &set : NULL, NULL, &timeout, NULL);
#endif
    sample();
    return result == 1 && FD_ISSET(fd, &set);
}

static int nonblocking(int fd) {
    int on = 1;
    return IoctlSocket(fd, FIONBIO, (void *)&on) == 0;
}

static int transfer(int fd, unsigned char *buffer, int writing) {
    unsigned offset = 0, tries;
    for (tries = 0; tries < 16 && offset < FRAME; tries++) {
        int count;
        if (!ready(fd, writing)) return 0;
        count = writing ? send(fd, buffer + offset, FRAME - offset, 0)
                        : recv(fd, buffer + offset, FRAME - offset, 0);
        sample();
        if (count <= 0) {
            if (count < 0 && (socket_error() == EWOULDBLOCK || socket_error() == EINTR))
                continue;
            return 0;
        }
        offset += count;
        transferred += count;
    }
    return offset == FRAME;
}

static void pattern(unsigned char *buffer, unsigned round) {
    unsigned i;
    for (i = 0; i < FRAME; i++) buffer[i] = (unsigned char)(round * 31 + i * 17);
}

static int exchange(int fd, int server) {
    unsigned round;
    unsigned char buffer[FRAME], expected[FRAME];
    for (round = 0; round < ROUNDS; round++) {
        pattern(expected, round);
        if (!server) {
            memcpy(buffer, expected, FRAME);
            if (!transfer(fd, buffer, 1)) return 0;
        }
        if (!transfer(fd, buffer, 0) || memcmp(buffer, expected, FRAME)) return 0;
        if (server && !transfer(fd, buffer, 1)) return 0;
        completed++;
    }
    return !wrong_cpu;
}

static int number(const char *s, unsigned max, unsigned *value) {
    char *end; unsigned long n;
    if (!s[0] || s[0] == '-') return 0;
    n = strtoul(s, &end, 10);
    if (*end || n > max) return 0;
    *value = (unsigned)n; return 1;
}

int main(int argc, char **argv) {
    struct sockaddr_in address;
    int fd = -1, peer = -1, ok = 0, server, error = 0;
    unsigned port;
    const char *stage = "arguments";
#ifdef SMP_NET_HOST
    const char *platform = "host";
#else
    const char *platform = "aros";
#endif
    if (argc != 4 || (strcmp(argv[1], "server") && strcmp(argv[1], "client")) ||
        !number(argv[2], 65535, &port) || port < 1024 ||
        !number(argv[3], 255, &expected_cpu)) {
        REPORT("SMP-Net: server|client PORT EXPECTED_CPU\n");
        return 20;
    }
    server = !strcmp(argv[1], "server");
    if (!net_open()) {
        REPORT("[SMP-Net] result=FAIL stage=open platform=%s\n", platform);
        return 20;
    }
    sample();
    stage = "socket";
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || !nonblocking(fd)) goto done;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(0x7f000001UL);
    if (server) {
        stage = "listen";
        if (bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 1)) goto done;
        REPORT("[SMP-Net] listening port=%u cpu=%u platform=%s\n", port, current_cpu(), platform);
        FLUSH();
        stage = "accept";
        if (!ready(fd, 0)) goto done;
        peer = accept(fd, NULL, NULL);
        if (peer < 0 || !nonblocking(peer)) goto done;
    } else {
#ifdef SMP_NET_HOST
        socklen_t length = sizeof(error);
#else
        int length = sizeof(error);
#endif
        stage = "connect";
        if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0 && socket_error() != EINPROGRESS)
            goto done;
        if (!ready(fd, 1) || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) || error)
            goto done;
        peer = fd;
    }
    stage = "exchange";
    ok = exchange(peer, server);
done:
    error = socket_error();
    if (peer >= 0 && peer != fd) CloseSocket(peer);
    if (fd >= 0) CloseSocket(fd);
    sample();
    ok = ok && !wrong_cpu;
    REPORT("[SMP-Net] role=%s platform=%s result=%s stage=%s rounds=%u bytes=%u first=%u last=%u samples=%u wrong=%u error=%d\n",
           argv[1], platform, ok ? "PASS" : "FAIL", stage, completed, transferred,
           first_cpu, last_cpu, samples, wrong_cpu, ok ? 0 : error);
    net_close();
    return ok ? 0 : 20;
}
