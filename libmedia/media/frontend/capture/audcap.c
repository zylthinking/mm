
#include "file_jointor.h"
#include "audio.h"
#include "my_errno.h"
#include "audcap.h"
#include "my_buffer.h"
#include "lock.h"
#include "media_buffer.h"
#include "now.h"
#include "codec.h"

static struct {
    lock_t lck;
    fraction nbpms;
    uint32_t align;
    uint32_t flag;
    struct list_head head;
    void* codec;
} capture = {0};

static int push(void* any, audio_format* format, char* buf, unsigned int length)
{
    if (buf == NULL) {
        return 0;
    }
    struct my_buffer* mbuf = (struct my_buffer *) buf;

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    if (capture.nbpms.num == 0) {
        audio_format* fmt = to_audio_format(media->pptr_cc);
        capture.align = (fmt->pcm->sambits / 8) * fmt->pcm->channel;
        capture.nbpms = pcm_bytes_from_ms(fmt->pcm, 1);
    }

    lock(&capture.lck);
    if (capture.flag == 2) {
        list_add_tail(&mbuf->head, &capture.head);
    } else {
        mbuf->mop->free(mbuf);
    }
    unlock(&capture.lck);
    return 0;
}

static void* audio_input_open(void* any)
{
    media_struct_t* media = (media_struct_t *) any;
    intptr_t* intp = (intptr_t *) media->any;
    enum aec_t aec = (enum aec_t) intp[0];
    audio_format* fmt = (audio_format*) intp[1];
    intp[1] = (intptr_t) audio_start_input(push, fmt, NULL, aec);
    if (intp[1] == 0) {
        return NULL;
    }
    media->any = as_stream;
    return &capture;
}

static inline int32_t encode(struct my_buffer* mbuf, struct list_head* headp)
{
    int32_t bytes = 0;
    if (capture.codec != NULL) {
        int32_t n = codec_write(capture.codec, mbuf, headp, NULL);
        if (n == -1) {
            if (mbuf != NULL) {
                mbuf->mop->free(mbuf);
            }
        } else {
            bytes = n;
        }
    } else if (mbuf != NULL) {
        list_add_tail(&mbuf->head, headp);
        bytes = (int32_t) mbuf->length;
    }
    return bytes;
}

static int audio_input_read(void* any, struct list_head* headp, const fraction* f)
{
    if (capture.nbpms.num == 0) {
        return 0;
    }

    int32_t bytes = -1;
    if (f != NULL && (uint32_t) (-1) != (f->num / f->den)) {
        bytes = (capture.nbpms.num  * f->num) / (capture.nbpms.den * f->den);
        bytes = roundup(bytes, capture.align);
    }

    struct list_head head;
    lock(&capture.lck);
    if (bytes == -1) {
        list_add(&head, &capture.head);
        list_del_init(&capture.head);
    } else {
        INIT_LIST_HEAD(&head);
        while (bytes > 0 && !list_empty(&capture.head)) {
            struct list_head* ent = capture.head.next;
            list_del(ent);

            struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
            bytes -= mbuf->length;
            list_add_tail(&mbuf->head, &head);
        }
    }
    unlock(&capture.lck);

    int32_t readed = 0;
    while (!list_empty(&head)) {
        struct list_head* ent = head.next;
        list_del(ent);
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        readed += encode(mbuf, headp);
    }

    if (f == NULL) {
        readed += encode(NULL, headp);
    }
    return (int) readed;
}

static void audio_input_close(void* any)
{
    audio_stop_input();
    // race with push
    lock(&capture.lck);
    free_buffer(&capture.head);
    capture.flag = 1;
    unlock(&capture.lck);

    if (capture.codec != NULL) {
        codec_close(capture.codec);
    }
    capture.flag = 0;
}

jointor* audcap_open(audio_format* fmt, enum aec_t aec, callback_t* hook)
{
    while (1 == (volatile uint32_t) capture.flag) {
        usleep(80 * 1000);
    }
    my_assert(capture.flag == 0);

    capture.codec = NULL;
    capture.lck = lock_val;
    capture.align = 0;
    fraction_zero(&capture.nbpms);
    INIT_LIST_HEAD(&capture.head);

    audio_format* pcm_fmt = audio_raw_format(fmt->pcm);
    static media_operation_t audcap_ops = {
        audio_input_open,
        audio_input_read,
        audio_input_close
    };
    intptr_t intp[2];
    intp[0] = (intptr_t) aec;
    intp[1] = (intptr_t) pcm_fmt;

    media_struct_t media_audio;
    media_audio.ops = &audcap_ops;
    media_audio.any = intp;
    jointor* joint = file_jointor_open(&media_audio);
    if (joint == NULL) {
        return NULL;
    }

    pcm_fmt = (audio_format *) intp[1];
    if (!media_same(&pcm_fmt->cc, &fmt->cc) || hook != NULL) {
        capture.codec = codec_open(&pcm_fmt->cc, &fmt->cc, hook);
        if (capture.codec == NULL) {
            file_jointor_close(joint);
            return NULL;
        }
    }
    capture.flag = 2;
    return joint;
}

void audcap_close(jointor* join)
{
    file_jointor_close(join);
    __sync_bool_compare_and_swap(&capture.flag, 2, 1);
}
