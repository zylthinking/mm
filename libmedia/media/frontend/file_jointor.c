
#include "file_jointor.h"
#include "my_errno.h"
#include "mem.h"
#include "my_handle.h"
#include "my_buffer.h"
#include "lock.h"
#include <string.h>

typedef struct tag_file_jointor {
    lock_t stream_lck, reader_lck;
    my_handle* media_handle;
    media_struct_t media;
    int ref;

    jointor* downstream;
    struct list_head head;
} file_jointor;

typedef struct tag_read_context {
    jointor jointer;
    void* id;
    file_jointor* fj;
    int ref;

    uint32_t bytes, can_release;
    struct list_head head;
    struct list_head entry;
    free_t free_fptr;
} read_context;

static int jointor_get(jointor* jointer)
{
    my_handle* handle = (my_handle *) jointer->any;
    read_context* rctx = (read_context *) handle_get(handle);

    handle_clone(handle);
    __sync_add_and_fetch(&rctx->ref, 1);

    file_jointor* fj = rctx->fj;
    int n = __sync_add_and_fetch(&fj->ref, 1);
    handle_clone(fj->media_handle);

    handle_put(handle);
    return n;
}

static void file_jointor_free(file_jointor* fj)
{
    handle_dettach(fj->media_handle);
    my_assert(list_empty(&fj->head));

    if (fj->downstream) {
        // if reach here, downstream has a bug.
        fj->downstream->ops->jointor_put(fj->downstream);
    }
    my_free(fj);
}

static int jointor_put(jointor* jointer)
{
    my_handle* handle = (my_handle *) jointer->any;
    read_context* rctx = (read_context *) handle_get(handle);
    __sync_sub_and_fetch(&rctx->ref, 1);
    
    file_jointor* fj = rctx->fj;
    handle_release(fj->media_handle);
    int n = __sync_sub_and_fetch(&fj->ref, 1);
    if (n == 0) {
        file_jointor_free(fj);
    }

    handle_put(handle);
    handle_release(handle);
    return n;
}

static int jointor_pull(void* any, struct list_head* headp, const fraction* f)
{
    my_handle* handle = (my_handle *) any;
    read_context* rctx = (read_context *) handle_get(handle);
    if (rctx == NULL) {
        return -1;
    }

    file_jointor* fj = rctx->fj;
    media_struct_t* media = (media_struct_t *) handle_get(fj->media_handle);
    if (media == NULL) {
        int n = 0, can_release = rctx->can_release;
        if (can_release == 1) {
            // only one clone can get the flushed data
            n = (int) __sync_lock_test_and_set(&rctx->bytes, -1);
            if (n > 0)  {
                list_join(&rctx->head, headp);
                // we can use list_del here, but it not the reasonable one.
                list_del_init(&rctx->head);
            }
        }
        handle_put(handle);
        return n;
    }

    if (list_empty(&rctx->entry)) {
        errno = EPERM;
        handle_put(fj->media_handle);
        handle_put(handle);
        return -1;
    }

    int n = media->ops->read(rctx->id, headp, f);
    if (n == -1) {
        int ref = 0;
        lock(&fj->stream_lck);
        // simultaneously jointor_pull from cloned brother may pass the
        // list_empty(&rctx->entry) test above, can't provide a lock to
        // ignore that because the lock will called every time, here will
        // much less. while it also means the media->ops->read may be
        // called again after -1 returned, reading an eof file is often
        // happened, so it is not a big problem.
        if (!list_empty(&rctx->entry)) {
            // use list_del_init instead of list_del to support
            // if (list_empty(&rctx->entry)) branch above
            list_del_init(&rctx->entry);
            ref = 1;
        }
        unlock(&fj->stream_lck);

        if (ref == 1) {
            // Can just put this in the lock above, indeed.
            // it is just my cleanliness
            //
            // two hold handle, 1. the puller 2. the list rctx->entry belongs to
            // releasing here is act as the 2nd
            ref = handle_release(handle);
            my_assert(ref > 0);
            ref = __sync_sub_and_fetch(&rctx->ref, 1);
            my_assert(ref > 0);

            if (rctx->free_fptr) {
                rctx->free_fptr(rctx->id);
                rctx->free_fptr = NULL;
            }

            // when media_closed get called, all rctx's can_release will be assgined 
            // excepted this one beacuse it has unlinked,
            // then if (media == NULL) brach above will cause 0 will always returned
            // we need assgin can_release by hand to avoid this.
            rctx->can_release = 1;
        }

        if (rctx == (read_context *) media->any) {
            handle_clone(fj->media_handle);
            handle_dettach(fj->media_handle);
        }
    }

    handle_put(fj->media_handle);
    handle_put(handle);
    return n;
}

static void notify_streams(file_jointor* fj, jointor* jointor)
{
    struct media_struct* media = (struct media_struct *) handle_get(fj->media_handle);
    if (media == NULL) {
        return;
    }
    lock(&fj->stream_lck);

    struct list_head* ent;
    for (ent = fj->head.next; ent != &fj->head; ent = ent->next) {
        read_context* rctx = list_entry(ent, read_context, entry);
        jointor->ops->jointor_push(jointor->any, link_from_up, &rctx->jointer);
    }

    unlock(&fj->stream_lck);
    handle_put(fj->media_handle);
}

static int jointor_push(void* any, int cmd, void* data)
{
    if (cmd != link_from_down && cmd != unlink_from_down) {
        errno = ENOSYS;
        return -1;
    }

    jointor* jointer = (jointor *) data;
    my_handle* handle = (my_handle *) any;
    read_context* rctx = (read_context *) handle_get(handle);
    file_jointor* fj = rctx->fj;

    if (cmd == link_from_down) {
        if (jointer->ctx.stand != stand_join) {
            errno = EPERM;
            handle_put(handle);
            return -1;
        }

        lock(&fj->reader_lck);
        if (fj->downstream == NULL) {
            fj->downstream = jointer;
        } else {
            jointer = NULL;
            errno = EBUSY;
        }
        unlock(&fj->reader_lck);

        if (jointer != NULL) {
            jointer->ops->jointor_get(jointer);
            notify_streams(fj, jointer);
        }
    } else {
        lock(&fj->reader_lck);
        if (fj->downstream == jointer) {
            fj->downstream = NULL;
        } else {
            jointer = NULL;
            errno = EINVAL;
        }
        unlock(&fj->reader_lck);

        if (jointer != NULL) {
            jointer->ops->jointor_put(jointer);
        }
    }

    handle_put(handle);
    if (jointer == NULL) {
        return -1;
    }
    return 0;
}

static struct jointor_ops file_ops = {
    jointor_get,
    jointor_put,
    jointor_pull,
    jointor_push
};

static void reader_context_free(void* addr)
{
    read_context* rctx = (read_context *) addr;
    // when file_jointor_close called, we fill rctx->head
    // then if the downstream call jointor_put without a
    // jointor_pull, rctx->head has unreleased buffers,
    // free them here
    free_buffer(&rctx->head);

    // free_fptr will be called as soon as possible
    // while, there still chances it will not be called early.
    // e.g. stream pull will not return -1, and before media_close
    // the outside user release the stream. call it here.
    if (rctx->free_fptr != NULL) {
        rctx->free_fptr(rctx->id);
    }
    my_free(addr);
}

static read_context* reader_context_alloc(void* id, file_jointor* fj, free_t free_fptr)
{
    errno = ENOMEM;
    read_context* rctx = (read_context *) my_malloc(sizeof(read_context));
    if (rctx == NULL) {
        return NULL;
    }

    my_handle* handle = handle_attach(rctx, reader_context_free);
    if (handle == NULL) {
        my_free(rctx);
        return NULL;
    }

    rctx->jointer.any = handle;
    rctx->jointer.ctx.stand = stand_file;
    rctx->jointer.ctx.extra = 0;
    rctx->jointer.ctx.identy = (intptr_t) fj;
    rctx->jointer.ops = &file_ops;
    INIT_LIST_HEAD(&rctx->jointer.entry[0]);
    INIT_LIST_HEAD(&rctx->jointer.entry[1]);

    rctx->ref = 1;
    INIT_LIST_HEAD(&rctx->head);
    INIT_LIST_HEAD(&rctx->entry);
    rctx->bytes = 0;
    rctx->can_release = 0;
    rctx->id = id;
    rctx->fj = fj;
    rctx->free_fptr = free_fptr;

    return rctx;
}

static void media_close(void* addr)
{
    media_struct_t* media = (media_struct_t *) addr;
    read_context* rctx = (read_context *) media->any;
    file_jointor* fj = rctx->fj;

    // does not need stream_lck because all other
    // accessing to fj->head have locked media_struct.
    while (!list_empty(&fj->head)) {
        struct list_head* ent = fj->head.next;
        list_del_init(ent);

        read_context* ctx = list_entry(ent, read_context, entry);
        int n = __sync_sub_and_fetch(&ctx->ref, 1);
        // read_context exists in two place
        // 1. by ouside users
        // 2. the internal fj->head
        // we delete from the fj->head, and find still someone hold
        // it, then it must be an outside user.
        // because the user maybe a stream reader, and it maybe have not
        // read all data in the stream, so do a final read to avoid losing
        // data, and set can_release indicator to tell the reader after
        // reading done.
        if (n > 0) {
            ctx->bytes = media->ops->read(ctx->id, &ctx->head, NULL);
            if (ctx->bytes == 0) {
                ctx->bytes = -1;
            }

            if (ctx->free_fptr != NULL) {
                ctx->free_fptr(ctx->id);
                ctx->free_fptr = NULL;
            }
            // in jointor_pull, a data dependent barrier exists
            // between can_release and bytes, wmb does not needed here
            ctx->can_release = 1;
        }

        // we must release after no further access to ctx
        // n > 0 only means some users still avaliable at the time calling myref_put
        // it is a hint of worth doing media->ops->read only.
        my_handle* handle = (my_handle *) ctx->jointer.any;
        handle_release(handle);
    }
    media->ops->close(rctx->id);
}

jointor* file_jointor_open(media_struct_t* media)
{
    errno = EFAILED;
    void* id = media->ops->open(media);
    if (id == NULL) {
        return NULL;
    }
    int need_notify = (media->any == as_stream);

    errno = ENOMEM;
    file_jointor* fj = (file_jointor *) my_malloc(sizeof(file_jointor));
    if (fj == NULL) {
        media->ops->close(id);
        return NULL;
    }
    fj->ref = 0;
    INIT_LIST_HEAD(&fj->head);
    fj->downstream = NULL;
    fj->stream_lck = fj->reader_lck = lock_val;

    read_context* rctx = reader_context_alloc(id, fj, NULL);
    if (rctx == NULL) {
        my_free(fj);
        media->ops->close(id);
        return NULL;
    }
    media->any = rctx;

    my_handle* media_handle = handle_attach(&fj->media, media_close);
    if (media_handle == NULL) {
        handle_release((my_handle *) rctx->jointer.any);
        my_free(fj);
        media->ops->close(id);
        return NULL;
    }
    fj->media = *media;
    fj->media_handle = media_handle;

    // only rctx seen by outside holds the fj's reference
    rctx->jointer.ops->jointor_get(&rctx->jointer);
    if (need_notify) {
        list_add(&rctx->entry, &fj->head);
    } else {
        handle_release((my_handle *) rctx->jointer.any);
        __sync_sub_and_fetch(&rctx->ref, 1);
    }
    return &rctx->jointer;
}

static read_context* find_read_context(file_jointor* fj, void* id)
{
    read_context* rctx = NULL;
    struct list_head* ent;
    for (ent = fj->head.next; ent != &fj->head; ent = ent->next) {
        read_context* ctx = list_entry(ent, read_context, entry);
        if (ctx->id == id) {
            rctx = ctx;
            break;
        }
    }
    return rctx;
}

int file_jointor_notify_stream(jointor* jointer, void* id, free_t free_fptr)
{
    int n = -1;
    my_handle* handle = (my_handle *) jointer->any;
    read_context* rctx = (read_context *) handle_get(handle);

    file_jointor* fj = rctx->fj;
    struct media_struct* media = (struct media_struct *) handle_get(fj->media_handle);
    if (media == NULL) {
        errno = EBADF;
        handle_put(handle);
        return -1;
    }

    if (id == rctx->id) {
        errno = EPERM;
        goto LABEL;
    }

    jointor* downstream = NULL;
    lock(&fj->reader_lck);
    lock(&fj->stream_lck);
    rctx = find_read_context(fj, id);
    if (rctx != NULL) {
        unlock(&fj->stream_lck);
        unlock(&fj->reader_lck);
        errno = EPERM;
        goto LABEL;
    }

    rctx = reader_context_alloc(id, fj, free_fptr);
    if (rctx == NULL) {
        unlock(&fj->stream_lck);
        unlock(&fj->reader_lck);
        goto LABEL;
    }
    list_add(&rctx->entry, &fj->head);
    unlock(&fj->stream_lck);

    if (fj->downstream != NULL) {
        downstream = fj->downstream;
        downstream->ops->jointor_get(downstream);
    }
    unlock(&fj->reader_lck);

    if (downstream != NULL) {
        n = downstream->ops->jointor_push(downstream->any, link_from_up, &rctx->jointer);
        downstream->ops->jointor_put(downstream);
        if (n == -1) {
            lock(&fj->stream_lck);
            list_del(&rctx->entry);
            unlock(&fj->stream_lck);

            // I assign values to rctx in reader_context_alloc,
            // but not increase their refs, letting downstream
            // calling jointor_get do these things. but we reach
            // here because we failed, and obviously jointor_get
            // dose not called at all. meaning, we can't call jointor_put here,
            // which will trying relese refs it does not hold at all.
            // That's why I have to do following instread of a jointor_put.
            my_handle* handle = (my_handle *) rctx->jointer.any;
            // if failed, should not close stream, for the ownership does not transfered.
            rctx->free_fptr = NULL;
            handle_release(handle);
        }
    } else {
        n = 0;
    }

LABEL:
    handle_put(fj->media_handle);
    handle_put(handle);
    return n;
}

void file_jointor_close(jointor* jointer)
{
    my_handle* handle = (my_handle *) jointer->any;
    read_context* rctx = (read_context *) handle_get(handle);

    file_jointor* fj = rctx->fj;
    handle_clone(fj->media_handle);
    handle_dettach(fj->media_handle);

    handle_put(handle);
    jointer->ops->jointor_put(jointer);
}
