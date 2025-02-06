
#include "captofile.h"
#include "audcap.h"
#include "vidcap.h"
#include "my_handle.h"
#include "mem.h"
#include "lock.h"
#include "my_errno.h"
#include <pthread.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

typedef struct {
    callback_t hook;
    struct list_head entry;
    intptr_t sync;
} hook_item;

typedef struct {
    my_handle* self;
    lock_t lck, hook_lck;
    jointor* joint;
    struct list_head head;
    struct list_head hook_list;
    void (*cap_close) (jointor* join);
} capture_wrapper;

static capture_wrapper capture[2] = {
    {
        .lck = lock_initial,
        .hook_lck = lock_initial,
        .head = LIST_HEAD_INIT(capture[0].head),
        .hook_list = LIST_HEAD_INIT(capture[0].hook_list),
        .cap_close = audcap_close
    },

    {
        .lck = lock_initial,
        .hook_lck = lock_initial,
        .head = LIST_HEAD_INIT(capture[1].head),
        .hook_list = LIST_HEAD_INIT(capture[1].hook_list),
        .cap_close = vidcap_close
    }
};

typedef struct {
    my_handle* self;
    my_handle* handle;
    struct list_head entry;
    callback_t callback;
} target_t;

static void write_buffer(struct my_buffer* mbuf, capture_wrapper* wrapper)
{
    struct list_head head;
    INIT_LIST_HEAD(&head);

    lock(&wrapper->lck);
    list_add(&head, &wrapper->head);
    list_del_init(&wrapper->head);
    unlock(&wrapper->lck);

    // need save target->self to handle in the lock
    // if we do not require the lock, target then maybe
    // freed already.
    lock(&wrapper->lck);
    while (!list_empty(&head)) {
        struct list_head* ent = head.next;
        list_del_init(ent);
        target_t* target = list_entry(ent, target_t, entry);
        my_handle* handle = target->self;
        handle_clone(handle);
        unlock(&wrapper->lck);

        target = (target_t *) handle_get(handle);
        if (target == NULL) {
            handle_release(handle);
            lock(&wrapper->lck);
            continue;
        }

        // now no one can grab target from us
        // write target freely
        struct my_buffer* mbuf2 = mbuf->mop->clone(mbuf);
        if (mbuf2 != NULL) {
            intptr_t n = target->callback.notify(target->callback.uptr, 0, mbuf2);
            if (n == -1) {
                mbuf2->mop->free(mbuf2);
            }
        }

        // we don't know wether or not handle_put
        // may release the target. put it back to list
        // before the potential release
        lock(&wrapper->lck);
        list_add_tail(ent, &wrapper->head);
        unlock(&wrapper->lck);

        // we need call handle_put ouside of wrapper->lck
        // because if we call handle_put, we lost the ownership of target
        // then target meybe be freed immidiatly, well, it will unlink itself
        // from targets list, this will require wrapper->lck.
        // for same reason, we can't put list_add_tail(ent, &wrapper->head) 
        // after handle_put, because ent has been freed
        handle_put(handle);
        handle_release(handle);
        lock(&wrapper->lck);
    }
    unlock(&wrapper->lck);
}

static void write_to_targets(struct list_head* headp, capture_wrapper* wrapper)
{
    while (!list_empty(headp)) {
        struct list_head* ent = headp->next;
        list_del(ent);

        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        write_buffer(mbuf, wrapper);
        mbuf->mop->free(mbuf);
    }
}

static void* read_thread(void* arg)
{
    mark("capture read thread start");
    fraction frac;
    frac.num = (uint32_t) -1;
    frac.den = 1;
    struct list_head head;
    INIT_LIST_HEAD(&head);
    my_handle* handle = (my_handle *) arg;

    while (1) {
        capture_wrapper* wrapper = (capture_wrapper *) handle_get(handle);
        if (wrapper == NULL) {
            break;
        }

        int bytes = wrapper->joint->ops->jointor_pull(wrapper->joint->any, &head, &frac);
        if (bytes == -1) {
            handle_put(handle);
            break;
        }

        if (bytes == 0) {
            handle_put(handle);
            usleep(1000 * 40);
        } else {
            write_to_targets(&head, wrapper);
            handle_put(handle);
        }
    }

    handle_dettach(handle);
    return NULL;
}

static intptr_t capture_hook_cb(void* uptr, intptr_t identy, void* any)
{
    intptr_t n = -1;
    struct list_head* ent;
    // we are sure wrapper is valid
    // because this callback called during jointor_pull in read_thread
    // where the wrapper has been locked.
    capture_wrapper* wrapper = (capture_wrapper *) uptr;
    codectx_t* ctx = (codectx_t *) any;

    relock(&wrapper->hook_lck);
    for (ent = wrapper->hook_list.next; (ctx->mbuf != NULL) && (ent != &wrapper->hook_list);) {
        hook_item* item = list_entry(ent, hook_item, entry);
        ent = ent->next;

        codectx_t ctx2 = *ctx;
        n = item->hook.notify(item->hook.uptr, identy, any);
        if (n != -1) {
            ctx->mbuf = ctx2.mbuf;
            n = 0;
        }
    }
    unlock(&wrapper->hook_lck);
    return n;
}

static void free_hook_item(hook_item* item)
{
    if (item->hook.uptr_put != NULL) {
        item->hook.uptr_put(item->hook.uptr);
    }
    my_free(item);
}

static void stop_capture(void* any)
{
    capture_wrapper* wrapper = (capture_wrapper *) any;
    if (wrapper->joint != NULL) {
        wrapper->cap_close(wrapper->joint);
        wrapper->joint->ops->jointor_put(wrapper->joint);
        wrapper->joint = NULL;
    }

    while (!list_empty(&wrapper->head)) {
        struct list_head* ent = wrapper->head.next;
        list_del(ent);

        target_t* target = list_entry(ent, target_t, entry);
        handle_release(target->self);
    }

    while (!list_empty(&wrapper->hook_list)) {
        struct list_head* ent = wrapper->hook_list.next;
        list_del(ent);

        hook_item* item = list_entry(ent, hook_item, entry);
        free_hook_item(item);
    }

    my_handle* handle = wrapper->self;
    relock(&wrapper->lck);
    wrapper->self = NULL;
    unlock(&wrapper->lck);
    handle_release(handle);
}

static intptr_t do_start_capture(capture_wrapper* wrapper, target_param* param)
{
    errno = EFAILED;
    callback_t hook;
    hook.uptr = wrapper;
    hook.notify = capture_hook_cb;
    hook.uptr_put = NULL;

    if (wrapper->cap_close == audcap_close) {
        wrapper->joint = audcap_open(to_audio_format(param->cc), param->aec, &hook);
    } else {
        wrapper->joint = vidcap_open(to_video_format(param->cc), param->campos, &hook);
    }

    if (wrapper->joint == NULL) {
        return -1;
    }
    wrapper->joint->ops->jointor_get(wrapper->joint);

    pthread_t tid;
    my_handle* handle = wrapper->self;
    handle_clone(handle);
    int n = pthread_create(&tid, NULL, read_thread, handle);
    if (n != 0) {
        errno = n;
        handle_release(handle);
        n = -1;
    } else {
        pthread_detach(tid);
    }
    return n;
}

static capture_wrapper* start_capture(target_param* param)
{
    capture_wrapper* wrapper = NULL;
    capture_wrapper* objptr = NULL;
    if (audio_type == media_type(*param->cc)) {
        objptr = &capture[0];
    } else {
        objptr = &capture[1];
    }

LABEL:
    relock(&objptr->lck);
    if (objptr->self == NULL) {
        objptr->self = handle_attach(objptr, stop_capture);
        if (objptr->self == NULL) {
            unlock(&objptr->lck);
            errno = ENOMEM;
            return NULL;
        }

        if (-1 == do_start_capture(objptr, param)) {
            handle_clone(objptr->self);
            handle_dettach(objptr->self);
            unlock(&objptr->lck);
            return NULL;
        }
    }
    my_handle* handle = objptr->self;
    handle_clone(handle);
    unlock(&objptr->lck);

    wrapper = handle_get(handle);
    if (wrapper == NULL) {
        handle_release(handle);
        goto LABEL;
    }
    return wrapper;
}

static void capture_handle_put(my_handle* handle)
{
    int n = handle_clone(handle);
    if (n == 4) {
        handle_dettach(handle);
    } else {
        handle_release(handle);
    }
    handle_release(handle);
}

static void target_free(void* addr)
{
    target_t* target = (target_t *) addr;
    capture_wrapper* wrapper = NULL;

    if (target->handle != NULL) {
        my_handle* handle = target->handle;
        wrapper = (capture_wrapper *) handle_get(handle);
        if (wrapper != NULL) {
            lock(&wrapper->lck);
            list_del_init(&target->entry);
            unlock(&wrapper->lck);
            handle_put(handle);
            handle_release(target->self);
        }

        if (target->callback.uptr_put != NULL) {
            target->callback.uptr_put(target->callback.uptr);
        }
        capture_handle_put(handle);
    }
    my_free(target);
}

void* capture_add_target(callback_t* cb, target_param* param)
{
    errno = ENOMEM;
    target_t* target = (target_t *) my_malloc(sizeof(target_t));
    if (target == NULL) {
        return NULL;
    }

    target->callback = *cb;
    target->handle = NULL;
    target->self = NULL;
    INIT_LIST_HEAD(&target->entry);

    my_handle* handle = handle_attach(target, target_free);
    if (handle == NULL) {
        my_free(target);
        return NULL;
    }

    errno = EFAILED;
    capture_wrapper* wrapper = start_capture(param);
    if (wrapper == NULL) {
        target->callback.uptr_put = NULL;
        handle_dettach(handle);
        return NULL;
    }

    target->handle = wrapper->self;
    handle_clone(handle);
    target->self = handle;
    lock(&wrapper->lck);
    list_add_tail(&target->entry, &wrapper->head);
    unlock(&wrapper->lck);

    handle_put(target->handle);
    return handle;
}

void capture_delete_target(void* any)
{
    handle_dettach((my_handle *) any);
}

static struct list_head* sync_item_cursor(struct list_head* headp)
{
    struct list_head* ent;
    for (ent = headp->next; ent != headp; ent = ent->next) {
        hook_item* hi = list_entry(ent, hook_item, entry);
        if (hi->sync == 0) {
            break;
        }
    }
    return ent;
}

void* hook_control(callback_t* hook, int32_t a0v1, void* ptr, intptr_t sync)
{
    my_handle* handle = NULL;
    capture_wrapper* wrapper = &capture[a0v1];
    lock(&wrapper->lck);
    if (wrapper->self != NULL) {
        handle = wrapper->self;
        handle_clone(handle);
    }
    unlock(&wrapper->lck);

    if (handle == NULL) {
        errno = ENOENT;
        return NULL;
    }

    wrapper = (capture_wrapper *) handle_get(handle);
    if (wrapper == NULL) {
        goto LABEL1;
    }

    hook_item* item = (hook_item *) ptr;
    if (item != NULL) {
        relock(&wrapper->hook_lck);
        list_del(&item->entry);
        unlock(&wrapper->hook_lck);
        free_hook_item(item);
        ptr = NULL;
        goto LABEL0;
    }

    errno = ENOMEM;
    item = (hook_item *) my_malloc(sizeof(hook_item));
    if (item != NULL) {
        item->hook = *hook;
        item->sync = sync;
        struct list_head* ent = &wrapper->hook_list;
        lock(&wrapper->hook_lck);
        if (item->sync) {
            ent = sync_item_cursor(ent);
        }
        list_add_tail(&item->entry, ent);
        unlock(&wrapper->hook_lck);
        ptr = item;
    }

LABEL0:
    handle_put(handle);
LABEL1:
    capture_handle_put(handle);
    return ptr;
}
