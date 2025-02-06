
#ifndef tcpsvr_h
#define tcpsvr_h

#include "mydef.h"
#include "fdset.h"
#include "fdset_in.h"

#ifndef _WINDOWS
#include <arpa/inet.h>
#endif

capi int32_t client_create(int fd, void* fset);

static int32_t new_connection(struct fd_struct* fds, struct my_buffer* mbuf, int err)
{
    int fd;
    // windows only, should not release mbuf
    // it is tricky, ok, hope it's works well ...
    if (mbuf != NULL) {
        if (err != 0) {
            return 0;
        }

        int64_t n64 = *(int *) mbuf->ptr[0];
        fd = (int) n64;
    } else {
        if (err != 0) {
            logmsg("on accpet called with errno %d\n", err);
            exit(-1);
        }

        fd = accept(fds->fd, NULL, NULL);
        if (fd == -1) {
            return 0;
        }
    }

    int32_t n = client_create(fd, fds->priv);
    if (n == -1) {
        close(fd);
    }
    return 0;
}

static void shutdown_server(struct fd_struct* fds)
{
    logmsg("server closed\n");
    close(fds->fd);
}

static void* tcp_server_new(const char* ip, uint16_t port, uint32_t cycle)
{
    int fd = my_socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        return NULL;
    }

    int n = 1;
    n = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &n, sizeof(n));
    if (n == -1) {
        return NULL;
    }

    static struct fd_struct fds;
    static struct fds_ops fops = {
        new_connection,
        NULL,
        NULL,
        shutdown_server,
        NULL,
        NULL
    };

    fds.fd = fd;
    fds.tcp0_udp1 = 0;
    fds.mask = EPOLLIN;
    fds.bytes = 0;
    fds.fop = &fops;

    struct sockaddr_in in = {0};
    in.sin_family = AF_INET;
    in.sin_port = htons(port);
    in.sin_addr.s_addr = inet_addr(ip);
    n = bind(fds.fd, (struct sockaddr *) &in, sizeof(in));
    if (n == -1) {
        return NULL;
    }

    n = listen(fds.fd, 5);
    if (n == -1) {
        close(fds.fd);
        return NULL;
    }

    void* fset = fdset_new(cycle);
    if (fset == NULL) {
        close(fds.fd);
        return NULL;
    }

    my_handle* handle = fdset_attach_fd(fset, &fds);
    if (handle == NULL) {
        fdset_delete(fset);
        return NULL;
    }

    handle_release(handle);
    fds.priv = fset;
    return fset;
}

#endif
