/*
    Copyright (C) 2026, John KNIPPER <code@jkn.me>.
    Licensed under the AROS Public License (APL) Version 1.1
*/
#include "socket_native.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

static ssize_t invoke(struct aros_socket_request *request)
{
    request->version = AROS_SOCKET_ABI;
    int error = aros_socket_native_call(request);
    if (error) {
        errno = error;
        return -1;
    }
    return request->result;
}

int aros_rs_socket(int domain, int type, int protocol)
{
    struct aros_socket_request request = {.operation = AROS_SOCKET_CREATE,
        .arg0 = domain, .arg1 = type, .arg2 = protocol};
    return (int)invoke(&request);
}

static int address_operation(int operation, int fd, const struct sockaddr *address, uint32_t length)
{
    struct sockaddr_storage storage = {0};
    struct aros_socket_request request = {.operation = (ULONG)operation, .fd = fd};
    size_t expected;
    if (!address || length < 2) {
        errno = EINVAL;
        return -1;
    }
    if (address->sa_family == AF_INET)
        expected = sizeof(struct sockaddr_in);
    else if (address->sa_family == AF_INET6)
        expected = sizeof(struct sockaddr_in6);
    else {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (length != expected) {
        errno = EINVAL;
        return -1;
    }
    memcpy(&storage, address, expected);
    ((struct sockaddr *)&storage)->sa_len = (unsigned char)expected;
    request.address = &storage;
    request.address_length = &length;
    return (int)invoke(&request);
}

int aros_rs_bind(int fd, const struct sockaddr *address, uint32_t length)
{
    return address_operation(AROS_SOCKET_BIND, fd, address, length);
}

int aros_rs_connect(int fd, const struct sockaddr *address, uint32_t length)
{
    return address_operation(AROS_SOCKET_CONNECT, fd, address, length);
}

int aros_rs_listen(int fd, int backlog)
{
    struct aros_socket_request request = {.operation = AROS_SOCKET_LISTEN, .fd = fd, .arg0 = backlog};
    return (int)invoke(&request);
}

static int output_address(int operation, int fd, struct sockaddr *address, uint32_t *length)
{
    struct aros_socket_request request = {.operation = (ULONG)operation, .fd = fd,
        .address = address, .address_length = length};
    if ((address == NULL) != (length == NULL)) {
        errno = EINVAL;
        return -1;
    }
    return (int)invoke(&request);
}

int aros_rs_accept(int fd, struct sockaddr *address, uint32_t *length)
{
    return output_address(AROS_SOCKET_ACCEPT, fd, address, length);
}

int aros_rs_getsockname(int fd, struct sockaddr *address, uint32_t *length)
{
    return output_address(AROS_SOCKET_LOCALNAME, fd, address, length);
}

int aros_rs_getpeername(int fd, struct sockaddr *address, uint32_t *length)
{
    return output_address(AROS_SOCKET_PEERNAME, fd, address, length);
}

int aros_rs_getsockopt(int fd, int level, int option, void *value, uint32_t *length)
{
    if (!length) {
        errno = EINVAL;
        return -1;
    }
    struct aros_socket_request request = {.operation = AROS_SOCKET_GETOPT, .fd = fd,
        .arg0 = level, .arg1 = option, .buffer = value, .length = *length};
    ssize_t result = invoke(&request);
    if (result < 0)
        return -1;
    if (request.length > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    *length = (uint32_t)request.length;
    return (int)result;
}

int aros_rs_setsockopt(int fd, int level, int option, const void *value, uint32_t length)
{
    struct aros_socket_request request = {.operation = AROS_SOCKET_SETOPT, .fd = fd,
        .arg0 = level, .arg1 = option, .buffer = (void *)value, .length = length};
    return (int)invoke(&request);
}

int aros_rs_shutdown(int fd, int how)
{
    struct aros_socket_request request = {.operation = AROS_SOCKET_SHUTDOWN, .fd = fd, .arg0 = how};
    return (int)invoke(&request);
}

int aros_rs_socket_clone(int fd)
{
    struct aros_socket_request request = {.operation = AROS_SOCKET_CLONE, .fd = fd, .arg0 = 0};
    return (int)invoke(&request);
}

int aros_rs_socket_nonblocking(int fd, int enabled)
{
    struct aros_socket_request request = {.operation = AROS_SOCKET_NONBLOCK,
        .fd = fd, .arg0 = enabled != 0};
    return (int)invoke(&request);
}

int aros_rs_socket_close(int fd)
{
    struct aros_socket_request request = {.operation = AROS_SOCKET_CLOSE, .fd = fd};
    return (int)invoke(&request);
}

int aros_rs_socket_wait_writable(int fd, int timeout_ms)
{
    struct aros_socket_request request = {.operation = AROS_SOCKET_POLL, .fd = fd,
        .arg0 = AROS_SOCKET_POLL_WRITE, .arg1 = timeout_ms};
    ssize_t result = invoke(&request);
    if (result < 0)
        return -1;
    return (result & AROS_SOCKET_POLL_WRITE) != 0;
}

/* AROS has no Unix-domain socket-pair facility in this API. Report that
   capability accurately instead of installing a successful no-op. */
int aros_rs_socketpair(int domain, int type, int protocol, int *descriptors)
{
    (void)domain;
    (void)type;
    (void)protocol;
    (void)descriptors;
    errno = EAFNOSUPPORT;
    return -1;
}

ssize_t aros_rs_sendmsg(int fd, const struct msghdr *message, int flags)
{
    (void)fd;
    (void)message;
    (void)flags;
    errno = EOPNOTSUPP;
    return -1;
}

ssize_t aros_rs_recvmsg(int fd, struct msghdr *message, int flags)
{
    (void)fd;
    (void)message;
    (void)flags;
    errno = EOPNOTSUPP;
    return -1;
}

ssize_t aros_rs_send(int fd, const void *buffer, size_t length, int flags)
{
    struct aros_socket_request request = {.operation = AROS_SOCKET_SEND, .fd = fd,
        .arg0 = flags, .buffer = (void *)buffer, .length = length};
    return invoke(&request);
}

ssize_t aros_rs_recv(int fd, void *buffer, size_t length, int flags)
{
    struct aros_socket_request request = {.operation = AROS_SOCKET_RECV, .fd = fd,
        .arg0 = flags, .buffer = buffer, .length = length};
    return invoke(&request);
}

ssize_t aros_rs_sendto(int fd, const void *buffer, size_t length, int flags,
                     const struct sockaddr *address, uint32_t address_length)
{
    struct sockaddr_storage storage = {0};
    size_t expected;
    if (!address || address_length < 2) {
        errno = EINVAL;
        return -1;
    }
    if (address->sa_family == AF_INET)
        expected = sizeof(struct sockaddr_in);
    else if (address->sa_family == AF_INET6)
        expected = sizeof(struct sockaddr_in6);
    else {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (address_length != expected) {
        errno = EINVAL;
        return -1;
    }
    memcpy(&storage, address, expected);
    ((struct sockaddr *)&storage)->sa_len = (unsigned char)expected;
    struct aros_socket_request request = {.operation = AROS_SOCKET_SEND, .fd = fd,
        .arg0 = flags, .buffer = (void *)buffer, .length = length,
        .address = &storage, .address_length = &address_length};
    return invoke(&request);
}

ssize_t aros_rs_recvfrom(int fd, void *buffer, size_t length, int flags,
                       struct sockaddr *address, uint32_t *address_length)
{
    if ((address == NULL) != (address_length == NULL)) {
        errno = EINVAL;
        return -1;
    }
    struct aros_socket_request request = {.operation = AROS_SOCKET_RECV, .fd = fd,
        .arg0 = flags, .buffer = buffer, .length = length,
        .address = address, .address_length = address_length};
    return invoke(&request);
}

/* POSIX band events, with BSD values. The native batch knows three
   readiness kinds, IN, PRI and OUT: normal data is IN (POLLWRNORM is POLLOUT
   itself) and priority band data is PRI, as on BSD sockets. These sockets
   have no write bands, so POLLWRBAND is never reported. */
#define AROS_RS_POLLRDNORM 0x40
#define AROS_RS_POLLRDBAND 0x80
#define AROS_RS_POLLWRBAND 0x100
#define AROS_RS_POLLBANDS (AROS_RS_POLLRDNORM | AROS_RS_POLLRDBAND | AROS_RS_POLLWRBAND)

static WORD native_events(WORD events)
{
    WORD native = (WORD)(events & (AROS_POLL_IN | AROS_POLL_PRI | AROS_POLL_OUT));
    if (events & AROS_RS_POLLRDNORM) native = (WORD)(native | AROS_POLL_IN);
    if (events & AROS_RS_POLLRDBAND) native = (WORD)(native | AROS_POLL_PRI);
    return native;
}

static WORD posix_revents(WORD asked, WORD native)
{
    WORD revents = (WORD)(native & (AROS_POLL_ERR | AROS_POLL_HUP | AROS_POLL_NVAL));
    revents = (WORD)(revents | (native & asked & (AROS_POLL_IN | AROS_POLL_PRI | AROS_POLL_OUT)));
    if ((asked & AROS_RS_POLLRDNORM) && (native & AROS_POLL_IN))
        revents = (WORD)(revents | AROS_RS_POLLRDNORM);
    if ((asked & AROS_RS_POLLRDBAND) && (native & AROS_POLL_PRI))
        revents = (WORD)(revents | AROS_RS_POLLRDBAND);
    return revents;
}

/* pollfd is kept as its exact C/libc scalar ABI, not an fd_set or host tag.
   A set that asks for band events goes to the stack as a translated copy, so
   the caller's events stay as given and revents holds only what it asked. */
int aros_rs_poll(struct aros_socket_pollfd *fds, unsigned int count, int timeout)
{
    struct aros_socket_request request = {.operation = AROS_SOCKET_POLL_BATCH,
        .arg1 = timeout, .buffer = fds, .length = count};
    struct aros_socket_pollfd *native = fds;
    int result, saved_errno;
    unsigned int i;
    if (count && !fds) { errno = EFAULT; return -1; }
    if (timeout < -1) { errno = EINVAL; return -1; }
    for (i = 0; i < count && !(fds[i].events & AROS_RS_POLLBANDS); i++)
        ;
    if (i < count) {
        native = malloc((size_t)count * sizeof(*native));
        if (!native) { errno = ENOMEM; return -1; }
        for (i = 0; i < count; i++) {
            native[i].fd = fds[i].fd;
            native[i].events = native_events(fds[i].events);
            native[i].revents = 0;
        }
        request.buffer = native;
    }
    result = (int)invoke(&request);
    if (native != fds) {
        saved_errno = errno;
        if (result >= 0)
            for (i = 0; i < count; i++)
                fds[i].revents = posix_revents(fds[i].events, native[i].revents);
        free(native);
        errno = saved_errno;
    }
    if (result < 0) return -1;
    /* stdin/out/err are task-local POSIX slots, unlike global socket numbers.
       The stack cannot identify them: refuse valid local slots explicitly. */
    for (i = 0; i < count; i++) {
        if ((fds[i].revents & AROS_POLL_NVAL) && fds[i].fd >= 0 && fds[i].fd <= 2) {
            int saved_errno = errno;
            if (fcntl(fds[i].fd, F_GETFD) >= 0) { errno = EOPNOTSUPP; return -1; }
            if (errno != EBADF) return -1;
            errno = saved_errno;
        }
    }
    return result;
}
