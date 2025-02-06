
#include "lock.h"
#include "playback.h"
#include "render_jointor.h"
#include "muxer.h"
#include "mem.h"

#define max_playback 4
static lock_t lck = lock_initial;
static int refs = 0;
static jointor* muxer = NULL;
static jointor* render = NULL;
static struct list_head head = LIST_HEAD_INIT(head);

typedef struct {
    struct list_head entry;
    callback_t codec_hook;
} playback_item_t;

static intptr_t codec_callback(void* uptr, intptr_t identy, void* any)
{
    intptr_t n = -1;
    codectx_t* ctx = (codectx_t *) any;
    lock(&lck);

    struct list_head* ent;
    for (ent = head.next; (ctx->mbuf != NULL) && (ent != &head); ent = ent->next) {
        playback_item_t* pbi = (playback_item_t *) list_entry(ent, playback_item_t, entry);
        codectx_t ctx2 = *ctx;
        n = pbi->codec_hook.notify(pbi->codec_hook.uptr, 0, &ctx2);
        if (n != -1) {
            ctx->mbuf = ctx2.mbuf;
        }
    }

    unlock(&lck);
    return n;
}

void playback_hook_delete(void* mptr)
{
    relock(&lck);
    if (refs > 0) {
        playback_item_t* pbi = (playback_item_t *) mptr;
        list_del(&pbi->entry);
        if (pbi->codec_hook.uptr_put) {
            pbi->codec_hook.uptr_put(pbi->codec_hook.uptr);
        }
        heap_free(pbi);
    }
    unlock(&lck);
}

void* playback_hook_add(callback_t* hook)
{
    playback_item_t* pbi = heap_alloc(pbi);
    if (pbi == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    INIT_LIST_HEAD(&pbi->entry);
    pbi->codec_hook = *hook;

    lock(&lck);
    if (refs > 0) {
        list_add_tail(&pbi->entry, &head);
    } else {
        errno = ENOENT;
        heap_free(pbi);
        pbi = NULL;
    }
    unlock(&lck);
    return pbi;
}

static jointor* joint_reference(audio_format* fmt)
{
    lock(&lck);
    if (muxer != NULL) {
        ++refs;
        unlock(&lck);
        return muxer;
    }

    callback_t callback;
    callback.uptr = NULL;
    callback.notify = codec_callback;
    callback.uptr_put = NULL;

    muxer = muxter_create(max_playback, &callback);
    if (muxer == NULL) {
        unlock(&lck);
        return muxer;
    }

    render = render_create(fmt);
    if (render == NULL) {
        muxer->ops->jointor_put(muxer);
        muxer = NULL;
        unlock(&lck);
        return muxer;
    }

    int32_t n = render_link_upstream(render, muxer);
    if (n == -1) {
        render->ops->jointor_put(render);
        muxer->ops->jointor_put(muxer);
        muxer = NULL;
        unlock(&lck);
        return muxer;
    }

    refs = 1;
    unlock(&lck);
    return muxer;
}

static void clear_hooks()
{
    while (!list_empty(&head)) {
        struct list_head* ent = head.next;
        list_del(ent);
        playback_item_t* pbi = (playback_item_t *) list_entry(ent, playback_item_t, entry);
        if (pbi->codec_hook.uptr_put) {
            pbi->codec_hook.uptr_put(pbi->codec_hook.uptr);
        }
        heap_free(pbi);
    }
}

static void joint_dereference(jointor* joint)
{
    my_assert(joint == muxer);
    relock(&lck);
    --refs;
    if (refs == 0) {
        clear_hooks();
        render_unlink_upstream(render, muxer);
        render->ops->jointor_put(render);
        muxer->ops->jointor_put(muxer);
        render = NULL;
        muxer = NULL;
    }
    unlock(&lck);
}

void* playback_open(audio_format* fmt, jointor* upstream)
{
    errno = ENOMEM;
    jointor* muxer = joint_reference(fmt);
    if (muxer != NULL) {
        int n = muxter_link_upstream(muxer, upstream);
        if (n == -1) {
            joint_dereference(muxer);
            muxer = NULL;
        }
    }
    return muxer;
}

void playback_close(void* mptr, jointor* upstream)
{
    jointor* muxer = (jointor *) mptr;
    muxter_unlink_upstream(muxer, upstream);
    joint_dereference(muxer);
}
