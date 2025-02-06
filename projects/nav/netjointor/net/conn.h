#ifndef conn_h
#define conn_h

#include "fdset_in.h"
#include "tcp.h"
#include "now.h"
#include "my_errno.h"
#include "lock.h"

#ifndef _MSC_VER
#include <unistd.h>
#include <sys/socket.h>
#endif

struct connector {
    struct fd_struct* fds;
    struct list_head entry;
    struct slot* slot_ptr;
    struct netaddr* addr;
    my_handle* handle;
    intptr_t wakeup;

    tcp_context* ctx;
    void (*setup_fds) (tcp_context*, struct fd_struct*, my_handle*, struct my_buffer**);
};

struct slot {
    struct fd_struct* fds;
    lock_t lck;
    int ref;
};

static struct slot* slot_alloc(struct slot* slot_ptr)
{
    if (slot_ptr == NULL) {
        slot_ptr = (struct slot *) my_malloc(sizeof(*slot_ptr));
        if (slot_ptr != 0) {
            slot_ptr->lck = lock_val;
            slot_ptr->fds = NULL;
            slot_ptr->ref = 1;
        }
    } else {
        __sync_add_and_fetch(&slot_ptr->ref, 1);
    }
    return slot_ptr;
}

static void slot_free(struct slot* slot_ptr)
{
    int n = __sync_sub_and_fetch(&slot_ptr->ref, 1);
    if (n == 0) {
        my_free(slot_ptr);
    }
}

static void connector_put(struct connector* ctor)
{
    if (ctor->fds != NULL) {
        my_close(ctor->fds->fd);
        my_free(ctor->fds);
    }

    lock(&ctor->slot_ptr->lck);
    int32_t empty = list_empty(&ctor->entry);
    if (!empty) {
        list_del(&ctor->entry);
    }
    unlock(&ctor->slot_ptr->lck);

    netaddr_free(ctor->addr);
    slot_free(ctor->slot_ptr);

    if (!empty) {
        handle_release(ctor->handle);
    }
    my_free(ctor);
}

static void dettach(struct fd_struct* fds)
{
    struct connector* ctor = (struct connector *) fds->priv;
    connector_put(ctor);
}

static int32_t conect_read(struct fd_struct* fds, struct my_buffer* mbuf, int err, struct sockaddr_in* in)
{
    (void) (in);
    struct connector* ctor = (struct connector *) fds->priv;
    my_assert(mbuf == NULL);

    if (err != 0 || -1 == check_connection(fds->fd)) {
        handle_clone(ctor->handle);
        handle_dettach(ctor->handle);
        return -1;
    }

    int b = __sync_bool_compare_and_swap((void **) &ctor->wakeup, (void *) 0, (void *) 1);
    if (!b) {
        handle_clone(ctor->handle);
        handle_dettach(ctor->handle);
        return -1;
    }
    // windows will call this when connected, and linux&freebsd will call conect_write
    // windows is different because we need issule a read for completion port to listen
    // the connection eof message before any data arrived.
    //
    // if __sync_bool_compare_and_swap return true, meaning we notify
    // connect_media_server connection has been established, then connect_media_server
    // may wake up and free ctor. so we need save pointers we will use before
    // call __sync_bool_compare_and_swap.
    // if __sync_bool_compare_and_swap fails, connect_media_server delegate the free thing
    // via dettach, it's sure ctor will still avaliable here.
    void (*setup_fds) (tcp_context*, struct fd_struct*, my_handle*, struct my_buffer**) = ctor->setup_fds;
    tcp_context* ctx = ctor->ctx;
    my_handle* handle = ctor->handle;

    b = __sync_bool_compare_and_swap((void **) &ctor->slot_ptr->fds, (void *) NULL, (void *) fds);
    if (!b) {
        handle_clone(ctor->handle);
        handle_dettach(ctor->handle);
        return -1;
    }

    setup_fds(ctx, fds, handle, NULL);
    return fds->bytes;
}

static struct my_buffer* conect_write(struct fd_struct* fds, struct sockaddr_in** in, int* err)
{
    (void) in;
    struct connector* ctor = (struct connector *) fds->priv;
    if (err != NULL || -1 == check_connection(fds->fd)) {
        handle_clone(ctor->handle);
        handle_dettach(ctor->handle);
        return NULL;
    }

    int b = __sync_bool_compare_and_swap((void **) &ctor->wakeup, (void *) 0, (void *) 1);
    if (!b) {
        handle_clone(ctor->handle);
        handle_dettach(ctor->handle);
        return NULL;
    }
    // if __sync_bool_compare_and_swap return true, meaning we notify
    // connect_media_server connection has been established, then connect_media_server
    // may wake up and free ctor. so we need save pointers we will use before
    // call __sync_bool_compare_and_swap
    // if __sync_bool_compare_and_swap fails, connect_media_server delegate the free thing
    // via dettach, it's sure ctor will still avaliable here.
    void (*setup_fds) (tcp_context*, struct fd_struct*, my_handle*, struct my_buffer**) = ctor->setup_fds;
    tcp_context* ctx = ctor->ctx;
    my_handle* handle = ctor->handle;

    struct my_buffer* mbuf = NULL;
    b = __sync_bool_compare_and_swap((void **) &ctor->slot_ptr->fds, (void *) NULL, (void *) fds);
    if (!b) {
        handle_clone(ctor->handle);
        handle_dettach(ctor->handle);
    } else {
        setup_fds(ctx, fds, handle, &mbuf);
    }
    return mbuf;
}

static struct fds_ops connector_ops = {
    conect_read,
    NULL,
    conect_write,
    dettach,
    NULL,
    NULL
};

static struct connector* connector_get(struct netaddr* addr, struct slot* slot_ptr)
{
    struct connector* ctor = (struct connector *) my_malloc(sizeof(struct connector));
    if (ctor == NULL) {
        return NULL;
    }

    ctor->fds = (struct fd_struct *) my_malloc(sizeof(struct fd_struct));
    if (ctor->fds == NULL) {
        my_free(ctor);
        return NULL;
    }

    ctor->fds->fd = my_socket(AF_INET, SOCK_STREAM, 0);
    if (ctor->fds->fd == -1) {
        my_free(ctor->fds);
        my_free(ctor);
        return NULL;
    }

    if (-1 == make_none_block(ctor->fds->fd)) {
        my_close(ctor->fds->fd);
        my_free(ctor->fds);
        my_free(ctor);
        return NULL;
    }

    ctor->fds->tcp0_udp1 = 0;
    ctor->fds->mask = EPOLLOUT;
    ctor->fds->bytes = 0;
    ctor->fds->bye = NULL;
    ctor->fds->fop = &connector_ops;
    ctor->fds->priv = ctor;

    ctor->addr = addr;
    __sync_add_and_fetch(&addr->ref, 1);
    ctor->slot_ptr = slot_alloc(slot_ptr);
    ctor->wakeup = 0;
    return ctor;
}

int connect_media_server(tcp_context* ctx, uint32_t ms,
                         void (*setup_fds) (tcp_context*, struct fd_struct*, my_handle*, struct my_buffer**))
{
    errno = ENOMEM;
    if (ms != (uint32_t) -1) {
        uint32_t current = now();
        if (current + ms < ms) {
            ms = (uint32_t) -1;
        } else {
            ms += now();
        }
    }

    struct list_head head;
    INIT_LIST_HEAD(&head);
    struct slot* slot_ptr = slot_alloc(NULL);
    if (slot_ptr == NULL) {
        return -1;
    }
    struct connector* ctor = NULL;

    struct tcp_addr* addr = ctx->addr[0];
    for (int i = 0; i < addr->nr_addr; ++i) {
        ctor = connector_get(addr->addr[i], slot_ptr);
        if (ctor == NULL) {
            break;
        }

        ctor->handle = NULL;
        ctor->setup_fds = setup_fds;
        ctor->ctx = ctx;
        list_add_tail(&ctor->entry, &head);
    }

    struct list_head* ent;
    for (ent = head.next; ent != &head;) {
        ctor = list_entry(ent, struct connector, entry);
        ent = ent->next;

        ctor->handle = fdset_make_handle(ctx->fset, ctor->fds);
        if (ctor->handle == NULL) {
            // to avoid call handle_release in connector_put
            list_del_init(&ctor->entry);
            connector_put(ctor);
            continue;
        }

        int n = fdset_attach_handle(ctx->fset, ctor->handle);
        if (n == -1) {
            list_del_init(&ctor->entry);
            handle_dettach(ctor->handle);
            continue;
        }

        // if ptr returned NULL, then something has happened
        // and the handle has been dettached. we are sure conector_put
        // will absolutely being called, and every cleaning will do or has
        // done in it. or else, we has forbiden conector_put being called
        // until handle_put, in this case and connect failed, we can avoid
        // conector_put doing the extra handle_dettach(ctor->handle).
        // when connect failed, we are not sure whether or not fdset can be
        // invoked to detect it, epoll/kevent/iocp actions is various depending
        // kinds of internal states. while, handle_dettach here force the
        // cleaning up happens.
        void* ptr = handle_get(ctor->handle);
        if (ptr != NULL) {
            n = do_connect(ctor->fds->fd, ctor->addr->ip, ctor->addr->port);
            if (n == -1 && EINPROGRESS != errno) {
                n = __sync_bool_compare_and_swap((void **) &ctor->wakeup, (void *) 0, (void *) 1);
                if (n) {
                    list_del_init(&ctor->entry);
                    // save handle because handle_put may free ctor
                    my_handle* handle = ctor->handle;
                    handle_put(handle);
                    handle_dettach(handle);
                } else {
                    handle_put(ctor->handle);
                }
            } else {
                handle_put(ctor->handle);
            }
        }
    }

    lock(&slot_ptr->lck);
    if (list_empty(&head)) {
        unlock(&slot_ptr->lck);
        slot_free(slot_ptr);
        return -1;
    }
    unlock(&slot_ptr->lck);

    errno = ETIMEDOUT;
    while (now() < ms) {
        if (slot_ptr->fds != NULL) {
            break;
        }
        usleep(50 * 1000);
    }
    __sync_bool_compare_and_swap((void **) &slot_ptr->fds, (void *) NULL, (void *) -1);

    int n = -1;
    lock(&slot_ptr->lck);
    while (!list_empty(&head)) {
        ent = head.next;
        list_del_init(ent);
        unlock(&slot_ptr->lck);

        ctor = list_entry(ent, struct connector, entry);
        if (ctor->fds == slot_ptr->fds) {
            ctor->fds = NULL;
            connector_put(ctor);
            n = 0;
        } else {
            handle_dettach(ctor->handle);
        }
        lock(&slot_ptr->lck);
    }
    unlock(&slot_ptr->lck);
    slot_free(slot_ptr);

    return n;
}

#endif
