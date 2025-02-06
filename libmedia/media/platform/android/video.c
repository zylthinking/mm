
#include "my_handle.h"
#include "mbuf.h"
#include "media_buffer.h"
#include "pts.h"
#include "lock.h"
#include "myjni.h"
#include "pts.h"
#include "video.h"
#include "android.h"
#include "thread_clean.h"

typedef struct {
    lock_t lck;
    intptr_t run;
    video_push_t fptr_push;
    void* uptr_push;

    enum camera_position_t campos;
    intptr_t angle;
    intptr_t mirror;
    my_handle* self;

    video_format* fmt;
    intptr_t width, height;
    uint32_t stride[3];

    uint32_t seq;
    media_iden_t id;
    java_code jcode;
} video_capture_t;
extern jclass media_clszz;

static struct my_buffer* java_buffer_clone(struct my_buffer* mbuf)
{
    java_buffer_t* jbuf = (java_buffer_t *) (mbuf->ptr[1] + mbuf->length);
    mbuf = jbuf->mop->clone(mbuf);
    if (mbuf != NULL) {
        __sync_add_and_fetch(&jbuf->refs, 1);
    }
    return mbuf;
}

static void java_buffer_release(struct my_buffer* mbuf)
{
    java_buffer_t* jbuf = (java_buffer_t *) (mbuf->ptr[1] + mbuf->length);
    intptr_t n = __sync_sub_and_fetch(&jbuf->refs, 1);
    if (n == 0) {
        java_code stack_java;
        java_code* code = thread_clean_push(0, (free_t) jni_put, sizeof(java_code));
        if (code == NULL) {
            jcode_get_env(&stack_java);
            code = &stack_java;
        }

        JNIEnv* env = jni_get(code, NULL);
        my_assert(env != NULL);
        (*env)->ReleaseByteArrayElements(env, jbuf->pixel, jbuf->mem, 0);
        (*env)->DeleteGlobalRef(env, jbuf->pixel);
        if (code == &stack_java) {
            jni_put(code);
        }
    }
    mbuf->mop = jbuf->mop;
    mbuf->mop->free(mbuf);
}

static struct mbuf_operations java_buffer_mop = {
    .clone = java_buffer_clone,
    .free = java_buffer_release
};

jint jni_pixel_push(JNIEnv* env, jobject obj, jlong something, jbyteArray pixel)
{
    my_handle* handle = (my_handle *) (intptr_t) something;
    video_capture_t* capt = (video_capture_t *) handle_get(handle);
    if (capt == NULL) {
        return -1;
    }

    if (__builtin_expect(capt->angle == -1, 0)) {
        handle_put(handle);
        return -1;
    }

    if (capt->width == 0 || capt->height == 0) {
        handle_put(handle);
        return 0;
    }

    struct my_buffer* mbuf = NULL;
    uintptr_t bytes = 0;
    jint length = (*env)->GetArrayLength(env, pixel);
    if (reference_format(capt->fmt)) {
        bytes = sizeof(media_buffer) + sizeof(java_buffer_t);
    } else {
        bytes = sizeof(media_buffer) + length;
    }

    mbuf = mbuf_alloc_2(bytes);
    if (mbuf == NULL) {
        handle_put(handle);
        return 0;
    }
    mbuf->length = bytes - sizeof(media_buffer);
    mbuf->ptr[1] = mbuf->ptr[0] + sizeof(media_buffer);

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    memset(&media->vp, 0, sizeof(media->vp));
    media->frametype = 0;
    media->angle = capt->angle | capt->mirror;
    media->fragment[0] = 0;
    media->fragment[1] = 1;
    media->pptr_cc = &(capt->fmt->cc);
    media->iden = capt->id;
    media->seq = capt->seq++;
    fill_pts(NULL, mbuf);

    media->vp[0].stride = capt->stride[0];
    media->vp[0].height = capt->fmt->pixel->size->height;
    if (reference_format(capt->fmt)) {
        mbuf->length -= sizeof(java_buffer_t);

        java_buffer_t* jbuf = (java_buffer_t *) (mbuf->ptr[1] + mbuf->length);
        jbuf->mem = (*env)->GetByteArrayElements(env, pixel, NULL);
        jbuf->refs = 1;
        jbuf->pixel = (jbyteArray) (*env)->NewGlobalRef(env, pixel);
        jbuf->mop = mbuf->mop;
        mbuf->mop = &java_buffer_mop;
        media->vp[0].ptr = (char *) jbuf->mem;
    } else {
        (*env)->GetByteArrayRegion(env, pixel, 0, length, (jbyte *) mbuf->ptr[1]);
        media->vp[0].ptr = (char *) mbuf->ptr[1];
    }

    for (uint32_t i = 1; i < capt->fmt->pixel->panels; ++i) {
        media->vp[i].stride = capt->stride[i];
        media->vp[i].height = panel_height(i, capt->fmt->pixel->size->height, capt->fmt->pixel->csp);
        intptr_t h = panel_height(i - 1, capt->height, capt->fmt->pixel->csp);
        media->vp[i].ptr = media->vp[i - 1].ptr + (media->vp[i - 1].stride * h);
    }

    capt->fptr_push(capt->uptr_push, mbuf);
    handle_put(handle);
    return 0;
}

static intptr_t video_begin_capt(my_handle* handle, video_capture_t* capt)
{
    java_code* jcode = &capt->jcode;
    JNIEnv* env = jni_get(jcode, media_clszz);
    if (env == NULL) {
        return -1;
    }

    video_format* fmt = capt->fmt;
    jlong something = (jlong) (intptr_t) handle;
    jintArray result = (jintArray) (*env)->CallStaticObjectMethod(env, jcode->clazz, jcode->id, (jint) capt->campos,
                                                                  fmt->pixel->size->width, fmt->pixel->size->height, fmt->pixel->csp,
                                                                  fmt->caparam->fps, something);
    if (result == NULL) {
        jni_put(jcode);
        return -1;
    }

    jint nr = (*env)->GetArrayLength(env, result);
    jint* buf = (*env)->GetIntArrayElements(env, result, NULL);

    my_assert(nr > 2);
    intptr_t i = 0;
    capt->stride[0] = capt->stride[1] = capt->stride[2] = 0;
    switch (nr) {
        case 5: capt->stride[i] = buf[i]; i++;
        case 4: capt->stride[i] = buf[i]; i++;
        case 3: capt->stride[i] = buf[i]; i++;
    }
    capt->width = buf[i++];
    capt->height = buf[i];
    logmsg("%d, %d selected\n", (int) capt->width, (int) capt->height);

    (*env)->ReleaseIntArrayElements(env, result, buf, 0);
    jni_put(jcode);
    return 0;
}

static void capt_cleanup(void* addr)
{
    video_capture_t* capt = (video_capture_t *) addr;
    handle_release(capt->self);
    capt->self = NULL;

    capt->width = capt->height = 0;
    if (capt->fptr_push != NULL) {
        capt->fptr_push(capt->uptr_push, NULL);
        capt->fptr_push = NULL;
    }
}

static video_capture_t capt = {
    .lck = lock_initial,
    .jcode = {{"libmedia/media", "video_start",  "(IIIIIJ)[I"}, NULL, NULL, 0},
};

void* video_create(enum camera_position_t campos, video_push_t func, void* ctx, video_format* fmt)
{
    errno = EINVAL;
    if (func == NULL) {
        return NULL;
    }

    errno = EEXIST;
    lock(&capt.lck);
    if (capt.fptr_push != NULL) {
        if (capt.run == 0) {
            errno = EAGAIN;
        }
        unlock(&capt.lck);
        return NULL;
    }

    errno = ENOMEM;
    my_handle* handle = handle_attach(&capt, capt_cleanup);
    if (handle == NULL) {
        unlock(&capt.lck);
        return NULL;
    }

    capt.fptr_push = func;
    capt.uptr_push = ctx;
    capt.fmt = fmt;
    capt.seq = 0;
    capt.id = media_id_self;
    capt.campos = campos;
    int64_t* feature = android_feature_address();
    if (campos == back) {
        capt.angle = (intptr_t) feature[back_angle];
        capt.mirror = 0;
    } else {
        capt.angle = (intptr_t) feature[front_angle];
        capt.mirror = 0x80;
    }
    handle_clone(handle);
    capt.self = handle;

    errno = EFAILED;
    if (-1 == video_begin_capt(handle, &capt)) {
        capt.fptr_push = NULL;
        handle_dettach(handle);
        handle = NULL;
    } else {
        capt.run = 1;
        handle_clone(handle);
    }
    unlock(&capt.lck);
    return handle;
}

void video_destroy(void* ptr)
{
    my_handle* handle = (my_handle *) ptr;
    video_capture_t* capt = (video_capture_t *) handle_get(handle);

    if (capt != NULL) {
        lock(&capt->lck);
        capt->run = 0;
        unlock(&capt->lck);
        handle_put(handle);
    }
    handle_dettach(handle);
}

int use_camera(enum camera_position_t campos)
{
    lock(&capt.lck);
    if (capt.run == 0) {
        unlock(&capt.lck);
        return 0;
    }

    my_handle* handle = capt.self;
    handle_clone(handle);
    video_capture_t* captr = (video_capture_t *) handle_get(handle);
    if (captr == NULL) {
        handle_release(handle);
        unlock(&capt.lck);
        return 0;
    }

    if (captr->campos == campos) {
        handle_put(handle);
        handle_release(handle);
        unlock(&capt.lck);
        return 0;
    }
    enum camera_position_t old = captr->campos;

    captr->angle = -1;
    do {
        handle_clone(handle);
        int n = handle_release(handle);
        if (n == 3) {
            break;
        }
        sched_yield();
    } while (1);

    captr->campos = campos;
    int64_t* feature = android_feature_address();
LABEL:
    if (campos == back) {
        captr->angle = (intptr_t) feature[back_angle];
        capt.mirror = 0;
    } else {
        capt.angle = (intptr_t) feature[front_angle];
        capt.mirror = 0x80;
    }

    int n = video_begin_capt(handle, captr);
    if (n == -1) {
        if (old != captr->campos) {
            captr->campos = old;
            goto LABEL;
        }
        capt.run = 0;
        handle_put(handle);
        handle_dettach(handle);
    }
    handle_put(handle);
    unlock(&capt.lck);

    if (captr->campos == old) {
        return -1;
    }
    return n;
}
