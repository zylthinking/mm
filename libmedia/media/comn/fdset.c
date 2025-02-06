
#ifndef _MSC_VER
#include "list_head.h"
#include "fdset.h"
#include "fdset_in.h"
#include "mem.h"
#include "lock.h"
#include "now.h"
#include "mbuf.h"
#include "utils.h"
#include "fdctxpool.h"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <string.h>

#ifdef __linux__
    #include <sys/epoll.h>
    #include <sys/eventfd.h>
    #include <linux/futex.h>

    #ifdef __ANDROID__
        #ifndef EPOLLRDHUP
        #define EPOLLRDHUP 0x2000
        #endif

        #ifndef EPOLLONESHOT
        #define EPOLLONESHOT (1 << 30)
        #endif
    #else
        #include <sys/timerfd.h>
    #endif
    #define epoll_mask(x) (x | EPOLLONESHOT | EPOLLET)
    #define futex(p1, p2, p3, p4, p5, p6) syscall(__NR_futex, p1, p2, p3, p4, p5, p6)
#else
    #include <sys/event.h>
	#define timer_ident ((uintptr_t) -1)
#endif

#define stat_ready 0
#define stat_busy_proc 1
#define stat_busy_wait 2

typedef struct tag_fdset {
    int fd;
#ifdef __linux__
	int efd, tfd, threads;
	uint32_t* keep;
#endif

    lock_t fdctx_lck, timer_lck;
    struct list_head fdctx_head;
    struct list_head timer_head;

    uint32_t cycle;
    lock_t task_lck;
    struct list_head task_pool;
} fdset;

typedef struct tag_timer_notify {
    uint32_t id;
    uint32_t tms;
    my_handle* fdctx;
    struct list_head fdset_entry;
    struct list_head fdctx_entry;
} timer_notify;

static struct my_buffer* proto_buffer_alloc(void* fdctx, uint32_t bytes)
{
    fd_contex* ctx = (fd_contex *) fdctx;
    struct my_buffer* mbuf = NULL;
    if (ctx->read_mbuf != NULL) {
        my_assert(ctx->read_mbuf->length == bytes);
        mbuf = ctx->read_mbuf;
        ctx->read_mbuf = NULL;
    } else {
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
    }
    return mbuf;
}

static int io_read(fd_contex* fdctx)
{
    int error = 0;
    struct fd_struct* fds = fdctx->fds;
    struct my_buffer* mbuf = NULL;
    struct sockaddr_in in, *inptr = NULL;

    if (fds->bytes > 0) {
LABEL:
        mbuf = proto_buffer_alloc(fdctx, fds->bytes);
        if (mbuf != NULL) {
            if (fds->tcp0_udp1 == 0) {
                if (-1 == do_tcp_read(fds->fd, mbuf)) {
                    error = errno;
                }
            } else {
                inptr = &in;
                if (-1 == do_udp_read(fds->fd, mbuf, inptr)) {
                    error = EAGAIN;
                }
            }
        } else {
            error = ENOMEM;
        }
    }

    int n = 0;
    if (error != EWOULDBLOCK && error != EAGAIN) {
        if (mbuf && mbuf->length == 0) {
            my_assert(error == 0);
            mbuf->length = (uintptr_t) (mbuf->ptr[1] - mbuf->ptr[0]);
            mbuf->ptr[1] = mbuf->ptr[0];
        }

        n = fds->fop->read(fds, mbuf, error, inptr);
        if (n > 0) {
            fds->bytes = n;
            goto LABEL;
        }
    } else {
        fds->bytes = (uint32_t) mbuf->length;
        fdctx->read_mbuf = mbuf;
    }
    return n;
}

static int io_write(fd_contex* fdctx)
{
    int n;
    struct sockaddr_in* in;
    struct my_buffer* mbuf = NULL;

    while (1) {
        if (fdctx->write_mbuf != NULL) {
            mbuf = fdctx->write_mbuf;
            in = fdctx->addr;
            fdctx->write_mbuf = NULL;
            fdctx->addr = NULL;
        } else {
            mbuf = fdctx->fds->fop->write(fdctx->fds, &in, NULL);
        }

        if (mbuf == NULL) {
            n = 0;
            break;
        }

        if (fdctx->fds->tcp0_udp1 == 0) {
            n = do_tcp_write(fdctx->fds->fd, mbuf);
        } else {
            n = do_udp_write(fdctx->fds->fd, mbuf, in);
        }

        if (mbuf->length != 0) {
            fdctx->write_mbuf = mbuf;
            fdctx->addr = in;
            break;
        }
        mbuf->mop->free(mbuf);
    }
#ifdef __linux__
    if (n == -1) {
        n = EPIPE;
        struct my_buffer* mbuf = fdctx->fds->fop->write(fdctx->fds, NULL, &n);
        if (__builtin_expect(mbuf != NULL, 0)) {
            mbuf->mop->free(mbuf);
        }

        if (n != -1) {
            n = 0;
        }
    }
#endif
    return n;
}

#ifdef __linux__
static void io_proc(fd_contex* fdctx, uint32_t mask)
{
    int32_t n = 0;
    fdctx->mask[0] = 0;
    struct fd_struct* fds = fdctx->fds;

    if ((0 == fds->tcp0_udp1) && (mask & EPOLLHUP)) {
        int n = check_connection(fds->fd);
        if (n == -1) {
            mask |= EPOLLERR;
        } else if (0 == (mask & (EPOLLERR | EPOLLRDHUP))) {
            return;
        }
    }

    if (mask & EPOLLERR) {
        fdctx->mask[1] = 0;
        if (-1 == fds->fop->read(fds, NULL, EFAILED, NULL)) {
            goto LABEL;
        }
    }

    if (mask & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        if (-1 != io_read(fdctx)) {
            if (mask & EPOLLRDHUP) {
                fdctx->mask[1] &= ~EPOLLIN;
                if (-1 == fds->fop->read(fds, NULL, EPIPE, NULL)) {
                    goto LABEL;
                }
            }
        } else {
            fdctx->mask[1] &= ~EPOLLIN;
            goto LABEL;
        }
        mask &= ~(EPOLLIN | EPOLLPRI | EPOLLRDHUP);
    }

    if (mask & EPOLLOUT) {
        n = io_write(fdctx);
        if (n == -1) {
            fdctx->mask[1] &= ~EPOLLOUT;
            goto LABEL;
        }
    } else if (mask != 0) {
        logmsg("epoll wake up with unexpected mask 0x%x\n", mask);
    }
    return;
LABEL:
    fdctx->mask[1] = 0;
    handle_clone(fdctx->self);
    handle_dettach(fdctx->self);
}
#else
static void io_proc(fd_contex* fdctx, struct kevent* event)
{
    struct fd_struct* fds = fdctx->fds;
    if (event->filter == EVFILT_READ) {
        fdctx->mask[0] &= ~EPOLLIN;
    } else if (event->filter == EVFILT_WRITE) {
        fdctx->mask[0] &= ~EPOLLOUT;
    } else {
        logmsg("event->filter == %d\n", event->filter);
        return;
    }

    int32_t n = 0;
    if (event->flags & EV_ERROR) {
        n = fds->fop->read(fds, NULL, EFAILED, NULL);
        fdctx->mask[1] &= ~(EPOLLIN | EPOLLOUT);
    } else if (event->filter == EVFILT_READ) {
        if (-1 != io_read(fdctx)) {
            if (event->flags & EV_EOF) {
                n = fds->fop->read(fds, NULL, EPIPE, NULL);
                fdctx->mask[1] &= ~EPOLLIN;
            }
        } else {
            fdctx->mask[1] &= ~EPOLLIN;
            n = -1;
        }
    } else if ((event->flags & EV_EOF) || (-1 == io_write(fdctx))) {
        fdctx->mask[1] &= ~EPOLLOUT;
        n = EPIPE;
        struct my_buffer* mbuf = fds->fop->write(fds, NULL, &n);
        if (__builtin_expect(mbuf != NULL, 0)) {
            mbuf->mop->free(mbuf);
        }
    }

    if (n == -1) {
        fdctx->mask[1] &= ~(EPOLLIN | EPOLLOUT);
        handle_clone(fdctx->self);
        handle_dettach(fdctx->self);
    }
}
#endif

static int timer_start_locked(fdset* fset, uint32_t ms)
{
	int n = -1;
    if (fset->cycle != (uint32_t) -1) {
        return 0;
    }

#ifdef __APPLE__
    struct kevent event;
    EV_SET(&event, timer_ident, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, ms, 0);
    n = kevent(fset->fd, &event, 1, NULL, 0, NULL);
#elif !defined(__ANDROID__)
    struct itimerspec timerspec;
    timerspec.it_value.tv_sec = ms / 1000;
    timerspec.it_value.tv_nsec = (ms - timerspec.it_value.tv_sec * 1000) * 1000000;
    timerspec.it_interval.tv_sec = 0;
    timerspec.it_interval.tv_nsec = 0;

    n = timerfd_settime(fset->tfd, 0, &timerspec, NULL);
    if (n == 0) {
        struct epoll_event event;
        event.events = epoll_mask(EPOLLIN);
        event.data.ptr = NULL;
        n = epoll_ctl(fset->fd, EPOLL_CTL_MOD, fset->tfd, &event);
    }
#endif
    return n;
}

static int sync_status(fd_contex* fdctx)
{
    int stat;

    do {
        stat = __sync_val_compare_and_swap(
                    &fdctx->stat, stat_ready, stat_busy_proc);
#ifdef __linux__
        if (stat == stat_busy_wait) {
            if (fdctx->attaching == 1) {
                usleep(1000 * 10);
                continue;
            }
        }
#endif
        if (stat != stat_busy_proc) {
            break;
        }
        usleep(1000 * 5);
    } while (1);

    return stat;
}

#ifdef __linux__
static inline int kevent_modify(fdset* fset, fd_contex* fdctx, int new_mask, int old_mask)
{
    if (new_mask == 0 || new_mask == old_mask) {
        return 0;
    }

    struct epoll_event event;
    event.data.ptr = fdctx;
    event.events = epoll_mask(new_mask);
    fdctx->mask[0] = new_mask;
    int n = epoll_ctl(fset->fd, EPOLL_CTL_MOD, fdctx->fds->fd, &event);
    if (n != 0) {
        my_assert2(errno != EINTR, "epoll_ctl does not said EINTR");
        __sync_val_compare_and_swap(&fdctx->mask[0], new_mask, old_mask);
    }
}
#else
static inline int kevent_modify(fdset* fset, fd_contex* fdctx, int new_mask, int old_mask)
{
    struct fd_struct* fds = fdctx->fds;
    struct kevent* event = NULL;
    int nr = 0;
    if (old_mask == new_mask) {
        return 0;
    }

    if ((new_mask & EPOLLIN) != (old_mask & EPOLLIN)) {
        if (new_mask & EPOLLIN) {
            EV_SET(&fdctx->event[0], fds->fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, fdctx);
        } else {
            EV_SET(&fdctx->event[0], fds->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        }

        event = &fdctx->event[0];
        ++nr;
    }

    if ((new_mask & EPOLLOUT) != (old_mask & EPOLLOUT)) {
        if (new_mask & EPOLLOUT) {
            EV_SET(&fdctx->event[1], fds->fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, fdctx);
        } else {
            EV_SET(&fdctx->event[1], fds->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        }
        ++nr;
        if (event == NULL) {
            event = &fdctx->event[1];
        }
    }

    int n = 0;
    // we have to assign before kevent
    // race with io_proc exists if kevent sccueeds
    fdctx->mask[0] = new_mask;
    while (event != NULL) {
        n = kevent(fset->fd, event, nr, NULL, 0, NULL);
        if (n == 0) {
            break;
        }

        if (errno != EINTR) {
            __sync_val_compare_and_swap(&fdctx->mask[0], new_mask, old_mask);
            logmsg("fd = %d, kevent failed with errno %d\n", fds->fd, errno);
            if (ENOENT == errno) {
                n = 0;
            } else {
                my_assert(0);
            }
            break;
        }
    }
    return n;
}
#endif

static inline int fdset_add_fd(fdset* fset, fd_contex* fdctx)
{
    struct fd_struct* fds = fdctx->fds;
    int n = 0;

    lock(&fdctx->mask_lck);
    uint32_t mask = fds->mask & fdctx->mask[1];
    if (mask != fdctx->mask[0]) {
        n = kevent_modify(fset, fdctx, mask, fdctx->mask[0]);
    }
    unlock(&fdctx->mask_lck);
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
                my_assert(fdctx->stat == stat_busy_proc);
                fdctx->fds->fop->notify(fdctx->fds, &notify->id);
                fdset_add_fd(fset, fdctx);
                fdctx->stat = stat_ready;
            }

            handle_put(handle);
            handle_release(handle);
        } else {
            handle_release(handle);
            sched_handle_free(&notify->id);
        }
    }
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
            uint32_t ms = notify->tms - tms;
            timer_start_locked(fset, ms);
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

#ifdef __linux__
static void looper(void* any, int fd, int efd, uint32_t cycle)
{
    struct epoll_event events;
    uintptr_t tid = systid();
    my_handle* handle = (my_handle *) any;

    while (1) {
        int nr = epoll_wait(fd, &events, 1, cycle);
        fdset* fset = (fdset *) handle_get(handle);
        if (__builtin_expect(fset == NULL, 0)) {
            break;
        }

        if (__builtin_expect(nr == 1, 1)) {
            fd_contex* fdctx = (fd_contex *) events.data.ptr;
            if (__builtin_expect(fdctx != NULL, 1)) {
                int stat = sync_status(fdctx);

                if (stat == stat_ready) {
                    my_assert(fdctx->stat == stat_busy_proc);
                    // we need clone fdctx->self and cache it to h
                    // because there is a window between fdctx->stat = stat_ready and handle_put(h)
                    // in which fdctx->self may be freed
                    my_handle* h = fdctx->self;
                    handle_clone(h);

                    // sync_status suspend fd_contex_free
                    // while it can't forbide handle_dettach(fdctx->self);
                    fdctx->proc_tid = tid;
                    void* ptr = handle_get(h);
                    fdctx->proc_tid = invalid_tid;
                    if (ptr == NULL) {
                        fdctx->stat = stat_ready;
                        goto LABEL;
                    }

                    io_proc(fdctx, events.events);
                    fdset_add_fd(fset, fdctx);

                    fdctx->stat = stat_ready;
                    handle_put(h);
                LABEL:
                    handle_release(h);
                }
            } else {
#ifdef __ANDROID__
                my_assert(0);
#endif
                timer_expires(fset);
            }
        } else if ((nr != 0) && (nr != -1 || EINTR != errno)) {
            logmsg("what's up? n = %d, errno = %d\n", nr, errno);
        }
#ifdef __ANDROID__
        timer_expires(fset);
#endif
        handle_put(handle);
    }

    int n = handle_release(handle);
    if (n == 0) {
        epoll_ctl(fd, EPOLL_CTL_DEL, efd, NULL);
        my_close(efd);
        my_close(fd);
    }
}
#else
static void looper(void* any, int fd)
{
    struct kevent event[2];
    uintptr_t tid = systid();
    my_handle* handle = (my_handle *) any;

    while (1) {
        int nr = kevent(fd, NULL, 0, event, 1, NULL);
        fdset* fset = (fdset *) handle_get(handle);
        if (__builtin_expect(fset == NULL, 0)) {
            break;
        }

        if (__builtin_expect(nr > 0, 1)) {
            fd_contex* fdctx = (fd_contex *) event[0].udata;
            if (__builtin_expect(event[0].ident != timer_ident, 1)) {
                int stat = sync_status(fdctx);

                if (stat == stat_ready) {
                    my_assert(fdctx->stat == stat_busy_proc);
                    // we need clone fdctx->self and cache it to h
                    // because there is a window between fdctx->stat = stat_ready and handle_put(h)
                    // in which fdctx->self may be freed
                    my_handle* h = fdctx->self;
                    handle_clone(h);

                    // sync_status suspend fd_contex_free
                    // while it can't forbide handle_dettach(fdctx->self);
                    fdctx->proc_tid = tid;
                    void* ptr = handle_get(h);
                    fdctx->proc_tid = invalid_tid;
                    if (ptr == NULL) {
                        fdctx->stat = stat_ready;
                        goto LABEL;
                    }

                    for (int i = 0; i < nr; ++i) {
                        struct kevent* event_ptr = &event[i];
                        io_proc(fdctx, event_ptr);
                    }

                    fdset_add_fd(fset, fdctx);
                    fdctx->stat = stat_ready;
                    handle_put(h);
                LABEL:
                    handle_release(h);
                } else {
                    logmsg("fd = %d kevent wakes but ignored\n", fdctx->fds->fd);
                }
            } else {
                timer_expires(fset);
            }
        } else if (nr != -1 || EINTR != errno) {
            logmsg("what's up? n = %d, errno = %d\n", nr, errno);
        }

        handle_put(handle);
    }

    handle_release(handle);
}
#endif

static void fset_delete(void* addr)
{
    fdset* fset = (fdset *) addr;
#if defined(__linux__) && !defined(__ANDROID__)
    epoll_ctl(fset->fd, EPOLL_CTL_DEL, fset->tfd, NULL);
    my_close(fset->tfd);
#endif

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
#ifdef __linux__
            epoll_ctl(fset->fd, EPOLL_CTL_DEL, fdctx->fds->fd, NULL);
#else
            kevent_modify(fset, fdctx, 0, fdctx->mask[0]);
#endif
        }
        unlock(&fdctx->set_lck);

        if (!empty) {
            handle_dettach(handle);
        }
    }

#ifdef __linux__
    if (fset->threads != 0) {
        uint64_t n = 1;
        write(fset->efd, &n, sizeof(n));
    } else {
        epoll_ctl(fset->fd, EPOLL_CTL_DEL, fset->efd, NULL);
        my_close(fset->efd);
        my_close(fset->fd);
    }
#else
    my_close(fset->fd);
#endif

    release_all_timer(fset, &fset->timer_head);
    free_buffer(&fset->task_pool);

#ifdef __linux__
    if (fset->keep != NULL) {
        fset->keep[2] = 1;
        futex(&fset->keep[2], FUTEX_WAKE, fset->keep[0], NULL, NULL, 0);
    }
#endif
    my_free(fset);
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

    int fd = fset->fd;
#ifdef __linux__
    int efd = fset->efd;
    uint32_t cycle = fset->cycle;
    __sync_add_and_fetch(&fset->threads, 1);
#endif

    handle_clone(hset);
    *flag = 1;

    handle_put(hset);
#ifdef __linux__
    looper(hset, fd, efd, cycle);
#else
    looper(hset, fd);
#endif
    return 0;
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
#ifdef __linux__
        __sync_add_and_fetch(&fset->keep[0], 1);
#endif
    }

    while (fset != NULL) {
        struct my_buffer* mbuf = NULL;
        lock(task_lck);

#ifdef __linux__
        if (fset->keep[2] > 0) {
            my_assert(!list_empty(pool));
            --fset->keep[2];
#else
        if (!list_empty(pool)) {
#endif
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
#ifdef __linux__
            __sync_add_and_fetch(&fset->keep[1], 1);
            futex(&fset->keep[2], FUTEX_WAIT, 0, NULL, NULL, 0);
            __sync_sub_and_fetch(&fset->keep[1], 1);
#else
            usleep(1000);
#endif
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

#ifdef __linux__
    uint32_t* old = __sync_val_compare_and_swap(&fset->keep, NULL, keep);
    if (old != NULL && old != keep) {
        handle_put(hset);
        *flag = 1;
        errno = EINVAL;
        return -1;
    }
#endif

    handle_clone(hset);
    *flag = 1;

    handle_put(hset);
    task_proc(hset);
    return 0;
}

#ifdef __linux__
void* fdset_new(uint32_t ms_cycle)
{
    struct epoll_event event;

    int n, tfd = -1;
#ifndef __ANDROID__
    tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd == -1) {
        return NULL;
    }

    struct itimerspec timerspec = {
        .it_interval = {0, 0},
        .it_value = {0, 0}
    };

    if (ms_cycle != (uint32_t) -1) {
        timerspec.it_value.tv_sec = ms_cycle / 1000;
        timerspec.it_value.tv_nsec = (ms_cycle * 1000000) % 1000000000;
        timerspec.it_interval = timerspec.it_value;
    }

    n = timerfd_settime(tfd, 0, &timerspec, NULL);
    if (n == -1) {
        goto LABEL5;
    }
#endif

    int fd = epoll_create(1);
    if (fd == -1) {
        goto LABEL5;
    }

#ifndef __ANDROID__
    event.events = epoll_mask(EPOLLIN);
    event.data.ptr = NULL;
    n = epoll_ctl(fd, EPOLL_CTL_ADD, tfd, &event);
    if (n == -1) {
        goto LABEL4;
    }
#endif

    int efd = eventfd(0, 0);
    if (efd == -1) {
        goto LABEL3;
    }

    event.data.fd = fd;
    event.events = EPOLLIN;
    n = epoll_ctl(fd, EPOLL_CTL_ADD, efd, &event);
    if (n == -1) {
        goto LABEL2;
    }

    errno = ENOMEM;
    fdset* fset = (fdset *) my_malloc(sizeof(fdset));
    if (fset == NULL) {
        goto LABEL1;
    }

    my_handle* handle = handle_attach(fset, fset_delete);
    if (handle == NULL) {
        goto LABEL0;
    }

    INIT_LIST_HEAD(&fset->fdctx_head);
    INIT_LIST_HEAD(&fset->timer_head);
    INIT_LIST_HEAD(&fset->task_pool);
    fset->fdctx_lck = lock_val;
    fset->timer_lck = lock_val;
    fset->task_lck = lock_val;
    fset->fd = fd;
    fset->efd = efd;
    fset->threads = 0;
    fset->tfd = tfd;
    fset->keep = NULL;
    fset->cycle = ms_cycle;
    return (void *) handle;

LABEL0:
    my_free(fset);
LABEL1:
    epoll_ctl(fd, EPOLL_CTL_DEL, efd, NULL);
LABEL2:
    my_close(efd);
LABEL3:
#ifndef __ANDROID__
    epoll_ctl(fd, EPOLL_CTL_DEL, tfd, NULL);
#endif
LABEL4:
    my_close(fd);
LABEL5:
#ifndef __ANDROID__
    my_close(tfd);
#endif
    return NULL;
}
#else
void* fdset_new(uint32_t ms_cycle)
{
    int fd = kqueue();
    if (fd == -1) {
        return NULL;
    }

    if (ms_cycle != (uint32_t) -1) {
        struct kevent event;
        EV_SET(&event, timer_ident, EVFILT_TIMER, EV_ADD, 0, ms_cycle, 0);
        int n = kevent(fd, &event, 1, NULL, 0, NULL);
        if (n == -1) {
            my_close(fd);
            return NULL;
        }
    }

    errno = ENOMEM;
    fdset* fset = (fdset *) my_malloc(sizeof(fdset));
    if (fset == NULL) {
        my_close(fd);
        return NULL;
    }

    my_handle* handle = handle_attach(fset, fset_delete);
    if (handle == NULL) {
        my_free(fset);
        my_close(fd);
        return NULL;
    }

    INIT_LIST_HEAD(&fset->fdctx_head);
    INIT_LIST_HEAD(&fset->timer_head);
    INIT_LIST_HEAD(&fset->task_pool);
    fset->fdctx_lck = lock_val;
    fset->timer_lck = lock_val;
    fset->task_lck = lock_val;
    fset->fd = fd;
    fset->cycle = ms_cycle;
    return (void *) handle;
}
#endif

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
#ifdef __linux__
    struct fd_struct* fds = fdctx->fds;
    epoll_ctl(fset->fd, EPOLL_CTL_DEL, fds->fd, NULL);
#else
    kevent_modify(fset, fdctx, 0, fdctx->mask[0]);
#endif
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
        // stat == stat_busy_wait means the fdctx didn't exist in fset
        stat = __sync_val_compare_and_swap(&fdctx->stat, stat_ready, stat_busy_wait);
        if (stat != stat_busy_proc || fdctx->proc_tid == systid()) {
            fdctx->stat = stat_busy_wait;
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
    fdctx->addr = NULL;
    fdctx->stat = stat_busy_wait;
    fdctx->proc_tid = invalid_tid;
    fdctx->mask_lck = lock_val;
    fdctx->set_lck = lock_val;
    fdctx->mask[0] = 0;
    fdctx->mask[1] = EPOLLIN | EPOLLOUT;
#ifdef __linux__
    fdctx->attaching = 0;
#endif
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

    if (-1 == make_no_sigpipe(fds->fd) ||
        -1 == make_none_block(fds->fd))
    {
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

static int fdset_add_fdctx(fdset* fset, fd_contex* fdctx)
{
    if (fdctx->stat != stat_busy_wait) {
        errno = EEXIST;
        return -1;
    }

#ifdef __linux__
    struct fd_struct* fds = fdctx->fds;
    uint32_t mask = fds->mask & fdctx->mask[1];
    struct epoll_event event;
    event.data.ptr = fdctx;
    event.events = epoll_mask(mask);

    fdctx->attaching = 1;
    int n = epoll_ctl(fset->fd, EPOLL_CTL_ADD, fds->fd, &event);
    if (-1 == n) {
        fdctx->attaching = 0;
        return -1;
    }
    fdctx->mask[0] = mask;
    fdctx->stat = stat_ready;
    fdctx->attaching = 0;
#else
    int n = fdset_add_fd(fset, fdctx);
	    if (-1 == n) {
        return -1;
    }
    fdctx->stat = stat_ready;
#endif
    return 0;
}

my_handle* fdset_attach_fd(void* set, struct fd_struct* fds)
{
    if (fds->fd == -1) {
        errno = EINVAL;
        return NULL;
    }

    if (-1 == make_no_sigpipe(fds->fd) ||
        -1 == make_none_block(fds->fd))
    {
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

void fdset_delete(void* hset)
{
    handle_dettach((my_handle *) hset);
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

    int n = fdset_add_fd(fset, fdctx);
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

    int n = 0;
    if (ent == &fset->timer_head) {
        n = timer_start_locked(fset, ms);
    }

    if (n == 0) {
        list_add(&notify->fdset_entry, ent);
    }
    unlock(&fset->timer_lck);
    return n;
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
    .clone = proto_buffer_clone,
    .free = proto_buffer_free
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

#ifdef __linux__
    int32_t thrds = 2 * (fset->keep[0] - fset->keep[1]);
    int32_t nr = 0;
#endif

    lock(&fset->task_lck);
#ifdef __linux__
    nr = ++fset->keep[2];
#endif
    list_add_tail(&mbuf->head, &fset->task_pool);
    unlock(&fset->task_lck);

#ifdef __linux__
    if (nr > thrds) {
        nr -= thrds;
        futex(&fset->keep[2], FUTEX_WAKE, nr, NULL, NULL, 0);
    }
#endif
    handle_put(fdctx->hset);
    handle_put(handle);
}

#endif
