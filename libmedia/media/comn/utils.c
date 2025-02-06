
#include "utils.h"
#include "my_errno.h"
#include "mem.h"
#include <stdio.h>

#ifndef _MSC_VER
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>

#ifdef  __linux__
#define send_flag MSG_NOSIGNAL
#else
#define send_flag 0
#endif

int my_socket(int af, int type, int protocol)
{
    int fd = socket(af, type, protocol);
    return fd;
}

void my_close(int fd)
{
    while (1) {
        int n = close(fd);
        if (n == 0 || EBADF == errno) {
            return;
        }

        if (errno != EINTR) {
            mark("close %d errno %d\n", fd, errno);
            return;
        }
    }
}

int make_none_block(int fd)
{
    int val = fcntl(fd, F_GETFL, 0);
    if (val != -1) {
        if (0 == (val & O_NONBLOCK)) {
            val |= O_NONBLOCK;
            val = fcntl(fd, F_SETFL, val);
        } else {
            val = 0;
        }
    }
    return val;
}

int make_no_sigpipe(int sock)
{
    int n = 0;
#ifdef __APPLE__
    int one = 1;
    n = setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, (void *) &one, sizeof(one));
#endif
    return n;
}

int do_connect(int fd, const char* ip, short port)
{
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    int n;
    do {
        n = connect(fd, (struct sockaddr *) &addr, sizeof(addr));
        if (n == -1 && EINPROGRESS == errno) {
            break;
        }
        mark("connect failed with %d", errno);
    } while (n == -1 && EINTR == errno);

    return n;
}

int check_connection(int fd)
{
    int error = 0;
    socklen_t len = sizeof(error);
    int n = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);
    if (n == -1 || error != 0) {
        mark("check_connection error = (%d)%d\n", n, error);
        return -1;
    }
    return 0;
}

int do_tcp_read(int fd, struct my_buffer* mbuf)
{
    while (mbuf->length > 0) {
        int n = (int) recv(fd, mbuf->ptr[1], mbuf->length, 0);
        if (n == 0) {
            errno = ESHUTDOWN;
            return -1;
        }

        if (n < 0) {
            if (EINTR == errno) {
                continue;
            } else if (EAGAIN != errno && EWOULDBLOCK != errno) {
                mark("read %d bytes, errno = %d\n", n, errno);
            }
            return -1;
        }

        mbuf->ptr[1] += n;
        mbuf->length -= n;
    }
    return 0;
}

int do_tcp_write(int fd, struct my_buffer* mbuf)
{
    int n = 0, nr = 0;

    while (mbuf->length > 0) {
        n = (int) send(fd, mbuf->ptr[1], (size_t) mbuf->length, send_flag);
        if (n == -1) {
            if (EINTR == errno) {
                continue;
            }

            if (EAGAIN == errno || EWOULDBLOCK == errno) {
                n = 0;
            }
            break;
        }

        if (__builtin_expect(n == 0, 0)) {
            if (++nr > 16) {
                n = -1;
                errno = EBADF;
                mark("send 0 bytes for %d times\n", nr);
                break;
            }
        } else {
            nr = 0;
            mbuf->length -= n;
            mbuf->ptr[1] += n;
        }
    }

    if (n > 0) {
        n = 0;
    }
    return n;
}

int do_udp_read(int fd, struct my_buffer* mbuf, struct sockaddr_in* inptr)
{
    int n;
    do {
        socklen_t sz = sizeof(struct sockaddr_in);
        n = (int) recvfrom(fd, mbuf->ptr[1], mbuf->length, 0, (struct sockaddr *) inptr, &sz);
    } while (n == -1 && errno == EINTR);

    if (n > 0) {
        mbuf->ptr[1] += n;
        mbuf->length = 0;
        n = 0;
    } else if (errno != EAGAIN) {
        mark("none blocked udp socket recvfrom get %d, errno = %d\n", n, errno);
        n = -1;
    }
    return n;
}

int do_udp_write(int fd, struct my_buffer* mbuf, struct sockaddr_in* in)
{
    do {
        int bytes = (int) sendto(fd, mbuf->ptr[1], mbuf->length, 0, (struct sockaddr *) in, sizeof(*in));
        if (bytes > 0) {
            mbuf->ptr[1] += mbuf->length;
            mbuf->length = 0;
            break;
        }

        if (bytes == 0) {
            mark("send 0 bytes, mbuf->length = %d, errno = %d", (uint32_t) mbuf->length, errno);
            break;
        }

        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            mark("sendto failed with errno %d\n", errno);
        }
    } while (errno == EINTR);

    return 0;
}

#else

int my_socket(int af, int type, int protocol)
{
    SOCKET fd = WSASocket(af, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (fd == INVALID_SOCKET) {
        int n = WSAGetLastError();
        return -1;
    }

    if (type == SOCK_DGRAM) {
        struct sockaddr_in in = {0};
        in.sin_family = AF_INET;
        in.sin_port = 0;
        in.sin_addr.s_addr = INADDR_ANY;
        int n = bind(fd, (struct sockaddr *) &in, sizeof(in));
        if (n == -1) {
            my_close((int) fd);
            return -1;
        }
    }
    return (int) fd;
}

void my_close(int x)
{
    closesocket((SOCKET) x);
}

int make_none_block(int fd)
{
    return 0;

    u_long n = 1;
    int ret = ioctlsocket(fd, FIONBIO, &n);
    if (ret != 0) {
        ret = -1;
    }
    return n;
}

int make_no_sigpipe(int fd)
{
    return 0;
}

int do_connect(int fd, const char* ip, short port)
{
    DWORD bytes;
    static LPFN_CONNECTEX lpfnConnectEx = NULL;
    if (lpfnConnectEx == NULL) {
        GUID GuidConnectEx = WSAID_CONNECTEX;
        int n = WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                         &GuidConnectEx, sizeof (GuidConnectEx),
                         &lpfnConnectEx, sizeof (lpfnConnectEx),
                         &bytes, NULL, NULL);
        if (n == SOCKET_ERROR) {
            errno = ENOSYS;
            return -1;
        }
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    io_context* ioc = (io_context *) my_malloc(sizeof(io_context));
    if (ioc == NULL) {
        errno = ENOMEM;
        return -1;
    }
    memset(ioc, 0, sizeof(io_context));
    ioc->flag = 0;
    ioc->io_type = 3;
    ioc->base = hand_shake;

    struct sockaddr_in in = {0};
    in.sin_family = AF_INET;
    in.sin_port = 0;
    in.sin_addr.s_addr = INADDR_ANY;
    int n = bind(fd, (struct sockaddr *) &in, sizeof(in));
    if (n == -1) {
        return NULL;
    }

    BOOL b = lpfnConnectEx((SOCKET) fd, (struct sockaddr *) &addr, sizeof(addr), NULL, 0, &bytes, &ioc->overlap);
    if (b) {
        my_free(ioc);
        setsockopt((SOCKET) fd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
    } else {
        n = -1;
        int err = WSAGetLastError();
        if (ERROR_IO_PENDING == err) {
            errno = EINPROGRESS;
        }
    }
    return n;
}

int check_connection(int fd)
{
    DWORD result = 0;
    int bytes = sizeof(result);
    int n = getsockopt(fd, SOL_SOCKET, 0x700C, (char *) &result, &bytes);
    if (n == -1 || result == 0xffffffff) {
        return -1;
    }
    setsockopt((SOCKET) fd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
    return 0;
}

int do_tcp_read(int fd, struct my_buffer* mbuf)
{
    io_context* ioc = (io_context *) mbuf;
    DWORD flags = 0;

    int n = WSARecv(fd, &ioc->wsa_buffer, 1, NULL, &flags, &ioc->overlap, NULL);
    DWORD code = WSAGetLastError();
    if (n == 0 || WSA_IO_PENDING == code) {
        return 0;
    }

    errno = EFAILED;
    return -1;
}

int do_tcp_write(int fd, struct my_buffer* mbuf)
{
    io_context* ioc = (io_context *) mbuf;
    my_assert(ioc->wsa_buffer.len > 0);
    errno = 0;
    my_assert(ioc->wsa_buffer.len > 16);

    int n = WSASend(fd, &ioc->wsa_buffer, 1, NULL, 0, &ioc->overlap, NULL);
    if (n == 0 || WSA_IO_PENDING == WSAGetLastError()) {
        return 0;
    }
    errno = EFAILED;
    return -1;
}

int do_udp_read(int fd, struct my_buffer* mbuf, struct sockaddr_in* inptr)
{
    io_context* ioc = (io_context *) mbuf;
    DWORD flags = 0;

    int n = WSARecvFrom(fd, &ioc->wsa_buffer, 1, NULL, &flags, &ioc->addr, &ioc->addr_size, &ioc->overlap, NULL);
    if (n == 0 || WSA_IO_PENDING == WSAGetLastError()) {
        return 0;
    }

    errno = EFAILED;
    return -1;
}

int do_udp_write(int fd, struct my_buffer* mbuf, struct sockaddr_in* in)
{
    io_context* ioc = (io_context *) mbuf;
    DWORD flags = 0;

    int n = WSASendTo(fd, &ioc->wsa_buffer, 1, NULL, 0,
                (const struct sockaddr *) in, sizeof(struct sockaddr), &ioc->overlap, NULL);
    if (n == 0 || WSA_IO_PENDING == WSAGetLastError()) {
        return 0;
    }

    errno = EAGAIN;
    return -1;
}

#endif
