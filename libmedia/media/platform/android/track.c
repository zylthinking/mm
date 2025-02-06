
#include "delay.h"
#include "myjni.h"
#include "audio.h"
#include "lock.h"
#include "mbuf.h"
#include "my_errno.h"
#include "my_handle.h"
#include "media_buffer.h"
#include "sigproc.h"
#include "stdint.h"
#include "now.h"
#include "pts.h"
#include "smooth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    lock_t lck;
    java_code jcode[2];

    my_handle* out;
    my_handle* in;
    audio_format* fmt[2];
    delay_estimator est;

    void* sigproc;
    uint32_t more_nb;
} audio_dev_t;

static audio_dev_t dobj = {
    .lck = lock_initial,
    .out = NULL,
    .in = NULL,
    .sigproc = NULL,
    .more_nb = 320,
    .jcode = {
        {{"libmedia/media", "track_start",  "(IIIJ)I"}, NULL, NULL, 0},
        {{"libmedia/media", "record_start", "(IIIJ)[I"}, NULL, NULL, 0},
    }
};
extern jclass media_clszz;

typedef struct {
    audio_dev_t* hal;
    fetch_pcm fetch_fptr;
    void* fetch_ctx;
} render;

typedef struct {
    uint32_t seq;
    media_iden_t pcmid;

    audio_dev_t* hal;
    fetch_pcm fetch_fptr;
    void* fetch_ctx;
    struct my_buffer* stock_mbuf;
} capture;

jint jni_pcm_pull(JNIEnv* env, jobject obj, jlong something, jbyteArray pcmbuf, jint padding, int32_t delay)
{
    my_handle* handle = (my_handle *) (intptr_t) something;
    render* writer = (render *) handle_get(handle);
    if (writer == NULL) {
        return -1;
    }

    if (padding < 640) {
        handle_put(handle);
        handle_release(handle);
        return -1;
    }

    jint length = (*env)->GetArrayLength(env, pcmbuf) - padding;
    jbyte* buf = (*env)->GetByteArrayElements(env, pcmbuf, NULL);
    int bytes = writer->fetch_fptr(writer->fetch_ctx, writer->hal->fmt[0], (char *) buf, length);
    pcm_format* pcm = writer->hal->fmt[0]->pcm;
    smooth_fill(pcm, buf, bytes, length);

    void* sigproc = writer->hal->sigproc;
    if (sigproc != NULL && bytes > 0) {
        if (delay == -1) {
            delay = calc_delay(&writer->hal->est, length, writer->hal->fmt[0]);
        }
        sigproc_pcm_notify(sigproc, (char *) buf, bytes, delay);
    }

    (*env)->ReleaseByteArrayElements(env, pcmbuf, buf, 0);
    handle_put(handle);
    return bytes;
}

static int device_begin_rend(render* writer, my_handle* handle, audio_format* fmt)
{
    java_code* jcode = &writer->hal->jcode[0];
    JNIEnv* env = jni_get(jcode, media_clszz);
    if (env == NULL) {
        return -1;
    }

    pcm_format* pcm = fmt->pcm;
    jlong something = (jlong) (intptr_t) handle;
    jint n = (*env)->CallStaticIntMethod(env, jcode->clazz,
                                   jcode->id, pcm->samrate,
                                   pcm->channel, pcm->sambits, something);
    jni_put(jcode);
    return n;
}

static void render_free(void * addr)
{
    render* writer = (render *) addr;
    writer->fetch_fptr(writer->fetch_ctx, NULL, NULL, 0);
}

int audio_start_output(fetch_pcm fptr, audio_format* fmt, void* ctx)
{
    lock(&dobj.lck);
    if (dobj.out != NULL) {
        unlock(&dobj.lck);
        return 0;
    }
    dobj.fmt[0] = fmt;

    errno = ENOMEM;
    render* writer = (render *) my_malloc(sizeof(render));
    if (writer == NULL) {
        unlock(&dobj.lck);
        return -1;
    }

    my_handle* handle = handle_attach(writer, render_free);
    if (handle == NULL) {
        my_free(writer);
        unlock(&dobj.lck);
        return -1;
    }

    writer->hal = &dobj;
    writer->fetch_fptr = fptr;
    writer->fetch_ctx = ctx;

    errno = EFAILED;
    int n = device_begin_rend(writer, handle, dobj.fmt[0]);
    if (n == -1) {
        handle_dettach(handle);
    } else {
        handle_clone(handle);
        dobj.out = handle;
    }
    unlock(&dobj.lck);

    return n;
}

void audio_stop_output()
{
    lock(&dobj.lck);
    if (dobj.out != NULL) {
        handle_dettach(dobj.out);
        dobj.out = NULL;
    }
    unlock(&dobj.lck);
}

static void capture_free(void* addr)
{
    capture* reader = (capture *) addr;
    reader->fetch_fptr(reader->fetch_ctx, NULL, NULL, 0);

    if (dobj.sigproc != NULL) {
        sigproc_put(dobj.sigproc);
        dobj.sigproc = NULL;
    }
}

static void initialize_aec(void* sigproc, uint32_t aec_method, uint32_t webrtc, uint32_t level)
{
    delay_estimator_init(&dobj.est);

    uint32_t val = 1;
    sigproc_control(sigproc, webrtc_control_aec, &aec_method);
    if (webrtc == 1) {
        sigproc_control(sigproc, webrtc_control_hpass, &val);
        sigproc_control(sigproc, webrtc_control_agc, &val);

        val = 3;
        sigproc_control(sigproc, webrtc_control_ns, &val);
    }

    val = level;
    sigproc_control(sigproc, webrtc_control_vad, &val);
    sigproc_control(sigproc, webrtc_control_reserve, &dobj.more_nb);
}

static audio_format* audio_format_get(intptr_t samples, intptr_t channels)
{
    switch (samples) {
        case 8000:  return (channels == 1) ? &pcm_8k16b1 : &pcm_8k16b2;
        case 16000: return (channels == 1) ? &pcm_16k16b1 : &pcm_16k16b2;
        case 24000: return (channels == 1) ? &pcm_24k16b1 : &pcm_24k16b2;
        case 32000: return (channels == 1) ? &pcm_32k16b1 : &pcm_32k16b2;
        case 44100: return (channels == 1) ? &pcm_44k16b1 : &pcm_44k16b2;
        case 48000: return (channels == 1) ? &pcm_48k16b1 : &pcm_48k16b2;
    }
    return NULL;
}

static audio_format* device_begin_capt(capture* reader, my_handle* handle, audio_format* fmt)
{
    java_code* jcode = &reader->hal->jcode[1];
    JNIEnv* env = jni_get(jcode, media_clszz);
    if (env == NULL) {
        return NULL;
    }

    pcm_format* pcm = fmt->pcm;
    jlong something = (jlong) (intptr_t) handle;
    jintArray result = (jintArray) (*env)->CallStaticObjectMethod(env, jcode->clazz,
                            jcode->id, pcm->samrate, pcm->channel, pcm->sambits, something);
    if (result == NULL) {
        jni_put(jcode);
        return NULL;
    }

    jint nr = (*env)->GetArrayLength(env, result);
    jint* buf = (*env)->GetIntArrayElements(env, result, NULL);
    my_assert(nr == 2);

    if (buf[0] != pcm->samrate || buf[1] != pcm->channel) {
        fmt = audio_format_get(buf[0], buf[1]);
    }
    (*env)->ReleaseIntArrayElements(env, result, buf, 0);
    jni_put(jcode);
    return fmt;
}

jint jni_pcm_push(JNIEnv* env, jobject obj, jlong something, jbyteArray pcmbuf, int32_t delay)
{
    my_handle* handle = (my_handle *) (intptr_t) something;
    capture* reader = (capture *) handle_get(handle);
    if (reader == NULL) {
        return -1;
    }

    audio_format* fmt = dobj.fmt[1];
    if (fmt == NULL) {
        handle_put(handle);
        return 0;
    }

    jint length = (*env)->GetArrayLength(env, pcmbuf);
    struct my_buffer* mbuf = mbuf_alloc_2(length + sizeof(media_buffer) + dobj.more_nb);
    if (mbuf == NULL) {
        mbuf = reader->stock_mbuf;
    } else {
        mbuf->length = length;
        mbuf->ptr[1] = (mbuf->ptr[0] + sizeof(media_buffer)) + dobj.more_nb;

        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        memset(media->vp, 0, sizeof(media->vp));
        media->frametype = 0;
        media->angle = 0;
        media->fragment[0] = 0;
        media->fragment[1] = 1;
        media->pptr_cc = &(fmt->cc);
        media->iden = reader->pcmid;
        media->seq = reader->seq;
        fill_pts(mbuf, NULL);
    }
    (*env)->GetByteArrayRegion(env, pcmbuf, 0, length, (jbyte *) mbuf->ptr[1]);

    fraction f = reader->hal->fmt[1]->ops->ms_from(reader->hal->fmt[1], mbuf->length, 0);
    uint32_t duration = f.num / f.den;
    void* sigproc = reader->hal->sigproc;
    if (sigproc != NULL && reader->hal->est.stable) {
        if (delay == -1) {
            delay = duration;
        }
        sigproc_pcm_proc(sigproc, mbuf, delay);
    }

    reader->seq += 1;
    if (mbuf != reader->stock_mbuf) {
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        media->vp[0].ptr = mbuf->ptr[1];
        media->vp[0].stride = (uint32_t) mbuf->length;
        reader->fetch_fptr(reader->fetch_ctx, fmt, (char *) mbuf, 0);
    }
    handle_put(handle);
    return 0;
}

static int32_t webrtc_aec_type(enum aec_t aec)
{
    if (aec == webrtc_none) {
        return sigproc_webrtc_none;
    }

    if (aec == webrtc_mb) {
        return sigproc_webrtc_mb;
    }

    return sigproc_webrtc_pc;
}

audio_format* audio_start_input(fetch_pcm fptr, audio_format* fmt, void* ctx, enum aec_t aec)
{
    lock(&dobj.lck);
    my_assert(dobj.in == NULL);
    dobj.fmt[1] = NULL;

    errno = ENOMEM;
    capture* reader = (capture *) my_malloc(4096);
    if (reader == NULL) {
        unlock(&dobj.lck);
        return NULL;
    }

    my_handle* handle = handle_attach(reader, capture_free);
    if (handle == NULL) {
        my_free(reader);
        unlock(&dobj.lck);
        return NULL;
    }

    reader->stock_mbuf = mbuf_alloc_6(reader + 1, 4096 - sizeof(capture));
    reader->stock_mbuf->ptr[1] = reader->stock_mbuf->ptr[0] + sizeof(media_buffer);
    reader->stock_mbuf->length -= sizeof(media_buffer);
    reader->seq = 0;
    reader->pcmid = media_id_self;
    reader->hal = &dobj;
    reader->fetch_fptr = fptr;
    reader->fetch_ctx = ctx;

    int n = 0;
    errno = EFAILED;
    fmt = device_begin_capt(reader, handle, fmt);
    if (fmt == NULL) {
        n = -1;
        handle_dettach(handle);
    } else {
        handle_clone(handle);
        dobj.in = handle;
        dobj.fmt[1] = fmt;

        dobj.sigproc = sigproc_get(sigproc_webrtc, fmt);
        initialize_aec(dobj.sigproc, webrtc_aec_type(aec),
                       aec == webrtc_none || aec == webrtc_mb || aec == webrtc_pc,
                       0);
    }
    unlock(&dobj.lck);
    return fmt;
}

int audio_aec_ctl(enum aec_t aec, uint32_t vol1, uint32_t vol0)
{
    int volume[2] = {-1, -1};
    if (vol0 != (uint32_t) -1) {
        volume[0] = (vol0 & 0x7fffffff);
    }

    if (vol1 != (uint32_t) -1) {
        volume[1] = (vol1 & 0xffff);
    }

    lock(&dobj.lck);
    if (dobj.in == NULL) {
        unlock(&dobj.lck);
        errno = ENOENT;
        return -1;
    }

    void* sigproc = dobj.sigproc;
    my_assert(sigproc != NULL);

    int n = 0;
    if (aec == webrtc_mb || aec == webrtc_pc || aec == webrtc_none) {
        uint32_t type = webrtc_aec_type(aec);
        sigproc_control(sigproc, webrtc_control_aec, &type);
        sigproc_control(sigproc, webrtc_pcm_volume, volume);
    } else if (aec == none) {
        uint32_t type = webrtc_aec_type(webrtc_none);
        sigproc_control(sigproc, webrtc_control_aec, &type);
    } else {
        errno = EINVAL;
        n = -1;
    }

    unlock(&dobj.lck);
    return n;
}

int audio_enable_vad(uint32_t level)
{
    lock(&dobj.lck);
    if (dobj.in == NULL) {
        unlock(&dobj.lck);
        errno = ENOENT;
        return -1;
    }

    void* sigproc = dobj.sigproc;
    my_assert(sigproc != NULL);
    sigproc_control(sigproc, webrtc_control_vad, &level);

    unlock(&dobj.lck);
    return 0;
}

void audio_stop_input()
{
    lock(&dobj.lck);
    if (dobj.in != NULL) {
        handle_dettach(dobj.in);
        dobj.in = NULL;
    }
    unlock(&dobj.lck);
}
