
#ifndef fdctxpool_h
#define fdctxpool_h

#include "mydef.h"
#include "list_head.h"
#include "lock.h"
#include "now.h"
#include "my_buffer.h"
#include "my_handle.h"
#include "utils.h"

#ifdef __linux__
#include <sys/epoll.h>
#endif

#ifdef __APPLE__
#include <sys/event.h>
#endif

#define invalid_tid (-1)

typedef struct tag_fd_context {
    uint32_t stat;
    uintptr_t proc_tid;
    lock_t mask_lck, set_lck;
    my_handle* hset;
    my_handle* self;

    struct list_head timer_head;
    struct list_head set_entry;

#ifdef __APPLE__
    uint32_t mask[2];
    struct kevent event[2];
#elif defined(__linux__)
    uint32_t mask[2];
    uint32_t attaching;
#elif defined(_MSC_VER)
    io_context ioc[2];
    uint32_t mask;
    enum {idle, listening, connecting, connected} socket_stat;
#endif

    struct fd_struct* fds;
    struct my_buffer* read_mbuf;
    struct my_buffer* write_mbuf;
    struct sockaddr_in* addr;
} fd_contex;

static lock_t fd_contex_lck = lock_initial;
static struct list_head fd_contex_head = LIST_HEAD_INIT(fd_contex_head);

static fd_contex* fd_contex_get()
{
    uint32_t current = now();
    fd_contex* ctx = NULL;
    lock(&fd_contex_lck);
    struct list_head* ent = fd_contex_head.next;
    if (!list_empty(ent)) {
        ctx = list_entry(ent, fd_contex, set_entry);
        uintptr_t* until = (uintptr_t *) &ctx->fds;
        if (current > *until) {
            list_del_init(ent);
        } else {
            ctx = NULL;
        }
    }
    unlock(&fd_contex_lck);

    if (ctx == NULL) {
        ctx = (fd_contex *) my_malloc(sizeof(fd_contex));
    }
    return ctx;
}

static void fd_contex_put(fd_contex* ctx)
{
    uintptr_t* until = (uintptr_t *) &ctx->fds;
    *until = now() + 3000;

    lock(&fd_contex_lck);
    list_add_tail(&ctx->set_entry, &fd_contex_head);
    unlock(&fd_contex_lck);
}

#endif
