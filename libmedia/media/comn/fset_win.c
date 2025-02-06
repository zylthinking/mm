
#ifdef _MSC_VER
#include "fdset.h"
#include "fdset_in.h"
#include "fdctxpool.h"
#include "mbuf.h"

#define stat_ready 0
#define stat_busy_proc 1
#define stat_busy_wait 2
#define timer_ident ((uintptr_t) -1)

#define fdset_kill ((LPOVERLAPPED) 1)
#define wake_message ((LPOVERLAPPED) 2)

typedef struct tag_fdset {
    HANDLE fd;
    lock_t fdctx_lck, timer_lck, pending_lck;
    struct list_head fdctx_head;
    struct list_head timer_head;

    uint32_t cycle;
    lock_t task_lck;
    struct list_head task_pool;
    struct list_head pending;
} fdset;

typedef struct tag_timer_notify {
    uint32_t id;
    uint32_t tms;
    my_handle* fdctx;
    struct list_head fdset_entry;
    struct list_head fdctx_entry;
} timer_notify;

static void add_to_pending(fdset* fset, io_context* ioc)
{
    my_assert(ioc != NULL && ioc->base != hand_shake);
    my_assert(ioc != (io_context *) wake_message);

    lock(&fset->pending_lck);
    list_add(&ioc->entry, &fset->pending);
    unlock(&fset->pending_lck);
}

static void del_from_pending(fdset* fset, io_context* ioc)
{
    if (ioc == (io_context *) wake_message
        || ioc == NULL || ioc->base == hand_shake)
    {
        return;
    }

    lock(&fset->pending_lck);
    list_del(&ioc->entry);
    unlock(&fset->pending_lck);
}

static void release_io_context(io_context* ioc, uint32_t include_buffer)
{
    if (include_buffer) {
        ioc->base->mop->free(ioc->base);
    }

    if (ioc->flag != 1) {
        if (ioc->overlap.hEvent != NULL) {
            CloseHandle(ioc->overlap.hEvent);
        }
        my_free(ioc);
    } else {
        ioc->io_type &= 1;
        ioc->base = NULL;
        ioc->wsa_buffer.buf = NULL;
        ioc->wsa_buffer.len = 0;
    }
}

static void free_pending(fdset* fset)
{
    while (!list_empty(&fset->pending)) {
        struct list_head* ent = fset->pending.next;
        list_del(ent);

        io_context* ioc = list_entry(ent, io_context, entry);
        release_io_context(ioc, hand_shake != ioc->base);
    }
}

static inline int fdset_add_fd(HANDLE fd, fd_contex* fdctx)
{
    int n = 0;
    uint32_t mask = (fdctx->fds->mask & fdctx->mask & EPOLLOUT);

    if (mask != 0 && fdctx->write_mbuf == NULL) {
        BOOL b = PostQueuedCompletionStatus(fd, 0, (DWORD) fdctx, wake_message);
        if (!b) {
            n = -1;
        }
    }
    return n;
}

uint32_t* sched_handle_alloc(uint32_t id)
{
    timer_notify* notify = (timer_notify *) my_malloc(sizeof(timer_notify));
    if (notify != NULL) {
        notify->id = id;
    }
    return (uint32_t *) notify;
}

void sched_handle_free(uint32_t* ptr)
{
    my_free(ptr);
}

static void release_all_timer(fdset* fset, struct list_head* headp)
{
    while (!list_empty(headp)) {
        timer_notify* notify = list_entry(headp->next, timer_notify, fdset_entry);
        list_del(headp->next);

        my_handle* handle = (my_handle *) notify->fdctx;
        fd_contex* fdctx = (fd_contex *) handle_get(handle);
        if (fdctx != NULL) {
            my_assert(headp != &fset->timer_head);

            int stat = sync_status(fdctx);
            if (stat == stat_ready) {
                while (fdctx->busy) {
                    sched_yield();
                }
                fdctx->fds->fop->notify(fdctx->fds, &notify->id);
                fdset_add_fd(fset->fd, fdctx);
                fdctx->stat = stat_ready;
            }

            handle_put(handle);
            handle_release(handle);
        } else {
            handle_release(handle);
            my_free(notify);
        }
    }
}

static void fset_delete(void* addr)
{
    fdset* fset = (fdset *) addr;

    while (!list_empty(&fset->fdctx_head)) {
        my_handle* handle = NULL;
        struct list_head* ent = fset->fdctx_head.next;
        fd_contex* fdctx = list_entry(ent, fd_contex, set_entry);

        // fdctx will never be freed, so we can acess it
        // even it mybe have been "released"
        // we need list_del_init here we make sure
        // only one of this or fd_contex_free can do the real work.
        // 
        // we reach here because fd_contex_free does not call list_del_init
        // yet when we get execute ent = fset->fdctx_head.next;
        // but maybe the fdctx have then been freed. 
        // if the fdctx was reused from the fdctx_pool before the handle_dettach
        // below, handle maybe another fdctx->self when added to another fset.
        // and list_del_init maybe do bad things.
        // in one word, we need make sure fdctx will not be resued in a time long enough
        // in fact, that is why fdctx_pool was introduced.
        lock(&fdctx->set_lck);
        int empty = list_empty(ent);
        if (!empty) {
            handle = fdctx->self;
            list_del_init(ent);
        }
        unlock(&fdctx->set_lck);

        if (!empty) {
            shutdown(fdctx->fds->fd, SD_BOTH);
            handle_dettach(handle);
        }
    }

    PostQueuedCompletionStatus(fset->fd, 0, 0, fdset_kill);
    release_all_timer(fset, &fset->timer_head);
    free_buffer(&fset->task_pool);

    free_pending(fset);
    my_free(fset);
}

void* fdset_new(uint32_t ms_cycle)
{
    HANDLE fd = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (fd == NULL) {
        return NULL;
    }

    if (ms_cycle == (uint32_t) -1) {
        ms_cycle = 10;
    }

    errno = ENOMEM;
    fdset* fset = (fdset *) my_malloc(sizeof(fdset));
    if (fset == NULL) {
        CloseHandle(fd);
        return NULL;
    }

    my_handle* handle = handle_attach(fset, fset_delete);
    if (handle == NULL) {
        my_free(fset);
        CloseHandle(fd);
        return NULL;
    }

    INIT_LIST_HEAD(&fset->fdctx_head);
    INIT_LIST_HEAD(&fset->timer_head);
    INIT_LIST_HEAD(&fset->task_pool);
    INIT_LIST_HEAD(&fset->pending);
    fset->fdctx_lck = lock_val;
    fset->timer_lck = lock_val;
    fset->task_lck = lock_val;
    fset->pending_lck = lock_val;
    fset->fd = fd;
    fset->cycle = ms_cycle;
    return (void *) handle;
}

static void unlink_from_fdset(fdset* fset, fd_contex* fdctx, int stat)
{
    lock(&fset->fdctx_lck);
    list_del(&fdctx->set_entry);
    unlock(&fset->fdctx_lck);

    if (stat == stat_busy_wait) {
        my_assert(list_empty(&fdctx->timer_head));
        return;
    }

    struct list_head* ent = fdctx->timer_head.next;
    if (!list_empty(ent)) {
        lock(&fset->timer_lck);
        for (; ent != &fdctx->timer_head; ent = ent->next) {
            timer_notify* notify = list_entry(ent, timer_notify, fdctx_entry);
            list_del(&notify->fdset_entry);
        }
        unlock(&fset->timer_lck);

        while (!list_empty(&fdctx->timer_head)) {
            // timer_head only can be append after being added to fset
            my_assert(stat == stat_ready);

            ent = fdctx->timer_head.next;
            list_del(ent);
            timer_notify* notify = list_entry(ent, timer_notify, fdctx_entry);
            handle_release(notify->fdctx);
            my_free(notify);
        }
    }
    shutdown(fdctx->fds->fd, SD_BOTH);
}

static void free_write_buffer(int stat, fd_contex* fdctx)
{
    if (stat != stat_ready) {
        return;
    }

    struct fd_struct* fds = fdctx->fds;
    int dirty = (fdctx->write_mbuf != NULL && fds->bye != NULL && fds->tcp0_udp1 == 0);

    while (dirty) {
        int n = do_tcp_write(fdctx->fds->fd, fdctx->write_mbuf);
        dirty = (fdctx->write_mbuf->length != 0);
        if (dirty == 0 || n == -1) {
            break;
        }
        usleep(1000);
    }

    if (fdctx->write_mbuf != NULL) {
        fdctx->write_mbuf->mop->free(fdctx->write_mbuf);
    }

    while (dirty == 0 && fds->bye != NULL) {
        int n = do_tcp_write(fdctx->fds->fd, fds->bye);
        if (fds->bye->length == 0 || n == -1) {
            break;
        }
        usleep(1000);
    }
}

static void fd_contex_free(void* addr)
{
    int stat;
    fd_contex* fdctx = (fd_contex *) addr;
    while (1) {
        stat = __sync_val_compare_and_swap(&fdctx->stat, stat_ready, stat_busy_wait);
        // stat == stat_busy_wait means the fdctx didn't exist in fset
        if (stat != stat_busy_proc) {
            break;
        }
        sched_yield();
    }

    fdset* fset = (fdset *) handle_get(fdctx->hset);
    if (fset != NULL) {
        unlink_from_fdset(fset, fdctx, stat);
        handle_release(fdctx->self);
        handle_put(fdctx->hset);
    } else {
        lock(&fdctx->set_lck);
        int empty = list_empty(&fdctx->set_entry);
        if (!empty) {
            list_del_init(&fdctx->set_entry);
        }
        unlock(&fdctx->set_lck);

        if (!empty) {
            handle_release(fdctx->self);
        }
    }
    handle_release(fdctx->hset);

    if (fdctx->read_mbuf != NULL) {
        fdctx->read_mbuf->mop->free(fdctx->read_mbuf);
    }

    free_write_buffer(stat, fdctx);
    if (stat == stat_ready) {
        struct fd_struct* fds = fdctx->fds;
        fds->fop->detach(fds);
    }

    INIT_LIST_HEAD(&fdctx->timer_head);
    fd_contex_put(fdctx);
}

static fd_contex* fd_make_context(my_handle* hset, fdset* fset, struct fd_struct* fds)
{
    errno = ENOMEM;

    fd_contex* fdctx = fd_contex_get();
    if (fdctx == NULL) {
        return NULL;
    }

    my_handle* handle = handle_attach(fdctx, fd_contex_free);
    if (handle == NULL) {
        fd_contex_put(fdctx);
        return NULL;
    }

    fdctx->self = handle;
    handle_clone(hset);
    fdctx->hset = hset;

    fdctx->fds = fds;
    fdctx->read_mbuf = NULL;
    fdctx->write_mbuf = NULL;
    fdctx->stat = stat_busy_wait;
    fdctx->busy = 0;
    fdctx->busy_lck = lock_val;
    fdctx->set_lck = lock_val;
    fdctx->socket_stat = fd_contex::idle;
    fdctx->mask = EPOLLIN | EPOLLOUT;

    memset(fdctx->ioc, 0, sizeof(fdctx->ioc));
    fdctx->ioc[0].io_type = 0;
    fdctx->ioc[1].io_type = 1;
    fdctx->ioc[0].flag = 1;
    fdctx->ioc[1].flag = 1;

    INIT_LIST_HEAD(&fdctx->set_entry);
    INIT_LIST_HEAD(&fdctx->timer_head);

    lock(&fset->fdctx_lck);
    list_add(&fdctx->set_entry, &fset->fdctx_head);
    unlock(&fset->fdctx_lck);
    return fdctx;
}

my_handle* fdset_make_handle(void* set, struct fd_struct* fds)
{
    if (fds->fd == -1) {
        errno = EINVAL;
        return NULL;
    }

    my_handle* hset = (my_handle *) set;
    fdset* fset = (fdset *) handle_get(hset);
    if (fset == NULL) {
        errno = EBADF;
        return NULL;
    }

    my_handle* handle = NULL;
    fd_contex* fdctx = fd_make_context(hset, fset, fds);
    if (fdctx != NULL) {
        handle = fdctx->self;
        handle_clone(handle);
    }
    handle_put(hset);
    return handle;
}

static int sync_status(fd_contex* fdctx)
{
    int stat;

    do {
        lock(&fdctx->busy_lck);
        stat = __sync_val_compare_and_swap(
                    &fdctx->stat, stat_ready, stat_busy_proc);
        unlock(&fdctx->busy_lck);

        if (stat != stat_busy_proc) {
            break;
        }
        usleep(1000);
    } while (1);

    return stat;
}

static void timer_expires(fdset* fset)
{
    uint32_t tms = now();
    struct list_head *ent, head;
    INIT_LIST_HEAD(&head);

    lock(&fset->timer_lck);
    for (ent = fset->timer_head.next; ent != &fset->timer_head;) {
        timer_notify* notify = list_entry(ent, timer_notify, fdset_entry);
        if (notify->tms > tms) {
            break;
        }

        ent = ent->next;
        list_del(&notify->fdset_entry);
        list_del(&notify->fdctx_entry);
        list_add(&notify->fdset_entry, &head);
    }
    unlock(&fset->timer_lck);
    release_all_timer(fset, &head);
}

static struct my_buffer* proto_buffer_alloc(void* fdctx, uint32_t bytes)
{
    fd_contex* ctx = (fd_contex *) fdctx;
    struct my_buffer* mbuf = NULL;

    struct fd_struct* fds = ctx->fds;
    uint32_t needed = roundup(task_bytes + bytes, sizeof(char *));
    if (fds->fop->buffer_get != NULL) {
        mbuf = fds->fop->buffer_get(fds, needed);
    } else {
        mbuf = mbuf_alloc_3(needed);
    }

    if (mbuf != NULL) {
        mbuf->length = bytes;
    }

    return mbuf;
}

static int io_write(fdset* fset, fd_contex* fdctx)
{
    int n;
    struct sockaddr_in* in = NULL;
    struct my_buffer* mbuf = NULL;

    while (1) {
        // sync_status assure only one thread reach here
        if (fdctx->write_mbuf != NULL) {
            mbuf = fdctx->write_mbuf;
            fdctx->write_mbuf = NULL;
        } else {
            mbuf = fdctx->fds->fop->write(fdctx->fds, &in, 0);
        }

        if (mbuf == NULL) {
            n = 0;
            break;
        }

        io_context* ioc = (io_context *) my_malloc(sizeof(io_context));
        if (ioc != NULL) {
            memset(ioc, 0, sizeof(io_context));
        } else if (fdctx->ioc[0].io_type == 0) {
            ioc = &fdctx->ioc[0];
        } else {
            n = 0;
            fdctx->write_mbuf = mbuf;
            break;
        }

        ioc->io_type = 2;
        ioc->base = mbuf;
        ioc->wsa_buffer.buf = mbuf->ptr[1];
        ioc->wsa_buffer.len = mbuf->length;

        errno = 0;
        if (fdctx->fds->tcp0_udp1 == 0) {
            n = do_tcp_write(fdctx->fds->fd, (struct my_buffer *) ioc);
        } else {
            n = do_udp_write(fdctx->fds->fd, (struct my_buffer *) ioc, in);
        }

        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            release_io_context(ioc, 0);
            fdctx->write_mbuf = mbuf;
            n = 0;
            break;
        }

        if (n == -1) {
            my_assert(hand_shake != ioc->base);
            release_io_context(ioc, 1);
            break;
        }
        add_to_pending(fset, ioc);
    }
    return n;
}

static struct my_buffer* do_tcp_accept(fd_contex* fdctx, int fd, io_context* ioc)
{
    errno = EFAILED;

    DWORD bytes;
    static LPFN_ACCEPTEX lpfnAcceptEx = NULL;
    if (lpfnAcceptEx == NULL) {
        GUID GuidAcceptEx = WSAID_ACCEPTEX;
        int n = WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                         &GuidAcceptEx, sizeof (GuidAcceptEx),
                         &lpfnAcceptEx, sizeof (lpfnAcceptEx),
                         &bytes, NULL, NULL);
        if (n == SOCKET_ERROR) {
            return NULL;
        }
    }

    // ms says the buffer at least be 16 bytes large than sockaddr
    // give it 64 bytes should be enough
    DWORD addr_bytes = sizeof(struct sockaddr) + 64 + sizeof(int64_t);
    if (fdctx->read_mbuf == NULL) {
        fdctx->read_mbuf = mbuf_alloc_2(addr_bytes * 2);
    }

    if (fdctx->read_mbuf == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    SOCKET s = my_socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        return NULL;
    }

    int64_t* sp = (int64_t *) fdctx->read_mbuf->ptr[0];
    *sp = (int64_t) s;
    fdctx->read_mbuf->ptr[1] += sizeof(int64_t);
    fdctx->read_mbuf->length -= sizeof(int64_t);
    ioc->base = fdctx->read_mbuf;
    ioc->io_type = 3;

LABEL:
    BOOL b = lpfnAcceptEx(fd, s, fdctx->read_mbuf->ptr[1], 0, addr_bytes, addr_bytes, &bytes, &ioc->overlap);
    if (b) {
        release_io_context(ioc, 0);
        return fdctx->read_mbuf;
    }

    int err = WSAGetLastError();
    if (err == ERROR_IO_PENDING) {
        errno = 0;
    } else if (err == WSAECONNRESET) {
        goto LABEL;
    } else {
        my_close((int) s);
    }
    return NULL;
}

static int io_read(fdset* fset, fd_contex* fdctx, io_context* ioc)
{
    struct fd_struct* fds = fdctx->fds;
    struct my_buffer* mbuf = NULL;
    struct sockaddr in, *inptr = NULL;

    int n = 0;
    if (ioc != NULL) {
        mbuf = ioc->base;
        in = ioc->addr;
        release_io_context(ioc, 0);
    }
    ioc = &fdctx->ioc[1];
    my_assert(ioc->io_type == 1);

    if (mbuf != NULL) {
        if (hand_shake == mbuf) {
            mbuf = NULL;
        } else if (mbuf->length == 0 || fds->tcp0_udp1 == 1) {
            mbuf->length = (uintptr_t) (mbuf->ptr[1] - mbuf->ptr[0]);
            mbuf->ptr[1] = mbuf->ptr[0];
        } else {
            goto LABEL3;
        }
LABEL2:
        n = fds->fop->read(fds, mbuf, 0, (struct sockaddr_in *) inptr);
        if (n == -1) {
            return -1;
        }
        my_assert(n >= 0);
    }
LABEL1:
    if (n > 0) {
        mbuf = proto_buffer_alloc(fdctx, n);
        my_assert(mbuf->ptr[0] == mbuf->ptr[1]);
    } else {
        mbuf = NULL;
    }

LABEL3:
    errno = 0;
    if (mbuf != NULL) {
        ioc->io_type = 3;
        ioc->base = mbuf;
        ioc->wsa_buffer.buf = mbuf->ptr[1];
        ioc->wsa_buffer.len = mbuf->length;

        if (fds->tcp0_udp1 == 0) {
            n = do_tcp_read(fds->fd, (struct my_buffer *) ioc);
        } else {
            n = do_udp_read(fds->fd, (struct my_buffer *) ioc, NULL);
        }
    } else if (n == 0) {
        mbuf = do_tcp_accept(fdctx, fds->fd, ioc);
        if (mbuf != NULL) {
            goto LABEL2;
        }
    } else {
        errno = ENOMEM;
    }
    my_assert(errno != EWOULDBLOCK && errno != EAGAIN);

    if (errno != 0) {
        my_assert(hand_shake != ioc->base);
        release_io_context(ioc, 1);
        n = fds->fop->read(fds, NULL, errno, NULL);
        if (n >= 0) {
            goto LABEL1;
        }
    } else {
        add_to_pending(fset, ioc);
    }
    return n;
}

static void io_proc(fdset* fset, fd_contex* fdctx, LPOVERLAPPED ovlptr, int io_type, BOOL failed)
{
    struct fd_struct* fds = fdctx->fds;
    io_context* ioc = (io_context *) ovlptr;

    if (wake_message == ovlptr) {
        ioc = NULL;
    } else {
        io_type = ioc->io_type - 2;
    }

    if (io_type == 0) {
        if (ioc != NULL) {
            my_assert(hand_shake != ioc->base);
            if (ioc->base->length == 0) {
                ioc->base->length = (uint32_t) (ioc->base->ptr[1] - ioc->base->ptr[0]);
                ioc->base->ptr[1] = ioc->base->ptr[0];
            } else {
                my_assert(fds->tcp0_udp1 == 0);
            }
            release_io_context(ioc, 1);
            ioc = NULL;
        }

        if (failed) {
            fdctx->mask &= ~EPOLLOUT;
            struct my_buffer* mbuf = fds->fop->write(fds, NULL, EPIPE);
            if (__builtin_expect(mbuf != NULL, 0)) {
                mbuf->mop->free(mbuf);
            }
        } else if (fdctx->mask & EPOLLOUT) {
            if (-1 == io_write(fset, fdctx)) {
                fds->fop->read(fds, NULL, errno, NULL);
                fdctx->mask &= ~EPOLLOUT;
            }
        }
    } else if (io_type == 1) {
        if (failed) {
            fdctx->mask &= ~EPOLLIN;
            fds->fop->read(fds, NULL, EFAILED, NULL);
            // when failed, ioc != NULL
            release_io_context(ioc, hand_shake != ioc->base);
        } else if (fdctx->mask & EPOLLIN) {
            if (-1 == io_read(fset, fdctx, ioc)) {
                fdctx->mask &= ~EPOLLIN;
            }
        } else if (ioc != NULL) {
            release_io_context(ioc, hand_shake != ioc->base);
        }
    }
}

static void looper(void* any, HANDLE fd, DWORD cycle)
{
    DWORD bytes;
    ULONG_PTR key;
    LPOVERLAPPED overlap_ptr = NULL;
    BOOL result = FALSE;
    my_handle* handle = (my_handle *) any;

    while (1) {
        overlap_ptr = NULL;
        result = GetQueuedCompletionStatus(fd, &bytes, &key, &overlap_ptr, cycle);
        fdset* fset = (fdset *) handle_get(handle);
        if (__builtin_expect(fset == NULL, 0)) {
            break;
        }

        my_assert(overlap_ptr != fdset_kill);
        io_context* ioc = (io_context *) overlap_ptr;
        del_from_pending(fset, ioc);
        fd_contex* fdctx = (fd_contex *) key;

        if (!result) {
            if (overlap_ptr == NULL) {
                DWORD err = GetLastError();
                if (err == WAIT_TIMEOUT) {
                    timer_expires(fset);
                }
                handle_put(handle);
                continue;
            }
        } else if (overlap_ptr != wake_message) {
            // ok, xp sp3 shows if closesocket, then result will be false
            // just comment the code, while, msdn said result will be true
            // and bytes should be 0 at this case. I don't know whether what msdn
            // says exists on some other windows
            // another notice: acceptex will get result be true and bytes be zero when succeed
            if (fdctx->socket_stat == fd_contex::connected) {
                if (bytes == 0) {
                    result = FALSE;
                } else {
                    struct my_buffer* mbuf = ioc->base;
                    mbuf->ptr[1] += bytes;
                    mbuf->length -= bytes;
                }
            } else if (fdctx->socket_stat == fd_contex::connecting) {
                fdctx->socket_stat = fd_contex::connected;
                my_assert(bytes == 0);
            }
        }

        if (stat_ready == sync_status(fdctx)) {
            // we need clone fdctx->self and cache it to h
            // because there is a window between fdctx->stat = stat_ready and handle_put(h)
            // in which fdctx->self may be freed
            my_handle* h = fdctx->self;
            handle_clone(h);

            // sync_status suspend fd_contex_free
            // while it can't forbide handle_dettach(fdctx->self);
            void* ptr = handle_get(h);
            if (ptr == NULL) {
                fdctx->stat = stat_ready;
                goto LABEL;
            }

            while (fdctx->busy) {
                sched_yield();
            }

            io_proc(fset, fdctx, overlap_ptr, bytes, !result);
            fdset_add_fd(fset->fd, fdctx);
            fdctx->stat = stat_ready;
            handle_put(h);
        LABEL:
            handle_release(h);
        } else if (overlap_ptr != wake_message) {
            release_io_context(ioc, hand_shake != ioc->base);
        }
        handle_put(handle);
    }

    handle_release(handle);
    CloseHandle(fd);
}

int fdset_join1(void* set, volatile int32_t* flag)
{
    my_assert(*flag == 0);
    my_handle* hset = (my_handle *) set;
    fdset* fset = (fdset *) handle_get(hset);
    if (fset == NULL) {
        *flag = 1;
        errno = EBADF;
        return -1;
    }

    DWORD cycle = (DWORD) fset->cycle;
    HANDLE fd = fset->fd;
    handle_clone(hset);
    *flag = 1;

    handle_put(hset);
    looper(hset, fd, cycle);
    return 0;
}

static int fdset_add_fdctx(fdset* fset, fd_contex* fdctx)
{
    if (fdctx->stat != stat_busy_wait) {
        errno = EEXIST;
        return -1;
    }
    struct fd_struct* fds = fdctx->fds;

    BOOL b = FALSE;
    if (fds->tcp0_udp1 == 0) {
        int size = (int) sizeof(b);
        int n = getsockopt((SOCKET) fds->fd, SOL_SOCKET, SO_ACCEPTCONN, (char *) &b, &size);
        if (n == -1) {
            errno = EFAILED;
            return -1;
        }
    }

    HANDLE handle = CreateIoCompletionPort((HANDLE) fds->fd, fset->fd, (DWORD) fdctx, 0);
    if (handle == NULL) {
        errno = EFAILED;
        return -1;
    }

    if (b) {
        fdctx->socket_stat = fd_contex::listening;
        PostQueuedCompletionStatus(fset->fd, 1, (DWORD) fdctx, wake_message);
    } else {
        fdctx->socket_stat = fd_contex::connecting;
        if (fds->bytes > 0) { // accepted tcp socket or udp socket
            io_context* ioc = &fdctx->ioc[0];
            ioc->io_type = 3;
            ioc->base = hand_shake;
            PostQueuedCompletionStatus(fset->fd, 0, (DWORD) fdctx, &ioc->overlap);

            if (fds->mask & EPOLLOUT) {
                PostQueuedCompletionStatus(fset->fd, 0, (DWORD) fdctx, wake_message);
            }
        } else if (fds->tcp0_udp1 == 1) { // udp socket
            if (fds->mask & EPOLLOUT) {
                PostQueuedCompletionStatus(fset->fd, 0, (DWORD) fdctx, wake_message);
            }
        }
    }
    fdctx->stat = stat_ready;
    return 0;
}

my_handle* fdset_attach_fd(void* set, struct fd_struct* fds)
{
    if (fds->fd == -1) {
        errno = EINVAL;
        return NULL;
    }

    my_handle* hset = (my_handle *) set;
    fdset* fset = (fdset *) handle_get(hset);
    if (fset == NULL) {
        errno = EBADF;
        return NULL;
    }

    my_handle* handle = NULL;
    fd_contex* fdctx = fd_make_context(hset, fset, fds);

    if (fdctx != NULL) {
        handle_clone(fdctx->self);
        int n = fdset_add_fdctx(fset, fdctx);

        if (0 == n) {
            handle = fdctx->self;
        } else {
            handle_dettach(fdctx->self);
        }
    }

    handle_put(hset);
    return handle;
}

int fdset_attach_handle(void* set, my_handle* handle)
{
    my_handle* hset = (my_handle *) set;
    fdset* fset = (fdset *) handle_get(hset);
    if (fset == NULL) {
        errno = EBADF;
        return -1;
    }

    fd_contex* fdctx = (fd_contex *) handle_get(handle);
    if (fdctx == NULL) {
        errno = EBADF;
        handle_put(hset);
        return -1;
    }

    int n = fdset_add_fdctx(fset, fdctx);
    handle_put(handle);
    handle_put(hset);
    return n;
}

int fdset_update_fdmask(my_handle* handle)
{
    errno = EBADF;
    fd_contex* fdctx = (fd_contex *) handle_get(handle);
    if (fdctx == NULL) {
        return -1;
    }

    fdset* fset = (fdset *) handle_get(fdctx->hset);
    if (fset == NULL) {
        handle_put(handle);
        return -1;
    }

    if (fdctx->stat == stat_busy_wait) {
        errno = ENOENT;
        handle_put(fdctx->hset);
        handle_put(handle);
        return -1;
    }

    struct fd_struct* fds = fdctx->fds;
    lock(&fdctx->busy_lck);
    fdctx->busy = 1;
    unlock(&fdctx->busy_lck);

    if (fds->mask & ~(fdctx->mask)) {
        fdctx->busy = 0;
        handle_put(fdctx->hset);
        handle_put(handle);
        errno = EINVAL;
        return -1;
    }

    if (fdctx->stat != stat_ready) {
        fdctx->busy = 0;
        handle_put(fdctx->hset);
        handle_put(handle);
        return 0;
    }

    errno = EFAILED;
    int n = fdset_add_fd(fset->fd, fdctx);
    fdctx->busy = 0;

    handle_put(fdctx->hset);
    handle_put(handle);
    return n;
}

static int fdset_add_timer(fdset* fset, timer_notify* notify, uint32_t ms)
{
    struct list_head* ent;
    lock(&fset->timer_lck);
    for (ent = fset->timer_head.prev; ent != &fset->timer_head; ent = ent->prev) {
        timer_notify* cur = list_entry(ent, timer_notify, fdset_entry);
        if (cur->tms <= notify->tms) {
            break;
        }
    }

    list_add(&notify->fdset_entry, ent);
    unlock(&fset->timer_lck);
    return 0;
}

int fdset_sched(my_handle* handle, uint32_t ms, uint32_t* id)
{
    timer_notify* notify = (timer_notify *) id;
    if (notify == NULL) {
        errno = EINVAL;
        return -1;
    }

    fd_contex* fdctx = (fd_contex *) handle_get(handle);
    if (fdctx == NULL) {
        errno = EBADF;
        return -1;
    }

    if (fdctx->stat == stat_busy_wait) {
        errno = ENOENT;
        handle_put(handle);
        return -1;
    }

    fdset* fset = (fdset *) handle_get(fdctx->hset);
    if (fset == NULL) {
        errno = ENOENT;
        handle_put(handle);
        return -1;
    }

    notify->tms = now() + ms;
    notify->fdctx = handle;
    handle_clone(handle);

    list_add(&notify->fdctx_entry, &fdctx->timer_head);
    int n = fdset_add_timer(fset, notify, ms);
    if (n == -1) {
        list_del(&notify->fdctx_entry);
        handle_release(handle);
    }

    handle_put(fdctx->hset);
    handle_put(handle);
    return n;
}

struct fd_struct* fd_struct_from(void* any)
{
    if (any == NULL) {
        return NULL;
    }

    fd_contex* fdctx = (fd_contex *) any;
    struct fd_struct* fds = fdctx->fds;
    return fds;
}

static void do_task_proc(struct my_buffer* mbuf)
{
    struct task* task_ptr = task_get(mbuf);
    my_handle* handle = task_ptr->handle;
    fd_contex* fdctx = (fd_contex *) handle_get(handle);
    if (fdctx == NULL) {
        mbuf->mop->free(mbuf);
        return;
    }

    handle_clone(handle);
    struct fd_struct* fds = fdctx->fds;
    int32_t n = fds->fop->dispose(fds, mbuf);
    handle_put(handle);
    if (n == -1) {
        handle_dettach(handle);
    } else {
        handle_release(handle);
    }
}

static void task_proc(my_handle* hset)
{
    struct list_head* pool = NULL;
    lock_t* task_lck = NULL;
    fdset* fset = (fdset *) handle_get(hset);
    if (fset != NULL) {
        pool = &fset->task_pool;
        task_lck = &fset->task_lck;
    }

    while (fset != NULL) {
        struct my_buffer* mbuf = NULL;
        lock(task_lck);
        if (!list_empty(pool)) {
            struct list_head* ent = pool->next;
            list_del(ent);
            mbuf = list_entry(ent, struct my_buffer, head);
        }
        unlock(task_lck);

        if (mbuf != NULL) {
            do_task_proc(mbuf);
        }
        handle_put(hset);

        if (mbuf == NULL) {
            usleep(1000);
        }
        fset = (fdset *) handle_get(hset);
    }
}

int fdset_join2(void* set, volatile int32_t* flag, uint32_t* keep)
{
    my_assert(*flag == 0);
    my_handle* hset = (my_handle *) set;
    fdset* fset = (fdset *) handle_get(hset);
    if (fset == NULL) {
        *flag = 1;
        errno = EBADF;
        return -1;
    }

    handle_clone(hset);
    *flag = 1;

    handle_put(hset);
    task_proc(hset);
    return 0;
}

static void proto_buffer_free(struct my_buffer* mbuf)
{
    struct task* taskptr = task_get(mbuf);
    handle_release(taskptr->handle);
    struct mbuf_operations* mop_ptr = (struct mbuf_operations *) taskptr->mop;
    mbuf->mop = mop_ptr;
    mop_ptr->free(mbuf);
}

static struct my_buffer* proto_buffer_clone(struct my_buffer* mbuf)
{
    struct task* taskptr = task_get(mbuf);
    handle_clone(taskptr->handle);
    struct mbuf_operations* mop_ptr = (struct mbuf_operations *) taskptr->mop;
    mbuf = mop_ptr->clone(mbuf);
    if (mbuf == NULL) {
        handle_release(taskptr->handle);
    }
    return mbuf;
}

static struct mbuf_operations mop = {
    proto_buffer_clone,
    proto_buffer_free
};

void proto_buffer_post(my_handle* handle, struct my_buffer* mbuf)
{
    fd_contex* fdctx = (fd_contex *) handle_get(handle);
    my_assert(fdctx != NULL);

    fdset* fset = (fdset *) handle_get(fdctx->hset);
    my_assert(fset != NULL);

    struct task* taskptr = task_get(mbuf);
    handle_clone(fdctx->self);
    taskptr->handle = fdctx->self;
    taskptr->mop = mbuf->mop;
    mbuf->mop = &mop;

    lock(&fset->task_lck);
    list_add_tail(&mbuf->head, &fset->task_pool);
    unlock(&fset->task_lck);

    handle_put(fdctx->hset);
    handle_put(handle);
}

void fdset_delete(void* hset)
{
    handle_dettach((my_handle *) hset);
}

#endif
