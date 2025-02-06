
#include "mfio_mp4.h"
#include "mem.h"
#include "my_handle.h"
#include "lock.h"
#include "mbuf.h"
#include "codec.h"
#include "libavformat/avformat.h"
#include <pthread.h>

#define safe_ms 600
typedef struct av_stream_t av_stream_t;

typedef struct {
    lock_t lck;
    intptr_t eof;
    uint32_t tms;
    uint64_t pts;
    uint64_t seq;
    AVFormatContext* ffctx;

    struct list_head head;
    av_stream_t* grant;
    av_stream_t* elect;
    AVStream* ffas;
    AVStream* ffvs;
    void* aac;
    void* h264;
    struct my_buffer* pcmbuf;
    uint32_t ms;
    intptr_t aac_header;
    intptr_t rencode_video;
    pixel_translate_t callback;
} mp4_mfio_t;

typedef struct {
    uint64_t dts;
    struct list_head entry;
} dts_t;

typedef struct {
    intptr_t reflost;
    struct list_head fragments;
    char* frame;
    intptr_t bytes;
    intptr_t frames;

    dts_t dts[10];
    struct list_head head;
} video_t;

typedef struct {
    intptr_t safe_bytes;
    intptr_t bytes;
    intptr_t ms;
    intptr_t mask;
} audio_t;

struct av_stream_t {
    lock_t lck;
    int64_t offset;
    intptr_t eos;
    my_handle* self;
    mp4_mfio_t* mfio;

    media_iden_t iden;
    fourcc** pfcc;

    union {
        video_t video;
        audio_t audio;
    };
    struct list_head head;
    struct list_head entry;
};

static uint64_t audio_pts_mixto(av_stream_t* avs)
{
    if (avs->audio.bytes <= avs->audio.safe_bytes) {
        return 0;
    }

    struct list_head* ent;
    media_buffer* media = NULL;
    intptr_t bytes = avs->audio.bytes - avs->audio.safe_bytes;
    // this rmb protect the loop below does not touch the mbuf
    // being or has added after observing the avs->audio.bytes above.
    // we can't hold avs->lck here to avoid conflict.
    // (or else dead lock may occure, see stream_append_pcm)
    rmb();

    for (ent = avs->head.next; ent != &avs->head; ent = ent->next) {
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media = (media_buffer *) mbuf->ptr[0];
        if (media->vp[0].stride > bytes) {
            break;
        }
        bytes -= media->vp[0].stride;
    }

    uint64_t pts = media->pts;
    audio_format* fmt = to_audio_format(media->pptr_cc);
    fraction frac = pcm_ms_from_bytes(fmt->pcm, (uint32_t) bytes);
    pts += frac.num / frac.den;
    return pts;
}

static uint64_t video_pts_max(mp4_mfio_t* mfio)
{
    uint64_t pts = 0;
    struct list_head* ent;
    for (ent = mfio->head.next; ent != &mfio->head; ent = ent->next) {
        av_stream_t* avs = list_entry(ent, av_stream_t, entry);
        if (avs->pfcc == NULL || audio_type == media_type_raw(avs->pfcc)) {
            continue;
        }

        if (list_empty(&avs->head)) {
            continue;
        }

        struct my_buffer* mbuf = list_entry(avs->head.prev, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->pts > pts) {
            pts = media->pts;
        }
    }

    if (pts > safe_ms) {
        pts -= safe_ms;
    } else {
        pts = 0;
    }
    return pts;
}

static intptr_t list_unlink_fragments(struct list_head* headp, struct list_head* listp)
{
    intptr_t n = 0, bytes = 0;
    while (!list_empty(headp)) {
        struct list_head* ent = headp->next;
        list_del(ent);
        list_add_tail(ent, listp);

        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        bytes += media->vp[0].stride;
        n += 1;
        if (media->fragment[0] == media->fragment[1] - 1) {
            my_assert(n == media->fragment[1]);
            break;
        }
    }
    return bytes;
}

static char* frame_buffer(av_stream_t* avs, intptr_t bytes)
{
    if (avs->video.bytes >= bytes) {
        return avs->video.frame;
    }

    char* buffer = my_malloc(bytes);
    if (buffer == NULL) {
        return NULL;
    }

    if (avs->video.frame != NULL) {
        my_free(avs->video.frame);
    }
    avs->video.frame = buffer;
    avs->video.bytes = bytes;
    return buffer;
}

static uint64_t dts_get(av_stream_t* avs, uint64_t pts)
{
    struct list_head* ent = avs->video.head.next;
    list_del(ent);
    dts_t* val = list_entry(ent, dts_t, entry);
    uint64_t n = val->dts;
    val->dts = pts;

    for (ent = avs->video.head.prev; ent != &avs->video.head; ent = ent->prev) {
        dts_t* dts = list_entry(ent, dts_t, entry);
        if (dts->dts <= val->dts) {
            break;
        }
    }
    list_add(&val->entry, ent);
    return n;
}

static void write_fragments(mp4_mfio_t* mfio, av_stream_t* avs, struct list_head* headp, intptr_t bytes)
{
    struct list_head* ent = headp->next;
    list_del(ent);
    struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    uint64_t pts = media->pts;
    char* buffer = media->vp[0].ptr;
    intptr_t nb = media->vp[0].stride;

    if (media->vp[0].stride < bytes) {
        my_assert(media->fragment[1] > 1);
        buffer = frame_buffer(avs, bytes);
        if (buffer == NULL) {
            logmsg("fails to write frame %d of %d bytes because of oom\n",
                   (int) media->frametype, (int) bytes);
            return;
        }
        memcpy(buffer, media->vp[0].ptr, nb);
        mbuf->mop->free(mbuf);
    }

    while (!list_empty(headp)) {
        ent = headp->next;
        list_del(ent);
        mbuf = list_entry(ent, struct my_buffer, head);
        media = (media_buffer *) mbuf->ptr[0];
        memcpy(&buffer[nb], media->vp[0].ptr, media->vp[0].stride);
        nb += media->vp[0].stride;
        mbuf->mop->free(mbuf);
    }
    my_assert(nb == bytes);

    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.stream_index = mfio->ffvs->index;
    pkt.pts = av_rescale_q(pts, mfio->ffvs->codec->time_base, mfio->ffvs->time_base);
    pkt.dts = av_rescale_q(dts_get(avs, pts), mfio->ffvs->codec->time_base, mfio->ffvs->time_base);
    pkt.flags = 0;
    if (media->frametype == video_type_idr) {
        pkt.flags = AV_PKT_FLAG_KEY;
    }
    pkt.data = (uint8_t *) buffer;
    pkt.size = (int) nb;
    //av_write_frame
    av_interleaved_write_frame(mfio->ffctx, &pkt);

    if (buffer != avs->video.frame) {
        mbuf->mop->free(mbuf);
    }
}

static void write_video_range(mp4_mfio_t* mfio, av_stream_t* avs, uint64_t to)
{
    struct list_head head;
    INIT_LIST_HEAD(&head);

    lock(&avs->lck);
    while (!list_empty(&avs->head)) {
        struct list_head* ent = avs->head.next;
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->pts > to) {
            break;
        }
        intptr_t bytes = list_unlink_fragments(&avs->head, &head);
        avs->video.frames--;
        unlock(&avs->lck);
        my_assert(bytes > 0);

        write_fragments(mfio, avs, &head, bytes);
        lock(&avs->lck);
    }
    unlock(&avs->lck);
}

static void campaign_leader(av_stream_t* avs)
{
    if (avs != avs->mfio->grant) {
        lock(&avs->mfio->lck);
        if ((avs->audio.bytes > 0 && avs->mfio->elect == NULL) ||
            (avs->audio.bytes > avs->mfio->elect->audio.bytes))
        {
            avs->mfio->elect = avs;
        }
        unlock(&avs->mfio->lck);
    }
}

static uint32_t mix_audio_range(mp4_mfio_t* mfio, av_stream_t* avs, uint64_t to)
{
    if (to <= mfio->pts) {
        return 0;
    }

    struct list_head head;
    INIT_LIST_HEAD(&head);
    char* end = NULL;
    struct my_buffer* end_mbuf = NULL;

    uint64_t bytes_out = 0;
    uint64_t pts_begin = 0;

    lock(&avs->lck);
    while (!list_empty(&avs->head)) {
        struct list_head* ent = avs->head.next;
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->pts >= to) {
            break;
        }
        list_del(ent);
        avs->audio.bytes -= media->vp[0].stride;
        campaign_leader(avs);
        unlock(&avs->lck);

        uint32_t bytes = (int32_t) media->vp[0].stride;
        bytes_out += bytes;
        if (pts_begin == 0) {
            pts_begin = media->pts;
        }

        audio_format* fmt = to_audio_format(media->pptr_cc);
        fraction frac = pcm_bytes_from_ms(fmt->pcm, (uint32_t) (to - media->pts));
        uint32_t need = frac.num / frac.den;
        list_add_tail(ent, &head);

        lock(&avs->lck);
        if (need > bytes) {
            continue;
        }

        if (bytes > need) {
            need &= ~avs->audio.mask;
            end = media->vp[0].ptr + need;
            end_mbuf = mbuf;
        }
        break;
    }
    unlock(&avs->lck);

    struct my_buffer* mbuf = NULL;
    media_buffer* media = NULL;
    int32_t* int32p = (int32_t *) mfio->pcmbuf->ptr[1];
    uint32_t bytes = (uint32_t) mfio->pcmbuf->length;

    // currently all input pcm are same formats
    while (!list_empty(&head)) {
        struct list_head* ent = head.next;
        list_del(ent);

        mbuf = list_entry(ent, struct my_buffer, head);
        media = (media_buffer *) mbuf->ptr[0];
        my_assert(0 == (media->vp[0].stride & avs->audio.mask));

        int16_t* int16p = (int16_t *) media->vp[0].ptr;
        for (int i = 0; bytes > 0 && i < media->vp[0].stride; i += 2) {
            *(int32p++) += *(int16p++);
            if (end == (char *) int16p) {
                goto LABEL;
            }
            bytes -= sizeof(int32_t);
        }

        if (end_mbuf != mbuf) {
            mbuf->mop->free(mbuf);
            mbuf = NULL;
        } else {
            mark("end_buf does not reached, bytes_out = %llu, "
                 "pts begin %llu, %llu-->%llu", bytes_out, pts_begin, mfio->pts, to);
        }
    }
    my_assert(list_empty(&head));

LABEL:
    if (end != NULL) {
        my_assert2(mbuf != NULL, "end = %p", end);
        media->vp[0].stride -= (end - media->vp[0].ptr);
        media->vp[0].ptr = end;

        lock(&avs->lck);
        list_add(&mbuf->head, &avs->head);
        avs->audio.bytes += media->vp[0].stride;
        campaign_leader(avs);
        unlock(&avs->lck);
    }

    media = (media_buffer *) mfio->pcmbuf->ptr[0];
    media->pts = mfio->pts;
    media->seq = mfio->seq++;
    bytes = (uint32_t) ((intptr_t) int32p - (intptr_t) mfio->pcmbuf->ptr[1]);
    return bytes;
}

static intptr_t header_initial(AVStream* ffs, struct my_buffer* mbuf)
{
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    int extradata_size = media->vp[0].stride + AV_INPUT_BUFFER_PADDING_SIZE;
    void* extradata = av_mallocz(extradata_size);
    if (extradata == NULL) {
        return -1;
    }

    AVCodecContext* codec = ffs->codec;
    codec->extradata = extradata;
    codec->extradata_size = extradata_size;
    memcpy(codec->extradata, media->vp[0].ptr, media->vp[0].stride);
    mbuf->mop->free(mbuf);

    return 0;
}

static void write_audio(mp4_mfio_t* mfio)
{
    struct my_buffer* mbuf = mfio->pcmbuf->mop->clone(mfio->pcmbuf);
    if (mbuf == NULL) {
        return;
    }

    struct list_head head;
    INIT_LIST_HEAD(&head);
    int32_t nb = codec_write(mfio->aac, mbuf, &head, NULL);
    if (nb == -1) {
        mbuf->mop->free(mbuf);
        return;
    }

    while (!list_empty(&head)) {
        struct list_head* ent = head.next;
        list_del(ent);
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];

        if (mfio->aac_header == 0) {
            intptr_t n = 0;
            struct my_buffer* header = stream_header_get(media->iden, media->pptr_cc);
            if (header != NULL) {
                n = header_initial(mfio->ffas, header);
            }

            if (n == -1) {
                header->mop->free(header);
            }
            mfio->aac_header = 1;
        }

        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.stream_index = mfio->ffas->index;
        pkt.pts = av_rescale_q(media->pts, mfio->ffas->codec->time_base, mfio->ffas->time_base);
        pkt.dts = av_rescale_q(media->pts, mfio->ffas->codec->time_base, mfio->ffas->time_base);
        pkt.flags = 0;

        pkt.data = (uint8_t *) media->vp[0].ptr;
        pkt.size = (int) media->vp[0].stride;

        av_interleaved_write_frame(mfio->ffctx, &pkt);
        mbuf->mop->free(mbuf);
    }
}

static void try_clean_stream(av_stream_t* avs)
{
    if (avs->eos == 0) {
        return;
    }

    intptr_t empty = 0;
    if (audio_type == media_type(*avs->pfcc)) {
        empty = (avs->audio.bytes == 0);
    } else {
        empty = (avs->video.frames == 0);
    }

    if (!empty) {
        return;
    }

    if (video_type == media_type(*avs->pfcc)) {
        if (avs->video.frames == 0) {
            if (avs->video.frame != NULL) {
                my_free(avs->video.frame);
            }
            free_buffer(&avs->video.fragments);
        }
    }
    my_assert(list_empty(&avs->head));
    list_del(&avs->entry);
    handle_release(avs->self);
    heap_free(avs);
}

static void write_mp4_range(mp4_mfio_t* mfio, uint64_t to, uint32_t include_audio)
{
    if (to <= mfio->pts) {
        return;
    }
    uint64_t pts = mfio->pts;

    struct list_head* ent;
    for (pts = mfio->pts; pts < to; mfio->pts = pts) {
        uint32_t bytes = 0;
        pts += mfio->ms;
        pts = my_min(pts, to);

        if (include_audio) {
            memset(mfio->pcmbuf->ptr[1], 0, mfio->pcmbuf->length);
        }

        for (ent = mfio->head.next; ent != &mfio->head;) {
            av_stream_t* avs = list_entry(ent, av_stream_t, entry);
            ent = ent->next;

            if (avs->pfcc == NULL) {
                continue;
            }

            if (audio_type == media_type(*avs->pfcc)) {
                if (include_audio) {
                    uint32_t nb = mix_audio_range(mfio, avs, pts);
                    if (nb > bytes) {
                        bytes = nb;
                    }
                } else {
                    continue;
                }
            } else {
                write_video_range(mfio, avs, pts);
            }
            try_clean_stream(avs);
        }

        if (bytes == 0) {
            continue;
        }

        char* frame = mfio->pcmbuf->ptr[1];
        int16_t* int16p = (int16_t *) frame;
        for (uint32_t i = 0; i < bytes; i += 4) {
            int32_t* intp = (int32_t *) &frame[i];
            if (intp[0] > 32767) {
                *(int16p++) = 32767;
            } else if (intp[0] < -32768) {
                *(int16p++) = -32768;
            } else {
                *(int16p++) = (int16_t) (*intp);
            }
        }

        media_buffer* media = (media_buffer *) mfio->pcmbuf->ptr[0];
        uint32_t saved = media->vp[0].stride;
        media->vp[0].stride = bytes / 2;
        write_audio(mfio);
        media->vp[0].stride = saved;
    }
}

static void* writer_thread(void* cptr)
{
    my_handle* handle = (my_handle *) cptr;
    mp4_mfio_t* mfio = (mp4_mfio_t *) handle_get(handle);

    while (mfio != NULL) {
        uint64_t pts = 0;

        lock(&mfio->lck);
        av_stream_t* avs = mfio->grant ? mfio->grant : mfio->elect;
        if (avs != NULL) {
            mfio->elect = NULL;
            pts = audio_pts_mixto(avs);
        } else {
            pts = video_pts_max(mfio);
        }
        unlock(&mfio->lck);

        if (pts > 0) {
            write_mp4_range(mfio, pts, avs != NULL);
        }

        handle_put(handle);
        usleep(1000 * 80);
        mfio = (mp4_mfio_t *) handle_get(handle);
    }

    handle_release(handle);
    return NULL;
}

static void clean_streams(mp4_mfio_t* mfio)
{
    while (!list_empty(&mfio->head)) {
        struct list_head* ent = mfio->head.next;
        list_del(ent);
        av_stream_t* avs = list_entry(ent, av_stream_t, entry);
        handle_dettach(avs->self);

        if (avs->pfcc != NULL) {
            if (video_type == media_type(*avs->pfcc)) {
                if (avs->video.frame != NULL) {
                    my_free(avs->video.frame);
                }
                free_buffer(&avs->video.fragments);
            }
            free_buffer(&avs->head);
        }
        heap_free(avs);
    }
}

static void mfio_clean(void* addr)
{
    mp4_mfio_t* mfio = (mp4_mfio_t *) addr;
    mfio->eof = 1;

    if (mfio->ffctx != NULL) {
        if (mfio->ffctx->pb != NULL) {
            write_mp4_range(mfio, mfio->pts + safe_ms + 200, mfio->pcmbuf != NULL);
            av_write_trailer(mfio->ffctx);
            avio_close(mfio->ffctx->pb);
            clean_streams(mfio);
        }

        if (mfio->aac != NULL) {
            codec_close(mfio->aac);
        }

        if (mfio->h264 != NULL) {
            codec_close(mfio->h264);
        }

        if (mfio->pcmbuf != NULL) {
            mfio->pcmbuf->mop->free(mfio->pcmbuf);
        }
        avformat_free_context(mfio->ffctx);
    }
    heap_free(mfio);
}

static AVStream* ffmpeg_aac_init(mp4_mfio_t* mfio, media_buffer* media)
{
    audio_format* fmt = to_audio_format(media->pptr_cc);
    audio_format* pcm = audio_raw_format(fmt->pcm);
    mfio->aac = codec_open(&pcm->cc, &fmt->cc, NULL);
    if (mfio->aac == NULL) {
        return NULL;
    }

    AVFormatContext* oc = mfio->ffctx;
    AVStream* ffs = avformat_new_stream(oc, NULL);
    if (ffs == NULL) {
        codec_close(mfio->aac);
        mfio->aac = NULL;
        return NULL;
    }
    ffs->id = oc->nb_streams - 1;

    AVCodecContext* codec = ffs->codec;
    codec->codec_type = AVMEDIA_TYPE_AUDIO;
    codec->codec_id = AV_CODEC_ID_AAC;
    codec->sample_rate = fmt->pcm->samrate;
    codec->channels = fmt->pcm->channel;
    codec->channel_layout = (fmt->pcm->channel == 2) ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
    codec->sample_fmt = AV_SAMPLE_FMT_FLTP;
    codec->frame_size = 1024;

    codec->time_base.num = 1;
    codec->time_base.den = 1000;
    ffs->time_base.num = 1;
    ffs->time_base.den = 1000;

    my_assert(oc->oformat->flags & AVFMT_GLOBALHEADER);
    codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    return ffs;
}

static AVStream* ffmpeg_h264_init(AVFormatContext* oc, media_buffer* media)
{
    AVStream* ffs = avformat_new_stream(oc, NULL);
    if (ffs == NULL) {
        return NULL;
    }
    ffs->id = oc->nb_streams - 1;

    video_format* fmt = to_video_format(media->pptr_cc);
    AVCodecContext* codec = ffs->codec;
    codec->codec_type = AVMEDIA_TYPE_VIDEO;
    codec->codec_id = AV_CODEC_ID_H264;
    codec->width = fmt->pixel->size->width;
    codec->height = fmt->pixel->size->height;
    codec->time_base.num = 1;
    codec->time_base.den = 1000;
    ffs->time_base.num = 1;
    ffs->time_base.den = 1000;

    my_assert(oc->oformat->flags & AVFMT_GLOBALHEADER);
    codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    return ffs;
}

static int mp4_file_initial(mp4_mfio_t* mfio, const char* file, media_buffer* audio, media_buffer* video)
{
    int n = avformat_alloc_output_context2(&mfio->ffctx, NULL, NULL, file);
    if (n < 0) {
        mfio->ffctx = NULL;
        return -1;
    }

    my_assert(mfio->ffctx != NULL);
    my_assert(0 == (mfio->ffctx->oformat->flags & AVFMT_NOFILE));
    my_assert(mfio->ffctx->pb == NULL);
    n = avio_open(&mfio->ffctx->pb, file, AVIO_FLAG_WRITE);
    if (n < 0) {
        return -1;
    }

    if (audio != NULL) {
        mfio->ffas = ffmpeg_aac_init(mfio, audio);
        if (mfio->ffas == NULL) {
            return -1;
        }
    }

    if (video != NULL) {
        mfio->ffvs = ffmpeg_h264_init(mfio->ffctx, video);
        if (mfio->ffvs == NULL) {
            return -1;
        }
    }

    if (0 != avformat_write_header(mfio->ffctx, NULL)) {
        return -1;
    }
    return 0;
}

static void* mfio_writer_open(const char* file, media_buffer* audio, media_buffer* video)
{
    errno = ENOMEM;
    av_register_all();
    mp4_mfio_t* mfio = heap_alloc(mfio);
    if (mfio == NULL) {
        return NULL;
    }
    memset(mfio, 0, sizeof(mp4_mfio_t));
    INIT_LIST_HEAD(&mfio->head);
    mfio->ms = 40;

    my_handle* handle = handle_attach(mfio, mfio_clean);
    if (handle == NULL) {
        heap_free(mfio);
        return NULL;
    }

    int n = mp4_file_initial(mfio, file, audio, video);
    if (n == -1) {
        handle_dettach(handle);
        return NULL;
    }

    pthread_t tid;
    n = pthread_create(&tid, NULL, writer_thread, handle);
    if (n == 0) {
        handle_clone(handle);
        pthread_detach(tid);
    } else {
        handle_dettach(handle);
        handle = NULL;
    }
    return handle;
}

static void stream_clean(void* addr)
{
    av_stream_t* avs = (av_stream_t *) addr;
    avs->eos = 1;
}

static void* mfio_add_stream(void* mptr)
{
    errno = ENOENT;
    mp4_mfio_t* mfio = (mp4_mfio_t *) handle_get((my_handle *) mptr);
    if (mfio == NULL) {
        return NULL;
    }

    errno = ENOMEM;
    av_stream_t* avs = heap_alloc(avs);
    if (avs == NULL) {
        handle_put((my_handle *) mptr);
        return NULL;
    }

    my_handle* handle = handle_attach(avs, stream_clean);
    if (handle == NULL) {
        heap_free(avs);
        handle_put((my_handle *) mptr);
        return NULL;
    }
    memset(avs, 0, sizeof(av_stream_t));
    INIT_LIST_HEAD(&avs->head);
    avs->iden = media_id_unkown;
    avs->lck = lock_val;
    avs->mfio = mfio;
    avs->self = handle;

    handle_clone(handle);
    lock(&mfio->lck);
    list_add_tail(&avs->entry, &mfio->head);
    unlock(&mfio->lck);

    handle_put((my_handle *) mptr);
    return handle;
}

static void mfio_delete_stream(void* mptr)
{
    my_handle* handle = (my_handle *) mptr;
    handle_dettach(handle);
}

static struct my_buffer* copy_video(struct my_buffer* stream)
{
    media_buffer* media1 = (media_buffer *) stream->ptr[0];
    struct my_buffer* mbuf = mbuf_alloc_3(sizeof(media_buffer) + media1->vp[0].stride);
    if (mbuf == NULL) {
        return NULL;
    }
    media_buffer* media2 = (media_buffer *) mbuf->ptr[0];
    mbuf->ptr[1] += sizeof(media_buffer);
    mbuf->length -= sizeof(media_buffer);
    *media2 = *media1;
    media2->vp[0].ptr = mbuf->ptr[1];
    memcpy(media2->vp[0].ptr, media1->vp[0].ptr, media2->vp[0].stride);
    return mbuf;
}

static intptr_t stream_init(av_stream_t* avs1, media_buffer* media, uint32_t current)
{
    mp4_mfio_t* mfio = avs1->mfio;
    my_assert(current >= mfio->tms);
    avs1->offset = (int64_t) media->pts - (current - mfio->tms);

    if (video_type == media_type(*media->pptr_cc)) {
        if (media->frametype == video_type_sps) {
            return -1;
        }

        char buf[16];
        sprintf(buf, "%d", media->angle * 90);
        av_dict_set(&mfio->ffvs->metadata, "rotate", buf, 0);

        if (mfio->rencode_video) {
            video_format* fmt_in = to_video_format(media->pptr_cc);
            video_format* fmt_out = video_format_get(codec_h264, csp_any, fmt_in->pixel->size->width, fmt_in->pixel->size->height);
            mfio->h264 = codec_open(&fmt_in->cc, &fmt_out->cc, NULL);
            if (mfio->h264 == NULL) {
                return -1;
            }
        } else {
            my_assert((*media->pptr_cc)->code == codec_h264);
            struct my_buffer* header = stream_header_get(media->iden, media->pptr_cc);
            if (header == NULL) {
                return -1;
            }

            struct my_buffer* mbuf = copy_video(header);
            media_buffer* media2 = (media_buffer *) mbuf->ptr[0];
            media2->pts = media->pts - avs1->offset;
            header->mop->free(header);
            if (mbuf == NULL) {
                return -1;
            }

            if (-1 == header_initial(mfio->ffvs, mbuf)) {
                mbuf->mop->free(mbuf);
                return -1;
            }
        }
        avs1->video.reflost = 1;
        INIT_LIST_HEAD(&avs1->video.fragments);
        INIT_LIST_HEAD(&avs1->video.head);
        for (int i = 0; i < elements(avs1->video.dts); ++i) {
            avs1->video.dts[i].dts = i + 1;
            list_add_tail(&avs1->video.dts[i].entry, &avs1->video.head);
        }
    } else {
        fraction frac;
        audio_format* fmt = to_audio_format(media->pptr_cc);
        if (mfio->pcmbuf == NULL) {
            frac = pcm_bytes_from_ms(fmt->pcm, mfio->ms);
            uint32_t bytes = (frac.num * sizeof(uint32_t)) / (frac.den * sizeof(uint16_t));
            mfio->pcmbuf = mbuf_alloc_2(sizeof(media_buffer) + bytes);
            if (mfio->pcmbuf == NULL) {
                return -1;
            }

            mfio->pcmbuf->ptr[1] = mfio->pcmbuf->ptr[0] + sizeof(media_buffer);
            mfio->pcmbuf->length -= sizeof(media_buffer);
            media_buffer* media2 = (media_buffer *) mfio->pcmbuf->ptr[0];
            memset(media2->vp, 0, sizeof(media2->vp));
            media2->iden = media->iden;
            media2->pptr_cc = media->pptr_cc;
            media2->vp[0].ptr = mfio->pcmbuf->ptr[1];
            media2->vp[0].stride = (uint32_t) mfio->pcmbuf->length / 2;
            media2->vp[0].height = 1;
            media2->fragment[0] = 0;
            media2->fragment[1] = 1;
            media2->frametype = 0;
            media2->angle = 0;
            media2->seq = 0;
            media2->pts = 0;
        }

        if (media_id_eqal(media->iden, media_id_self)) {
            my_assert(mfio->grant == NULL);
            mfio->grant = avs1;
        }

        frac = pcm_bytes_from_ms(fmt->pcm, safe_ms);
        avs1->audio.safe_bytes = frac.num / frac.den;
        avs1->audio.bytes = 0;
        intptr_t align = fmt->pcm->channel * fmt->pcm->sambits / 8;
        my_assert(align == 2 || align == 4);
        avs1->audio.mask = align - 1;
    }
    avs1->iden = media->iden;
    avs1->pfcc = media->pptr_cc;
    return 0;
}

typedef struct {
    struct my_buffer* mbuf;
    struct mbuf_operations* mop;
} pcm_packet_t;

static void free_audio(struct my_buffer* mbuf)
{
    pcm_packet_t* packet = (pcm_packet_t *) mbuf->ptr[1];
    packet->mbuf->mop->free(packet->mbuf);
    mbuf->mop = packet->mop;
    mbuf->mop->free(mbuf);
}

static struct my_buffer* copy_audio(struct my_buffer* mbuf)
{
    static struct mbuf_operations mop = {
        .clone = NULL,
        .free = free_audio
    };

    struct my_buffer* mbuf2 = mbuf_alloc_3(sizeof(media_buffer) + sizeof(pcm_packet_t));
    if (mbuf2 == NULL) {
        return NULL;
    }
    mbuf2->ptr[1] += sizeof(media_buffer);
    mbuf2->length -= sizeof(media_buffer);

    media_buffer* media1 = (media_buffer *) mbuf->ptr[0];
    media_buffer* media2 = (media_buffer *) mbuf2->ptr[0];
    *media2 = *media1;

    pcm_packet_t* packet = (pcm_packet_t *) mbuf2->ptr[1];
    packet->mbuf = mbuf->mop->clone(mbuf);
    if (packet->mbuf == NULL) {
        mbuf2->mop->free(mbuf2);
        return NULL;
    }
    packet->mop = mbuf2->mop;
    mbuf2->mop = &mop;
    return mbuf2;
}

static intptr_t stream_append_pcm(av_stream_t* avs, struct my_buffer* mbuf)
{
    mbuf = copy_audio(mbuf);
    if (mbuf == NULL) {
        return -1;
    }
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    my_assert(media->vp[1].stride == 0);

    lock(&avs->lck);
    list_add_tail(&mbuf->head, &avs->head);
    wmb();
    avs->audio.bytes += media->vp[0].stride;
    media->pts -= avs->offset;
    campaign_leader(avs);

    unlock(&avs->lck);
    return 0;
}

static intptr_t stream_append_h264(av_stream_t* avs, struct my_buffer* mbuf)
{
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    if (avs->video.reflost == 1) {
        if (reference_others(media->frametype)) {
            return -1;
        } else if (video_type_idr == media->frametype) {
            avs->video.reflost = 0;
        }
    }

    mbuf = copy_video(mbuf);
    if (mbuf == NULL) {
        free_buffer(&avs->video.fragments);
        if (reference_able(media->frametype)) {
            avs->video.reflost = 1;
        }
        return -1;
    }
    media = (media_buffer *) mbuf->ptr[0];
    media->pts -= avs->offset;

    list_add_tail(&mbuf->head, &avs->video.fragments);
    if (media->fragment[0] == 0) {
        if (media->vp[0].ptr[2] == 1) {
            // VCL nalu should always has 4 bytes annexb.
            // reference_able is subset of VCL nalu, however
            // it may fails some frame to be decoded.
            my_assert(!reference_able(media->frametype));
            return -1;
        }
        my_assert(media->vp[0].ptr[2] == 0 && media->vp[0].ptr[3] == 1);
    }

    if (media->fragment[0] == media->fragment[1] - 1) {
        lock(&avs->lck);
        list_join(&avs->video.fragments, &avs->head);
        list_del_init(&avs->video.fragments);
        avs->video.frames++;
        unlock(&avs->lck);
    }
    return 0;
}

static intptr_t mp4_video_header_init(av_stream_t* avs, struct my_buffer* mbuf)
{
    AVStream* ffs = avs->mfio->ffvs;
    media_buffer* media = (media_buffer *) mbuf->ptr[0];

    struct my_buffer* header = stream_header_get(media->iden, media->pptr_cc);
    if (header == NULL) {
        return -1;
    }

    struct my_buffer* sps = copy_video(header);
    header->mop->free(header);
    if (sps == NULL) {
        return -1;
    }

    intptr_t n = header_initial(ffs, sps);
    if (n == -1) {
        sps->mop->free(sps);
    }
    return n;
}

static intptr_t stream_append_pixel(av_stream_t* avs, struct my_buffer* mbuf)
{
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    my_assert(media->fragment[1] == 1);

    if (avs->mfio->callback != NULL) {
        struct my_buffer* mbuf2 = avs->mfio->callback(mbuf);
        if (mbuf2 == mbuf) {
            mbuf = mbuf->mop->clone(mbuf);
        } else {
            mbuf = mbuf2;
        }
    } else {
        mbuf = mbuf->mop->clone(mbuf);
    }

    if (mbuf == NULL) {
        return -1;
    }

    struct list_head head;
    INIT_LIST_HEAD(&head);
    int32_t n = codec_write(avs->mfio->h264, mbuf, &head, NULL);
    if (n == -1) {
        mbuf->mop->free(mbuf);
        return -1;
    }

    lock(&avs->lck);
    while (!list_empty(&head)) {
        struct list_head* ent = head.next;
        list_del(ent);

        mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        my_assert(media->fragment[1] == 1);
        media->pts -= avs->offset;
        uint32_t frametype = media->frametype;

        AVCodecContext* codec = avs->mfio->ffvs->codec;
        if (__builtin_expect(codec->extradata == NULL, 0)) {
            intptr_t n = mp4_video_header_init(avs, mbuf);
            if (n == -1 || frametype == video_type_sps) {
                mbuf->mop->free(mbuf);
                continue;
            }
        }

        if (__builtin_expect(avs->video.reflost == 1, 0)) {
            if (reference_others(frametype)) {
                mbuf->mop->free(mbuf);
                continue;
            } else if (video_type_idr == frametype) {
                avs->video.reflost = 0;
            }
        }
        list_add_tail(&mbuf->head, &avs->head);
    }
    unlock(&avs->lck);
    return 0;
}

static intptr_t mfio_stream_write(void* mptr, struct my_buffer* mbuf)
{
    my_handle* handle = (my_handle *) mptr;
    av_stream_t* avs = (av_stream_t *) handle_get(handle);
    if (__builtin_expect(avs == NULL, 0)) {
        return -1;
    }

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    intptr_t raw = media_type_raw(media->pptr_cc);
    if (avs->mfio->rencode_video || audio_type == media_type(*media->pptr_cc)) {
        if (!raw) {
            handle_put(handle);
            return -1;
        }
    } else if (raw) {
        handle_put(handle);
        return -1;
    }

    if (__builtin_expect(avs->mfio->eof, 0)) {
        handle_put(handle);
        return -1;
    }

    if (__builtin_expect(media_id_eqal(avs->iden, media_id_unkown), 0)) {
        uint32_t current = now();
        lock(&avs->mfio->lck);
        if (avs->mfio->tms == 0) {
            avs->mfio->tms = current;

        }
        unlock(&avs->mfio->lck);

        if (-1 == stream_init(avs, media, current)) {
            handle_put(handle);
            return -1;
        }
    }

    intptr_t n;
    if (audio_type == media_type(*media->pptr_cc)) {
        n = stream_append_pcm(avs, mbuf);
    } else if (avs->mfio->rencode_video) {
        n = stream_append_pixel(avs, mbuf);
    } else {
        n = stream_append_h264(avs, mbuf);
    }
    handle_put(handle);
    return n;
}

static void mfio_writer_close(void* mptr)
{
    my_handle* handle = (my_handle *) mptr;
    handle_dettach(handle);
}

mfio_writer_t mp4_mfio_writer = {
    .open = mfio_writer_open,
    .add_stream = mfio_add_stream,
    .delete_stream = mfio_delete_stream,
    .write = mfio_stream_write,
    .close = mfio_writer_close
};

void mp4_video_config(void* mptr, intptr_t encode_video, pixel_translate_t callback)
{
    my_handle* handle = (my_handle *) mptr;
    mp4_mfio_t* mfio = (mp4_mfio_t *) handle_get((my_handle *) handle);
    if (mfio == NULL) {
        return;
    }
    mfio->rencode_video = encode_video;
    if (mfio->rencode_video) {
        mfio->callback = callback;
    }
    handle_put(handle);
}
