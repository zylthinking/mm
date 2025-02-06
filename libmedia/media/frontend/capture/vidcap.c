
#include "video.h"
#include "lock.h"
#include "vidcap.h"
#include "file_jointor.h"
#include "my_handle.h"
#include "codec.h"

#if defined(__ANDROID__)
#include "android.h"
#endif

static struct vidcap_context {
    my_handle* self;
    lock_t lck;

    union {
        struct {
            video_format* fmt;
            callback_t* hook;
            enum camera_position_t campos;
        } args;

        struct {
            void* codec;
            void* vs;
            struct list_head head[2];
            uint64_t pts[2];
        } ctx;
    } u;
} capctx = {NULL};

static void video_push(void* something, struct my_buffer* mbuf)
{
    my_handle* handle = (my_handle *) something;
    if (mbuf == NULL) {
        logmsg("video push finished\n");
        handle_release(handle);
        return;
    }

    struct vidcap_context* ctx = (struct vidcap_context *) handle_get(handle);
    if (ctx == NULL) {
        mbuf->mop->free(mbuf);
        return;
    }

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    my_assert(media->pts != (uint64_t) -1);

    // need not care about cocurreny accessing to pts
    // the other access will happens when there is something
    // in ctx.head which is empty now.
    if (ctx->u.ctx.pts[0] == (uint64_t) -1) {
        my_assert(ctx->u.ctx.pts[1] == (uint64_t) -1);
        ctx->u.ctx.pts[0] = media->pts;
    } else if (ctx->u.ctx.pts[1] > ctx->u.ctx.pts[0] + 2000) {
        // drop the newest than oldest for that then we need not
        // write pts[0] here to avoid serially access it in lock.
        handle_put(handle);
        mbuf->mop->free(mbuf);
        return;
    }

    ctx->u.ctx.pts[1] = media->pts;
    lock(&ctx->lck);
    list_add_tail(&mbuf->head, &ctx->u.ctx.head[1]);
    unlock(&ctx->lck);
    handle_put(handle);
}

static void* video_input_open(void* any)
{
    errno = EFAILED;
    media_struct_t* media = (media_struct_t *) any;
    struct vidcap_context* ctx = (struct vidcap_context *) media->any;

    callback_t* hook = ctx->u.args.hook;
    video_format* fmt = ctx->u.args.fmt;
    enum camera_position_t campos = ctx->u.args.campos;
#if defined(__APPLE__)
    video_format* raw = video_raw_format(fmt->pixel, csp_nv12ref);
#else
    int64_t* feature = android_feature_address();
    my_assert(feature != NULL);
    video_format* raw = video_raw_format(fmt->pixel, feature[camera_csp]);
#endif

    if (!media_same(&raw->cc, &fmt->cc) || hook != NULL) {
        ctx->u.ctx.codec = codec_open(&raw->cc, &fmt->cc, hook);
        if (ctx->u.ctx.codec == NULL) {
            return NULL;
        }
    } else {
        ctx->u.ctx.codec = NULL;
    }

    INIT_LIST_HEAD(&ctx->u.ctx.head[0]);
    INIT_LIST_HEAD(&ctx->u.ctx.head[1]);
    ctx->u.ctx.pts[0] = ctx->u.ctx.pts[1] = (uint64_t) -1;
    handle_clone(ctx->self);

    ctx->u.ctx.vs = video_create(campos, video_push, ctx->self, raw);
    if (ctx->u.ctx.vs == NULL) {
        handle_release(ctx->self);
        if (ctx->u.ctx.codec != NULL) {
            codec_close(ctx->u.ctx.codec);
            ctx->u.ctx.codec = NULL;
        }
        return NULL;
    }
    media->any = as_stream;
    return ctx;
}

static int video_input_read(void* any, struct list_head* headp, const fraction* f)
{
    int nr = 0;
    struct vidcap_context* ctx = (struct vidcap_context *) any;
    uint64_t pts = (uint64_t) -1;
    if (f != NULL) {
        pts = f->num;
    }

    if (pts == 0) {
        // only muxer will pull with pts 0
        // in which case it's fault to encode the stream
        my_assert(ctx->u.ctx.codec == NULL);
        if (list_empty(&ctx->u.ctx.head[0])) {
            return 0;
        }

        struct my_buffer* mbuf = list_entry(&ctx->u.ctx.head[0].next, struct my_buffer, head);
        struct my_buffer* mbuf2 = mbuf->mop->clone(mbuf);
        if (mbuf2 == NULL) {
            return 0;
        }

        mbuf2->ptr[1] += mbuf2->length;
        mbuf2->length = 0;
        list_add(&mbuf2->head, headp);
        return 1;
    }

LABEL:
    while (!list_empty(&ctx->u.ctx.head[0])) {
        struct list_head* ent = ctx->u.ctx.head[0].next;
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->pts > pts || nr > 1) {
            ctx->u.ctx.pts[0] = media->pts;
            return nr;
        }

        list_del(ent);
        if (ctx->u.ctx.codec != NULL) {
            nr += codec_write(ctx->u.ctx.codec, mbuf, headp, NULL);
        } else {
            ++nr;
            list_add_tail(ent, headp);
        }
    }

    lock(&ctx->lck);
    list_add(&ctx->u.ctx.head[0], &ctx->u.ctx.head[1]);
    list_del_init(&ctx->u.ctx.head[1]);
    if (!list_empty(&ctx->u.ctx.head[0])) {
        unlock(&ctx->lck);
        goto LABEL;
    }

    ctx->u.ctx.pts[0] = ctx->u.ctx.pts[1];
    unlock(&ctx->lck);

    if (f == NULL && ctx->u.ctx.codec != NULL) {
        nr += codec_write(ctx->u.ctx.codec, NULL, headp, NULL);
    }
    return nr;
}

static void video_input_close(void* any)
{
    struct vidcap_context* ctx = (struct vidcap_context *) any;
    free_buffer(&ctx->u.ctx.head[0]);
    free_buffer(&ctx->u.ctx.head[1]);
    video_destroy(ctx->u.ctx.vs);
    ctx->u.ctx.vs = NULL;

    if (ctx->u.ctx.codec != NULL) {
        codec_close(ctx->u.ctx.codec);
        ctx->u.ctx.codec = NULL;
    }

    // to be EAGAIN nice
    my_handle* handle = ctx->self;
    ctx->self = NULL;
    handle_release(handle);
}

static media_operation_t vidcap_ops = {
    video_input_open,
    video_input_read,
    video_input_close
};

jointor* vidcap_open(video_format* fmt, enum camera_position_t campos, callback_t* hook)
{
    errno = EEXIST;
    my_handle* handle = handle_attach(&capctx, NULL);
    if (handle == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    my_handle* h = handle;
    handle = __sync_val_compare_and_swap(&capctx.self, NULL, h);
    if (handle == NULL) {
        my_assert(capctx.self == h);
    } else {
        void* ptr = handle_get(handle);
        if (ptr != NULL) {
            handle_put(handle);
        } else {
            errno = EAGAIN;
        }
        handle_dettach(h);
        return NULL;
    }

    capctx.lck = lock_val;
    capctx.u.args.fmt = fmt;
    capctx.u.args.hook = hook;
    capctx.u.args.campos = campos;
    media_struct_t media_video = {
        &vidcap_ops, &capctx
    };

    jointor* joint = file_jointor_open(&media_video);
    if (joint == NULL) {
        capctx.self = NULL;
        handle_dettach(h);
    }
    return joint;
}

void vidcap_close(jointor* join)
{
    file_jointor_close(join);
}
