
#ifndef fdset_in_h
#define fdset_in_h

#include "mydef.h"
#include "my_handle.h"
#include "my_errno.h"
#include "my_buffer.h"
#include "utils.h"

#ifdef __linux__
#include <netinet/in.h>
#include <sys/epoll.h>
#else
#define EPOLLIN     1
#define EPOLLOUT    2
#endif

#ifdef __APPLE__
#include <netinet/in.h>
#include <sys/event.h>
#endif

struct fd_struct;
struct fds_ops {
    int32_t (*read) (struct fd_struct*, struct my_buffer* mbuf, int err, struct sockaddr_in* in);
    int32_t (*dispose) (struct fd_struct*, struct my_buffer* mbuf);
    struct my_buffer* (*write) (struct fd_struct*, struct sockaddr_in** in, int* err);
    void (*detach) (struct fd_struct*);
    void (*notify) (struct fd_struct*, uint32_t* id);
    struct my_buffer* (*buffer_get) (struct fd_struct*, uint32_t bytes);
};

struct fd_struct {
    int fd;
    int tcp0_udp1;
    struct my_buffer* bye;
    uint32_t mask;
    uint32_t bytes;
    struct fds_ops* fop;
    void* priv;
};

capi my_handle* fdset_attach_fd(void*, struct fd_struct* fds);
capi my_handle* fdset_make_handle(void* set, struct fd_struct* fds);

// do not call it more than one time
capi int fdset_attach_handle(void* set, my_handle* handle);
capi int fdset_update_fdmask(my_handle* handle);
capi uint32_t* sched_handle_alloc(uint32_t id);
capi void sched_handle_free(uint32_t* ptr);

// fdset_sched should only be called in fds_ops, because it does not
// hold locks when add handle to fdctx's list, the adding indeed is not
// neccessary. only to release memory more quickly.
// if it does need calling this ouside of fds_ops, I will modify the code.
capi int fdset_sched(my_handle* handle, uint32_t ms, uint32_t* id);
capi struct fd_struct* fd_struct_from(void* any);

struct task {
    my_handle* handle;
    void* mop;
};

#define task_bytes ((uint32_t) sizeof(struct task))
#define task_get(mbuf) ((struct task *) (intptr_t) (roundup(((intptr_t) mbuf->ptr[1] + mbuf->length), sizeof(char *))))
capi void proto_buffer_post(my_handle* handle, struct my_buffer* mbuf);

#endif
