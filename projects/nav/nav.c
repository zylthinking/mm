
#include "nav.h"
#include "lock.h"
#include "media_buffer.h"
#include "file_jointor.h"
#include "ramfile.h"
#include "audcap.h"
#include "netjointor.h"
#include "my_errno.h"
#include "mem.h"
#include "fdset.h"
#include "preset.h"
#include "captofile.h"
#include "my_handle.h"
#include "playback.h"
#include "sound_proc.h"
#include "videoctl.h"
#include "preset.h"
#include <pthread.h>
#include <string.h>

typedef struct {
    jointor* file;
    void* playback;
} link_t;

typedef struct {
    void* ramfile;
    link_t feedback;
    void* target;
    void* hookptr;
} media_t;

typedef struct {
    volatile int32_t sync;
    lock_t lck;
    void* set;
    void* clt;
    int32_t code;
    media_t avs[2];
    link_t remote;
} nav_context;

static void* net_main(void* addr)
{
    mark("thread start");
    nav_context* context = (nav_context *) addr;
    fdset_join1(context->set, &context->sync);
    mark("thread exit");
    return NULL;
}

static void feedback_unlink(nav_context* context, media_t* avs)
{
    if (avs->feedback.playback != NULL) {
        playback_close(avs->feedback.playback, avs->feedback.file);
    }
    avs->feedback.file->ops->jointor_put(avs->feedback.file);
    avs->feedback.file = NULL;
    ramfile_close(avs->ramfile);
    avs->ramfile = NULL;
}

static intptr_t feedback_link(nav_context* context, media_t* avs)
{
    my_assert(avs->ramfile == NULL);
    avs->ramfile = ramfile_open();
    if (avs->ramfile == NULL) {
        return -1;
    }

    avs->feedback.file = ramfile_jointor_get(avs->ramfile);
    my_assert(avs->feedback.file != NULL);

    avs->feedback.playback = playback_open(NULL, avs->feedback.file);
    if (avs->feedback.playback == NULL) {
        feedback_unlink(context, avs);
        return -1;
    }
    return 0;
}

static intptr_t video_feedback(void* uptr, intptr_t identy, void* any)
{
    codectx_t* ctx = (codectx_t *) any;
    if (__builtin_expect(ctx->mbuf == NULL, 0)) {
        return 0;
    }

    my_handle* handle = (my_handle *) uptr;
    nav_context* context = (nav_context *) handle_get(handle);
    if (context == NULL) {
        return 0;
    }

    video_format* infmt = to_video_format(ctx->left);
    video_format* outfmt = to_video_format(ctx->right);
    if (infmt->cc->code != codec_pixel || outfmt->cc->code == codec_pixel) {
        handle_put(handle);
        return 0;
    }

    lock(&context->lck);
    if (context->avs[1].target == NULL) {
        unlock(&context->lck);
        handle_put(handle);
        return 0;
    }

    my_assert(context->avs[1].ramfile != NULL);
    struct my_buffer* mbuf2 = ctx->mbuf->mop->clone(ctx->mbuf);
    if (mbuf2 != NULL) {
        ramfile_write(context->avs[1].ramfile, mbuf2);
    }
    unlock(&context->lck);

    handle_put(handle);
    return 0;
}

static void vidcap_stop(nav_context* context)
{
    lock(&context->lck);
    if (context->avs[1].target == NULL) {
        unlock(&context->lck);
        return;
    }
    unlock(&context->lck);

    if (context->avs[1].feedback.file != NULL) {
        hook_control(NULL, 1, context->avs[1].hookptr, 0);
        feedback_unlink(context, &context->avs[1]);
    }

    lock(&context->lck);
    capture_delete_target(context->avs[1].target);
    context->avs[1].target = NULL;
    unlock(&context->lck);
}

static intptr_t nav_upload_video(void* uptr, intptr_t identy, void* any)
{
    my_handle* handle = (my_handle *) uptr;
    struct my_buffer* mbuf = (struct my_buffer *) any;

    nav_context* context = (nav_context *) handle_get(handle);
    if (context == NULL) {
        logmsg("upload discard because nav_close has been called\n");
        mbuf->mop->free(mbuf);
        return 0;
    }

    frame_information_print(mbuf);
    int32_t n = media_client_push_video(context->clt, mbuf);
    if (n == -1) {
        mbuf->mop->free(mbuf);
    }
    handle_put(handle);
    return 0;
}

static int32_t vidcap_start(my_handle* handle, nav_context* ctx, video_param* vp)
{
    uintptr_t feedback = vp->feedback;

    errno = EFAILED;
    lock(&ctx->lck);
    if (ctx->avs[1].target != NULL) {
        unlock(&ctx->lck);
        errno = EEXIST;
        return -1;
    }

    if (feedback) {
        intptr_t ret = feedback_link(ctx, &ctx->avs[1]);
        if (ret == -1) {
            unlock(&ctx->lck);
            return -1;
        }
    }

    handle_clone(handle);
    video_format* fmt = video_format_get(codec_h265, csp_any, (uint16_t) vp->width, (uint16_t) vp->height);
    if (fmt != NULL) {
        video_configuration(fmt, vp->gop, vp->fps, vp->kbps);
        target_param param;
        param.cc = &fmt->cc;
        param.campos = vp->campos;
        callback_t cb;
        cb.uptr = handle;
        cb.uptr_put = (free_t) handle_release;
        cb.notify = nav_upload_video;
        ctx->avs[1].target = capture_add_target(&cb, &param);
    }

    if (ctx->avs[1].target == NULL) {
        if (feedback) {
            feedback_unlink(ctx, &ctx->avs[1]);
        }
        handle_release(handle);
        unlock(&ctx->lck);
        return -1;
    }
    unlock(&ctx->lck);

    if (ctx->avs[1].feedback.file != NULL) {
        handle_clone(handle);
        callback_t hook;
        hook.uptr = handle;
        hook.uptr_put =  (free_t) handle_release;
        hook.notify = video_feedback;
        ctx->avs[1].hookptr = hook_control(&hook, 1, NULL, 0);
    }
    return 0;
}

static intptr_t nav_upload_audio(void* uptr, intptr_t identy, void* any)
{
    my_handle* handle = (my_handle *) uptr;
    struct my_buffer* mbuf = (struct my_buffer *) any;

    nav_context* context = (nav_context *) handle_get(handle);
    if (context == NULL) {
        logmsg("upload discard because nav_close has been called\n");
        mbuf->mop->free(mbuf);
        return 0;
    }

    int32_t n = media_client_push_audio(context->clt, mbuf, frames_per_packet);
    if (n == -1) {
        mbuf->mop->free(mbuf);
    }
    handle_put(handle);
    return 0;
}

static int32_t audcap_start(my_handle* handle, nav_context* context, audio_param* cap)
{
    errno = EFAILED;
    lock(&context->lck);
    if (context->avs[0].target != NULL) {
        unlock(&context->lck);
        errno = EEXIST;
        return -1;
    }

    handle_clone(handle);
    target_param param;
    param.cc = &cap->fmt->cc;
    param.aec = cap->aec;

    callback_t cb;
    cb.uptr = handle;
    cb.uptr_put = (free_t) handle_release;
    cb.notify = nav_upload_audio;
    context->avs[0].target = capture_add_target(&cb, &param);
    unlock(&context->lck);

    if (context->avs[0].target == NULL) {
        handle_release(handle);
        return -1;
    }

    if (cap->effect != type_none) {
        effect_settype((int) cap->effect);
        callback_t hook;
        hook.uptr = NULL;
        hook.uptr_put = NULL;
        hook.notify = soundtouch_proc;
        context->avs[0].hookptr = hook_control(&hook, 0, NULL, 1);
    }
    return 0;
}

static void audcap_stop(nav_context* context)
{
    lock(&context->lck);
    if (context->avs[0].target == NULL) {
        unlock(&context->lck);
        return;
    }
    unlock(&context->lck);

    if (context->avs[0].hookptr != NULL) {
        hook_control(NULL, 0, context->avs[0].hookptr, 0);
    }

    lock(&context->lck);
    capture_delete_target(context->avs[0].target);
    context->avs[0].target = NULL;
    unlock(&context->lck);
}

static void audio_context_free(void* addr)
{
    nav_context* context = (nav_context *) addr;

    if (context->avs[0].target != NULL) {
        audcap_stop(context);
    }

    if (context->avs[1].target != NULL) {
        vidcap_stop(context);
    }

    if (context->remote.playback != NULL) {
        playback_close(context->remote.playback, context->remote.file);
       context->remote.playback = NULL;
    }

    if (context->remote.file != NULL) {
        context->remote.file->ops->jointor_put(context->remote.file);
        context->remote.file = NULL;
    }

    if (context->clt != NULL) {
        media_client_close(context->clt, context->code);
    }

    if (context->set != NULL) {
        fdset_delete(context->set);
    }
    my_free(addr);
}

void* nav_open(nav_open_args* args)
{
    nav_context* context = (nav_context *) my_malloc(sizeof(nav_context));
    if (context == NULL) {
        return NULL;
    }
    memset(context, 0, sizeof(*context));
    context->lck = lock_val;

    my_handle* handle = handle_attach(context, audio_context_free);
    if (handle == NULL) {
        my_free(context);
        return NULL;
    }

    struct tcp_addr* addr = tcp_addr_parsel(args->uid, args->info, args->bytes, args->isp);
    if (addr == NULL) {
        goto LABEL;
    }

    context->set = fdset_new(20);
    if (context->set == NULL) {
        goto LABEL;
    }

    pthread_t tid;
    int32_t n = pthread_create(&tid, NULL, net_main, context);
    if (n != 0) {
        goto LABEL;
    }
    pthread_detach(tid);

    while (context->sync == 0) {
        usleep(10);
    }

    context->clt = media_client_open(context->set, addr, args->ms_wait,
                                     args->fptr, args->dont_play, args->dont_upload, args->login_level);
    if (context->clt == NULL) {
        goto LABEL;
    }
    addr = NULL;

    context->remote.file = jointor_get(context->clt);
    if (context->remote.file == NULL) {
        goto LABEL;
    }

    context->remote.playback = playback_open(NULL, context->remote.file);
    if (context->remote.playback != NULL) {
        return (void *) handle;
    }
LABEL:
    if (addr != NULL) {
        tcp_addr_free(addr);
    }

    handle_dettach(handle);
    return NULL;
}

void nav_close(void* any, uint32_t code)
{
    my_assert(any != NULL);
    my_handle* handle = (my_handle *) any;

    nav_context* context = (nav_context *) handle_get(handle);
    my_assert(context != NULL);

    context->code = code;
    handle_put(handle);
    handle_dettach(handle);
}

int32_t nav_ioctl(void* any, int32_t cmd, void* param)
{
    if (any == NULL) {
        errno = EINVAL;
        return -1;
    }

    int32_t n = 0;
    errno = EFAILED;
    my_handle* handle = (my_handle *) any;
    nav_context* context = (nav_context *) handle_get(handle);
    my_assert(context != NULL);
    uint32_t* intp = (uint32_t *) param;

    switch (cmd) {
        case audio_capture_start:
            n = audcap_start(handle, context, (audio_param *) param);
            break;

        case audio_capture_stop:
            audcap_stop(context);
            break;

        case audio_choose_aec:
            n = audio_aec_ctl((enum aec_t) intp[0], intp[1], intp[2]);
            break;

        case audio_capture_pause:
            media_client_silence(context->clt, 1, *intp);
            break;

        case audio_play_pause:
            media_client_silence(context->clt, 0, *intp);
            break;

        case audio_enable_dtx:
            n = audio_enable_vad(*intp);
            if (n == 0) {
                media_client_enable_dtx(context->clt, !!(*intp));
            }
            break;

        case video_capture_start:
            n = vidcap_start(handle, context, (video_param *) param);
            break;

        case video_capture_stop:
            vidcap_stop(context);
            break;

        case video_use_camera:
            n = use_camera(*(enum camera_position_t *) param);
            break;

        default:
            errno = EINVAL;
            n = -1;
            break;
    }

    handle_put(handle);
    return n;
}
