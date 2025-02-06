
#include "myjni.h"
#include "mydef.h"
#include "list_head.h"
#include "mem.h"
#include "mmv.h"
#include "nav.h"
#include "my_errno.h"
#include "lock.h"
#include "netdef.h"
#include "media_buffer.h"
#include <semaphore.h>

struct notify_list {
    lock_t lck;
    volatile int nr;
    struct list_head head;
    sem_t sem;
};

struct notify_message {
    uint64_t stat;
    uint64_t args[4];
};
#define nr_long (sizeof(struct notify_message) / sizeof(uint64_t))

struct notify_node {
    struct list_head entry;
    void* tcp_handle;
    uint32_t nr[2];
    struct notify_message* stat;
};

struct notify_list notifier = {
    .lck = {0},
    .nr = 0,
    .head = LIST_HEAD_INIT(notifier.head)
};

static void message_fill(struct notify_message* message, uint32_t notify, void* any)
{
    message->stat = notify;
    switch (notify) {
        case notify_disconnected:
        {
            break;
        }

        case notify_login:
        case notify_user_count:
        {
            message->args[0] = *(uint64_t *) any;
            break;
        }

        case notify_user_stream:
        {
            usr_stream_t* ustream = (usr_stream_t *) any;
            media_buffer* media = (media_buffer *) ustream->media;
            message->args[0] = ustream->uid;
            message->args[1] = ustream->aud0_vid1;
            message->args[2] = 0;
            if (ustream->media != NULL) {
                message->args[2] = (intptr_t) media->pptr_cc;
            }
            message->args[3] = ustream->cid;
            break;
        }

        case notify_user_stat:
        {
            usr_stat_t* stat = (usr_stat_t *) any;
            message->args[0] = stat->uid;
            message->args[1] = stat->stat;
            message->args[2] = stat->reason;
            break;
        }

        case notify_statistic:
        {
            media_stat_t* stat = (media_stat_t *) any;
            message->args[0] = stat->uid;
            message->args[1] = stat->aud0_vid1;
            message->args[2] = stat->lost;
            message->args[3] = stat->recv;
            break;
        }

        case notify_io_stat:
        {
            iostat_t* stat = (iostat_t *) any;
            message->args[0] = stat->nb0;
            message->args[1] = stat->nb1;
            break;
        }

        default:
        {
            my_assert(0);
            break;
        }
    }
}

static int32_t notify(void* ctx, uint32_t notify, void* any)
{
    struct notify_node* node = (struct notify_node *) ctx;
    lock(&notifier.lck);

    uint32_t idx = node->nr[0] % node->nr[1];
    struct notify_message* message = (struct notify_message *) (node->stat + idx);
    message_fill(message, notify, any);
    ++node->nr[0];
    if (list_empty(&node->entry)) {
        list_add(&node->entry, &notifier.head);
        sem_post(&notifier.sem);
    }
    unlock(&notifier.lck);
    return 0;
}

static jlongArray jni_nav_status(JNIEnv* env, jclass clazz)
{
    jlongArray longs = NULL;

    while (notifier.nr > 0) {
        if (-1 == sem_wait(&notifier.sem)) {
            assert(errno == EINTR);
            continue;
        }

        lock(&notifier.lck);
        struct list_head* ent = notifier.head.next;
        if (ent == &notifier.head) {
            unlock(&notifier.lck);
            continue;
        }

        list_del_init(ent);
        struct notify_node* node = list_entry(ent, struct notify_node, entry);
        uint32_t events = node->nr[0];
        if (node->nr[0] > node->nr[1]) {
            events = node->nr[1];
        } else {
            node->nr[0] = 0;
        }

        longs = (*env)->NewLongArray(env, 1 + events * nr_long);
        if (longs != NULL) {
            int64_t handle = (int64_t) (intptr_t) node;
            (*env)->SetLongArrayRegion(env, longs, 0, 1, (jlong *) (&handle));

            uint32_t idx = node->nr[0] % events;
            if (idx != 0) {
                uint32_t nr = (events - idx) * nr_long;
                (*env)->SetLongArrayRegion(env, longs, 1, nr, (jlong *) (&node->stat[idx]));
                (*env)->SetLongArrayRegion(env, longs, 1 + nr, idx * nr_long, (jlong *) (&node->stat[0]));
            } else {
                jlong* p = (jlong *) (&node->stat[0]);
                (*env)->SetLongArrayRegion(env, longs, 1, events * nr_long, (jlong *) (&node->stat[0]));
                node->nr[0] = 0;
            }
        }
        unlock(&notifier.lck);
        break;
    }
    return longs;
}

static int64_t jni_nav_open(JNIEnv* env, jclass clazz, jlong uid, jbyteArray info,
                            jint events, jint level, jint isp, jint ms_wait, jint dont_play, jint dont_upload)
{
    uint8_t buf[1024];
    jsize bytes = (*env)->GetArrayLength(env, info);
    assert(bytes <= sizeof(buf));
    (*env)->GetByteArrayRegion(env, info, 0, bytes, (jbyte *) buf);

    size_t need = sizeof(struct notify_node);
    if (events <= 0) {
        events = 1;
    }
    need += sizeof(struct notify_message) * events;

    struct notify_node* node = (struct notify_node *) my_malloc(need);
    if (node == NULL) {
        logmsg("failed to alloc notify_node\n");
        return -1;
    }
    node->stat = (struct notify_message *) (node + 1);
    node->nr[0] = 0;
    node->nr[1] = events;
    INIT_LIST_HEAD(&node->entry);

    nav_open_args args;
    args.uid = (uint64_t) uid;
    args.info = buf;
    args.bytes = (uint32_t) bytes;
    args.isp = (uint32_t) isp;
    args.ms_wait = (uint32_t) ms_wait;
    args.dont_play = dont_play;
    args.dont_upload = dont_upload;
    args.login_level = level;
    args.fptr.fptr = notify;
    args.fptr.ctx = (void *) node;
    void* something = nav_open(&args);
    if (something == NULL) {
        logmsg("failed to call nav_open\n");
        my_free(node);
        return -1;
    }

    node->tcp_handle = something;
    __sync_add_and_fetch(&notifier.nr, 1);
    return (int64_t) (intptr_t) node;
}

static void jni_nav_close(JNIEnv* env, jclass clazz, jlong something, jint code)
{
    struct notify_node* node = (struct notify_node *) (intptr_t) something;
    nav_close(node->tcp_handle, code);
    lock(&notifier.lck);
    list_del_init(&node->entry);
    unlock(&notifier.lck);
    my_free(node);

    int32_t nr = __sync_sub_and_fetch(&notifier.nr, 1);
    if (nr == 0) {
        sem_post(&notifier.sem);
    }
}

static audio_format* audio_format_get(intptr_t samples, intptr_t channels)
{
    if (samples == 8000) {
        return (channels == 1) ? &silk_8k16b1 : &silk_8k16b2;
    }

    if (samples == 16000) {
        return (channels == 1) ? &silk_16k16b1 : &silk_16k16b2;
    }
    return NULL;
}

static int32_t jni_nav_ioctl(JNIEnv* env, jclass clazz, jlong something, jint cmd, jintArray args)
{
    uint32_t buf[1024];
    audio_param audio;
    video_param video;
    void* ptr = NULL;

    if (cmd == audio_capture_start) {
        (*env)->GetIntArrayRegion(env, args, 0, 4, (jint *) buf);
        audio.aec = buf[0];
        audio.effect = buf[1];
        audio.fmt = audio_format_get(buf[2], buf[3]);
        ptr = &audio;
    } else if (cmd == audio_capture_pause) {
        (*env)->GetIntArrayRegion(env, args, 0, 1, (jint *) buf);
        ptr = &buf[0];
    } else if (cmd == audio_play_pause) {
        (*env)->GetIntArrayRegion(env, args, 0, 1, (jint *) buf);
        ptr = &buf[0];
    } else if (cmd == audio_choose_aec) {
        (*env)->GetIntArrayRegion(env, args, 0, 3, (jint *) buf);
        ptr = &buf[0];
    } else if (cmd == audio_enable_dtx) {
        (*env)->GetIntArrayRegion(env, args, 0, 1, (jint *) buf);
        ptr = &buf[0];
    } else if (cmd == video_capture_start) {
        (*env)->GetIntArrayRegion(env, args, 0, 7, (jint *) buf);
        video.feedback = buf[0];
        video.width = buf[2];
        video.height = buf[1];
        video.campos = (enum camera_position_t) buf[3];
        video.fps = buf[4];
        video.gop = buf[5];
        video.kbps = buf[6];

        ptr = &video;
    } else if (cmd == video_use_camera) {
        (*env)->GetIntArrayRegion(env, args, 0, 1, (jint *) buf);
        ptr = &buf[0];
    } else if (cmd != audio_capture_stop && cmd != video_capture_stop) {
        return -1;
    }

    struct notify_node* node = (struct notify_node *) (intptr_t) something;
    int32_t n = nav_ioctl(node->tcp_handle, (int32_t) cmd, ptr);
    return n;
}

static int32_t lib_open(JavaVM* vm, JNIEnv* env)
{
    int32_t n = sem_init(&notifier.sem, 0, 0);
    if (n == -1) {
        logmsg("failed call sem_init %d\n", errno);
    }
    return n;
}

static void lib_close(JavaVM* vm, JNIEnv* env)
{
    sem_destroy(&notifier.sem);
}

static int64_t jni_mmv_captofile(JNIEnv* env, jclass clazz, jbyteArray path, jint proc, jint aec)
{
    uint8_t buf[1024];
    jsize bytes = (*env)->GetArrayLength(env, path);
    assert(bytes < sizeof(buf));
    buf[bytes] = 0;
    (*env)->GetByteArrayRegion(env, path, 0, bytes, (jbyte *) buf);
    void* something = mmvoice_capture_to_file(buf, (intptr_t) proc, (enum aec_t) aec);
    if (something == NULL) {
        return -1;
    }
    return (intptr_t) something;
}

static void jni_mmv_capstop(JNIEnv* env, jclass clazz, jlong something)
{
    void* handle = (void *) (intptr_t) something;
    mmvoice_capture_stop(handle);
}

static jint jni_mmv_cap_amplitude(JNIEnv* env, jclass clazz, jlong something)
{
    void* handle = (void *) (intptr_t) something;
    return amplitude(handle);
}

static int64_t jni_mmv_playfile(JNIEnv* env, jclass clazz, jbyteArray path)
{
    uint8_t buf[1024];
    jsize bytes = (*env)->GetArrayLength(env, path);
    assert(bytes < sizeof(buf));
    buf[bytes] = 0;
    (*env)->GetByteArrayRegion(env, path, 0, bytes, (jbyte *) buf);

    playpos* pos = mmvoice_play_file(buf);
    if (pos == NULL) {
        return -1;
    }
    return  (intptr_t) pos;
}

static void jni_mmv_stoplay(JNIEnv* env, jclass clazz, jlong something)
{
    playpos* pos = (void *) (intptr_t) something;
    mmvoice_stop_play(pos);
}

static jlongArray jni_mmv_playpos(JNIEnv* env, jclass clazz, jlong something)
{
    playpos* pos = (void *) (intptr_t) something;
    jlongArray longs = (*env)->NewLongArray(env, 4);
    if (longs == NULL) {
        return NULL;
    }

    int64_t buf[4];
    buf[0] = pos->total;
    buf[1] = pos->consumed;
    buf[2] = pos->ms_total;
    buf[3] = pos->err;
    (*env)->SetLongArrayRegion(env, longs, 0, 4, (jlong *) buf);
    return longs;
}

extern int tcpaddr_compile(char buf[1024], uint32_t sid, char* ip, short port);
static jint jni_nav_compile(JNIEnv* env, jclass clazz, jbyteArray buf, jint sid, jbyteArray ip, jint port)
{
    char bin[1024];
    char ipbuf[16];

    jsize bytes = (*env)->GetArrayLength(env, ip);
    my_assert(bytes < sizeof(ipbuf));
    ipbuf[bytes] = 0;
    (*env)->GetByteArrayRegion(env, ip, 0, bytes, (jbyte *) ipbuf);

    int n = tcpaddr_compile(bin, (uint32_t) sid, ipbuf, (short) port);
    bytes = (*env)->GetArrayLength(env, buf);
    assert(bytes >= n);
    (*env)->SetByteArrayRegion(env, buf, 0, bytes, (jbyte *) bin);
    return n;
}

static JNINativeMethod mmapi_methods[] = {
    {
        .name = "nav_compile",
        .signature = "([BI[BI)I",
        .fnPtr = jni_nav_compile
    },

    {
        .name = "nav_open",
        .signature = "(J[BIIIIII)J",
        .fnPtr = jni_nav_open
    },

    {
        .name = "nav_close",
        .signature = "(JI)V",
        .fnPtr = jni_nav_close
    },

    {
        .name = "nav_ioctl",
        .signature = "(JI[I)I",
        .fnPtr = jni_nav_ioctl
    },

    {
        .name = "nav_status",
        .signature = "()[J",
        .fnPtr = jni_nav_status
    },

    {
        .name = "mmv_captofile",
        .signature = "([BII)J",
        .fnPtr = jni_mmv_captofile
    },

    {
        .name = "mmv_cap_amplitude",
        .signature = "(J)I",
        .fnPtr = jni_mmv_cap_amplitude
    },

    {
        .name = "mmv_capstop",
        .signature = "(J)V",
        .fnPtr = jni_mmv_capstop
    },

    {
        .name = "mmv_playfile",
        .signature = "([B)J",
        .fnPtr = jni_mmv_playfile
    },

    {
        .name = "mmv_playpos",
        .signature = "(J)[J",
        .fnPtr = jni_mmv_playpos
    },

    {
        .name = "mmv_stoplay",
        .signature = "(J)V",
        .fnPtr = jni_mmv_stoplay
    },
};

__attribute__((constructor)) static void mmapi_register()
{
    static struct java_mehods mmapi_entry = methods_entry(libmm, mmapi, lib_open, lib_close, NULL);
    java_method_register(&mmapi_entry);
}
