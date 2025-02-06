
#include "mmv.h"
#include "captofile.h"
#include "mediafile.h"
#include "sound_proc.h"
#include "media_buffer.h"
#include "file_jointor.h"
#include "mem.h"
#include "now.h"
#include "playback.h"
#include <pthread.h>
#include "my_errno.h"

#ifndef _MSC_VER
#include <unistd.h>
#endif

static intptr_t write_to_file(void* file, intptr_t identy, void* any)
{
    struct my_buffer* mbuf = (struct my_buffer *) any;
    memdia_file_write(file, (media_buffer *) mbuf->ptr[0], mbuf->ptr[1], (uint32_t) mbuf->length);
    mbuf->mop->free(mbuf);
    return 0;
}

struct mmvoice_capture_context {
    void* target;
    void* ptr[2];
    uint32_t tms;
    uint16_t voice;
};

static intptr_t calc_voice(void* uptr, intptr_t identy, void* any)
{
    codectx_t* ctx = (codectx_t *) any;
    audio_format* infmt = to_audio_format(ctx->left);
    if (infmt->cc->code != codec_pcm) {
        return 0;
    }

    struct mmvoice_capture_context* mcc = (struct mmvoice_capture_context *) uptr;
    uint32_t current = now();
    if (current < mcc->tms + 160) {
        return 0;
    }

    mcc->voice = 0;
    int16_t* sp = (int16_t *) ctx->mbuf->ptr[1];
    for (uint32_t i = 0; i < ctx->mbuf->length / 2; ++i) {
        int16_t n = sp[i];
        if (n < 0) {
            n = -n;
        }

        if (n > mcc->voice) {
            mcc->voice = n;
        }
    }
    mcc->tms = current;
    return 0;
}

void* mmvoice_capture_to_file(const char* fullpath, intptr_t effect, enum aec_t aec)
{
    struct mmvoice_capture_context* ctx = (struct mmvoice_capture_context *) my_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    ctx->ptr[0] = ctx->ptr[1] = NULL;
    ctx->tms = 0;
    ctx->voice = 0;

    void* file = media_file_open(fullpath, 1);
    if (file == NULL) {
        my_free(ctx);
        return NULL;
    }

    target_param param;
    param.cc = &silk_8k16b1.cc;
    param.aec = aec;
    callback_t cb;
    cb.uptr = file;
    cb.uptr_put = media_file_close;
    cb.notify = write_to_file;
    ctx->target = capture_add_target(&cb, &param);
    if (ctx->target == NULL) {
        my_free(ctx);
        media_file_close(file);
        return NULL;
    }

    callback_t callback;
    callback.notify = calc_voice;
    callback.uptr = ctx;
    callback.uptr_put = NULL;

    ctx->ptr[0] = hook_control(&callback, 0, NULL, 1);
    if (effect != type_none) {
        callback.uptr = NULL;
        callback.uptr_put = NULL;
        callback.notify = soundtouch_proc;
        ctx->ptr[1] = hook_control(&callback, 0, NULL, 1);
    }
    return ctx;
}

int32_t amplitude(void* any)
{
    int32_t n = -1;
    if (any != NULL) {
        struct mmvoice_capture_context* ctx = (struct mmvoice_capture_context *) any;
        n = (int32_t) (ctx->voice * 100 / 0x7fff);
    } else {
        errno = EINVAL;
    }
    return n;
}

void mmvoice_capture_stop(void* any)
{
    if (any != NULL) {
        struct mmvoice_capture_context* ctx = (struct mmvoice_capture_context *) any;
        hook_control(NULL, 0, ctx->ptr[0], 0);
        capture_delete_target(ctx->target);
        my_free(ctx);
    } else {
        my_assert(0);
    }
}

struct mmvoice_play_context {
    media_struct_t media_file;
    playpos pos;
    jointor* joint[2];
    void* file;
    int need_free;
};

static void* mmvoice_open(void* any)
{
    media_struct_t* media = (media_struct_t *) any;
    struct mmvoice_play_context* context = container_of(media, struct mmvoice_play_context, media_file);
    const char* pathname = (const char *) media->any;

    context->file = media_file_open(pathname, 0);
    if (context->file == NULL) {
        return NULL;
    }

    media->any = as_stream;
    return context;
}

static int mmvoice_read(void* any, struct list_head* headp, const fraction* f)
{
    struct mmvoice_play_context* context = (struct mmvoice_play_context *) any;
    int bytes = memdia_file_read(context->file, headp, f);
    if (bytes >= 0) {
        context->pos.consumed = media_file_tell(context->file);
    } else if (bytes == -1) {
        context->pos.err = 1;
    }
    return bytes;
}

static void atomic_free(struct mmvoice_play_context* context)
{
    int need_free = (int) __sync_lock_test_and_set(&context->need_free, 1);
    if (need_free == 1) {
        my_free(context);
    }
}

static void mmvoice_close(void* any)
{
    struct mmvoice_play_context* context = (struct mmvoice_play_context *) any;
    media_file_close(context->file);
    atomic_free(context);
}

static media_operation_t mmvoice_ops = {
    mmvoice_open,
    mmvoice_read,
    mmvoice_close
};

playpos* mmvoice_play_file(const char* fullpath)
{
    struct mmvoice_play_context* context = (struct mmvoice_play_context *) my_malloc(sizeof(*context));
    if (context == NULL) {
        return NULL;
    }

    context->media_file.ops = &mmvoice_ops;
    context->media_file.any = (void *) fullpath;
    context->pos.consumed = 0;
    context->pos.err = 0;
    context->need_free = 0;

    context->joint[0] = file_jointor_open(&context->media_file);
    if (context->joint[0] == NULL) {
        my_free(context);
        return NULL;
    }

    struct file_header* header = media_file_header(context->file);
    context->pos.ms_total = header->be_file_ms;
    if (header->be_bytes == 0) {
        goto LABEL;
    }

    context->joint[1] = playback_open(NULL, context->joint[0]);
    if (context->joint[1] == NULL) {
        goto LABEL;
    }

    context->pos.total = header->be_bytes;
    return &context->pos;
LABEL:
    context->joint[0]->ops->jointor_put(context->joint[0]);
    context->joint[0] = NULL;
    atomic_free(context);
    return NULL;
}

void mmvoice_stop_play(playpos* pos)
{
    if (pos != NULL) {
        struct mmvoice_play_context* context = container_of(pos, struct mmvoice_play_context, pos);
        playback_close(context->joint[1], context->joint[0]);
        context->joint[1] = NULL;
        file_jointor_close(context->joint[0]);
        atomic_free(context);
    } else {
        my_assert(0);
    }
}
