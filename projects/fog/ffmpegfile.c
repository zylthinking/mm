
#include "my_handle.h"
#include "fileit.h"
#include "ffmpegfile.h"
#include "lock.h"
#include "mbuf.h"
#include "my_buffer.h"
#include "ff_buffer.h"
#include "file_jointor.h"
#include "libavformat/avformat.h"
#include <pthread.h>

#define path_bytes 256
#define ms_from_ffmpeg_tick(x, pts_unit) (1000 * (x) * pts_unit.num / pts_unit.den)

static const char* h264_mp4toannexb = "h264_mp4toannexb";
typedef struct {
    intptr_t magic;
    const char* bsfc;
    uintptr_t latch[2];
    uintptr_t prefetch;
    uintptr_t avsync;
    uintptr_t seekable;
    uintptr_t eof_reconn;
} ffmpeg_file_config_t;

typedef struct {
    AVCodecContext* codectx;
    AVBitStreamFilterContext* bsfc;
    intptr_t header_recieved;

    lock_t lck;
    intptr_t refs;
    intptr_t idx;
    enum AVCodecID codecid;
    int64_t pts_base;
    AVRational pts_unit;
    media_buffer media;

    uint64_t duration;
    uint64_t maxpts;
    uint64_t outpts;

    struct list_head head;
    struct list_head entry;
    my_handle* self;
    my_handle* file;
} ffmpeg_stream_t;

typedef struct {
    lock_t lck;
    ffmpeg_file_config_t conf;
    callback_t cb;
    fcontext_t fcontext;
    int64_t seekto;
    uint64_t cursor;
    jointor* joint;

    union {
        struct {
            uint8_t zero;
            uint8_t eof;
            uint64_t maxpts;
            int64_t pts_offset;
            struct list_head head;
            ffmpeg_stream_t* ffs;
            media_iden_t mediaid;
            uint32_t readtms;
            uint32_t timeout;
        } main;

        char pathname[path_bytes];
    } u;
    AVFormatContext* fmtctx;
} ffmpeg_context_t;

static intptr_t illegal(AVStream* stream) {
    if (stream->codec->codec_type != AVMEDIA_TYPE_AUDIO &&
        stream->codec->codec_type != AVMEDIA_TYPE_VIDEO)
    {
        return 1;
    }

    if (stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (stream->codec->sample_rate == 0 || stream->codec->channels == 0) {
            return 1;
        }
    }
    return 0;
}

static intptr_t same_stream(ffmpeg_stream_t* ffs, AVStream* stream)
{
    return (ffs->codecid == stream->codec->codec_id);
}

static intptr_t init_media(AVStream* stream, media_buffer* media, AVCodecContext* codectx)
{
    int chans_height, samrate_width, codec_type;
    errno = ENOSYS;
    uint32_t angle = 0;
    enum AVCodecID codecid = codectx->codec_id;
    if (AV_CODEC_ID_OPUS == codecid) {
        chans_height = codectx->channels;
        samrate_width = codectx->sample_rate;
        codec_type = codec_opus;
    } else if (AV_CODEC_ID_AAC == codecid) {
        chans_height = codectx->channels;
        samrate_width = codectx->sample_rate;
        codec_type = codec_aac;

        if (codectx->profile == FF_PROFILE_AAC_HE || codectx->profile == FF_PROFILE_AAC_HE_V2) {
            samrate_width /= 2;
            chans_height /= 2;
            codec_type = codec_aacplus;
        } else if (codectx->profile > FF_PROFILE_AAC_HE) {
            return -1;
        }
    } else if (AV_CODEC_ID_H264 == codecid) {
        AVDictionaryEntry* entry = av_dict_get(stream->metadata, "rotate", NULL, 0);
        if (entry != NULL) {
            angle = atoi(entry->value);
            angle /= 90;
        }
        samrate_width = codectx->width;
        chans_height = codectx->height;
        codec_type = codec_h264;
    } else {
        logmsg("codec_id: %d, chans = %d, samrate = %d, width = %d height = %d\n",
               codecid, codectx->channels, codectx->sample_rate, codectx->width, codectx->height);
        return -1;
    }

    media->pptr_cc = fourcc_get(codec_type, samrate_width, chans_height);
    if (media->pptr_cc == NULL) {
        return -1;
    }
    memset(media->vp, 0, sizeof(media->vp));
    media->fragment[0] = 0;
    media->fragment[1] = 1;
    media->angle = angle;
    media->seq = 0;
    media->pts = 0;
    media->iden = media_id_uniq();

    return 0;
}

static void stream_release(ffmpeg_stream_t* ffs)
{
    intptr_t n = __sync_sub_and_fetch(&ffs->refs, 1);
    if (n == 0) {
        if (ffs->bsfc != NULL) {
            av_bitstream_filter_close(ffs->bsfc);
        }
        my_free(ffs);
    }
}

static void stream_free(void* addr)
{
    ffmpeg_stream_t* ffs = (ffmpeg_stream_t *) addr;
    free_buffer(&ffs->head);
    if (ffs->file != NULL) {
        ffmpeg_context_t* ffc = (ffmpeg_context_t *) handle_get(ffs->file);
        if (ffc != NULL) {
            lock(&ffc->lck);
            list_del_init(&ffs->entry);
            unlock(&ffc->lck);
            handle_release(ffs->self);
            handle_put(ffs->file);
            ffs->self = NULL;
        }
        handle_release(ffs->file);
        ffs->file = NULL;
    }
    stream_release(ffs);
}

static my_handle* ffmpeg_stream_add(AVStream* stream, my_handle* ffh, ffmpeg_context_t* ffc)
{
    errno = ENOMEM;
    my_handle* handle = NULL;
    ffmpeg_stream_t* ffs = ffc->u.main.ffs;

    if (ffs != NULL && same_stream(ffs, stream)) {
        my_assert(ffs != NULL);
        my_assert(ffs->refs == 1);
        my_assert(ffs->self == NULL);
        my_assert(ffs->file == NULL);
        handle = handle_attach(ffs, stream_free);
        if (handle == NULL) {
            return NULL;
        }
        ffs->idx = stream->index;
        ffs->refs = 2;
        ffs->codectx = stream->codec;
        ffs->header_recieved = 0;
    } else {
        ffs = (ffmpeg_stream_t *) my_malloc(sizeof(ffmpeg_stream_t));
        if (ffs == NULL) {
            return NULL;
        }

        handle = handle_attach(ffs, stream_free);
        if (handle == NULL) {
            return NULL;
        }

        ffs->lck = lock_val;
        ffs->idx = stream->index;
        ffs->codecid = stream->codec->codec_id;
        ffs->refs = 1;
        ffs->codectx = stream->codec;
        ffs->pts_unit = stream->time_base;
        ffs->pts_base = stream->start_time;
        ffs->duration = 0;
        ffs->maxpts = 0;
        ffs->outpts = 0;
        ffs->file = NULL;
        ffs->bsfc = NULL;
        ffs->header_recieved = 0;

        if (-1 == init_media(stream, &ffs->media, ffs->codectx)) {
            return NULL;
        }

        if (ffc->conf.avsync) {
            ffs->media.iden = ffc->u.main.mediaid;
        }

        INIT_LIST_HEAD(&ffs->head);
        INIT_LIST_HEAD(&ffs->entry);

        if (ffc->conf.bsfc != NULL) {
            ffs->bsfc = av_bitstream_filter_init(ffc->conf.bsfc);
            if (ffs->bsfc == NULL) {
                handle_dettach(handle);
                return NULL;
            }
        }
    }

    handle_clone(handle);
    int n = file_jointor_notify_stream(ffc->joint, handle, (free_t) handle_release);
    if (-1 == n) {
        handle_release(handle);
        handle_dettach(handle);
        return NULL;
    }

    handle_clone(handle);
    ffs->self = handle;
    lock(&ffc->lck);
    list_add(&ffs->entry, &ffc->u.main.head);
    unlock(&ffc->lck);

    handle_clone(ffh);
    ffs->file = ffh;
    return handle;
}

static void ffmpeg_stream_clear(ffmpeg_context_t* ffc)
{
    lock(&ffc->lck);
    struct list_head* ent;
    for (ent = ffc->u.main.head.next; ent != &ffc->u.main.head;) {
        ffmpeg_stream_t* ffs = list_entry(ent, ffmpeg_stream_t, entry);
        ent = ent->next;
        handle_clone(ffs->self);
        handle_dettach(ffs->self);
    }
    unlock(&ffc->lck);
}

static intptr_t ffmpeg_read_header(my_handle* ffh, ffmpeg_context_t* ffc)
{
    ffc->u.main.eof = 0;
    ffc->u.main.maxpts = 0;
    ffc->u.main.pts_offset = 0;

    ffc->u.main.readtms = now();
    ffc->u.main.timeout = 0;
    int n = avformat_find_stream_info(ffc->fmtctx, NULL);
    if (n < 0) {
        errno = ENODATA;
        return -1;
    }

    my_handle* video_handle = NULL;
    for (int i = 0; i < ffc->fmtctx->nb_streams; ++i) {
        AVStream* stream = ffc->fmtctx->streams[i];
        if (illegal(stream)) {
            continue;
        }

        my_handle* handle = ffmpeg_stream_add(stream, ffh, ffc);
        if (handle == NULL) {
            ffmpeg_stream_clear(ffc);
            return -1;
        }

        if (ffc->conf.seekable && ffc->u.main.ffs == NULL) {
            if (stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
                ffmpeg_stream_t* ffs = handle_get(handle);
                if (ffs != NULL) {
                    ffc->u.main.ffs = ffs;
                    // it's safe
                    ffs->refs = 2;
                    handle_put(handle);
                }
                handle_release(handle);
            } else if (video_handle == NULL) {
                video_handle = handle;
            } else {
                handle_release(handle);
            }
        } else {
            handle_release(handle);
        }
    }

    if (ffc->u.main.ffs == NULL && video_handle != NULL) {
        ffmpeg_stream_t* ffs = handle_get(video_handle);
        if (ffs != NULL) {
            ffc->u.main.ffs = ffs;
            // it's safe
            ffs->refs = 2;
            handle_put(video_handle);
        }
        handle_release(video_handle);
    }

    if (ffc->u.main.ffs != NULL && ffc->fmtctx->duration != -1) {
        ffc->fcontext.duration = ffc->fmtctx->duration / (AV_TIME_BASE / 1000);
    }
    ffc->cb.notify(ffc->cb.uptr, context_update, &ffc->fcontext);
    return 0;
}

static void ffmpeg_stream_clear_buffer(ffmpeg_stream_t* ffs)
{
    struct list_head head;
    lock(&ffs->lck);
    list_add(&head, &ffs->head);
    list_del_init(&ffs->head);
    ffs->duration = 0;
    ffs->maxpts = 0;
    ffs->media.seq -= 16;
    unlock(&ffs->lck);
    free_buffer(&head);
}

static void ffmpeg_file_clear_buffer(ffmpeg_context_t* ffc)
{
    struct list_head* ent = NULL;
    lock(&ffc->lck);
    for (ent = ffc->u.main.head.next; ent != &ffc->u.main.head; ent = ent->next) {
        ffmpeg_stream_t* ffs = list_entry(ent, ffmpeg_stream_t, entry);
        ffmpeg_stream_clear_buffer(ffs);
    }
    ffc->fcontext.have = 0;
    ffc->fcontext.more = ffc->conf.latch[0];
    unlock(&ffc->lck);
}

static uint64_t ffmpeg_file_max_pts(ffmpeg_context_t* ffc)
{
    uint64_t value = 0;
    struct list_head* ent = NULL;
    for (ent = ffc->u.main.head.next; ent != &ffc->u.main.head; ent = ent->next) {
        ffmpeg_stream_t* ffs = list_entry(ent, ffmpeg_stream_t, entry);
        value = my_max(value, ffs->outpts);
    }
    return value;
}

static uint64_t ffmpeg_file_max_duration(ffmpeg_context_t* ffc)
{
    uint64_t value = 0;
    struct list_head* ent = NULL;
    for (ent = ffc->u.main.head.next; ent != &ffc->u.main.head; ent = ent->next) {
        ffmpeg_stream_t* ffs = list_entry(ent, ffmpeg_stream_t, entry);
        value = my_max(value, ffs->duration);
    }
    return value;
}

static int av_net_callback(void* uptr)
{
    my_handle* handle = (my_handle *) uptr;
    ffmpeg_context_t* ffc = (ffmpeg_context_t *) handle_get(handle);
    if (ffc == NULL) {
        return AVERROR_EXIT;
    }

    uint32_t current = now();
    int n = (current > ffc->u.main.readtms + 12 * 1000);
    if (n == 1) {
        ffc->u.main.timeout = 1;
        n = AVERROR_EXIT;
    }
    handle_put(handle);
    return n;
}

static intptr_t ffmpeg_seek(void* handle, uint64_t seekto)
{
    ffmpeg_context_t* ffc = (ffmpeg_context_t *) handle_get((my_handle *) handle);
    if (ffc == NULL) {
        errno = EBADF;
        return -1;
    }

    if (ffc->u.main.ffs == NULL) {
        errno = ENOSYS;
        handle_put((my_handle *) handle);
        return -1;
    }

    int64_t pos = (int64_t) seekto;
    if (pos < 0 || pos > ffc->fcontext.duration ||
        ffc->fcontext.duration == (uint64_t) -1 || ffc->fcontext.duration == 0)
    {
        errno = EINVAL;
        handle_put((my_handle *) handle);
        return -1;
    }

    lock(&ffc->lck);
    if (ffc->fcontext.cursor == ffc->fcontext.duration) {
        unlock(&ffc->lck);
        errno = ENOENT;
        handle_put((my_handle *) handle);
        return -1;
    }

    ffc->seekto = pos;
    if (ffc->cursor == (uint64_t) -1) {
        ffc->cursor = ffc->fcontext.cursor;
    }
    ffc->fcontext.cursor = pos;
    unlock(&ffc->lck);

    ffmpeg_file_clear_buffer(ffc);
    handle_put((my_handle *) handle);
    return 0;
}

static intptr_t ffmpeg_do_seek(ffmpeg_context_t* ffc, uint64_t seek_once)
{
    ffmpeg_stream_t* ffs = ffc->u.main.ffs;
    my_assert(ffs != NULL);
    ffmpeg_file_clear_buffer(ffc);

    lock(&ffc->lck);
    int64_t seekto = ffc->seekto;
    uint64_t cursor = ffc->fcontext.cursor;
    unlock(&ffc->lck);

    if (seekto != ffc->fcontext.duration) {
        double seek_in_sec = seekto / 1000;
        int64_t seek_in_unit = seek_in_sec * ffs->pts_unit.den / ffs->pts_unit.num;
        int64_t seek_value = seek_in_unit + ffs->pts_base;
        ffc->u.main.readtms = now();
        ffc->u.main.timeout = 0;
        int64_t n = av_seek_frame(ffc->fmtctx, (int) ffs->idx, seek_value, AVSEEK_FLAG_BACKWARD);
        if (n < 0) {
            if (seek_once == seekto) {
                lock(&ffc->lck);
                if (ffc->seekto == seekto) {
                    ffc->seekto = -1;
                    ffc->fcontext.cursor = ffc->cursor;
                    ffc->cursor = (uint64_t) -1;
                    // have to call notify holding lock
                    // to avoid conflicting with ffc->cursor assgiment in seek
                    ffc->cb.notify(ffc->cb.uptr, context_update, &ffc->fcontext);
                }
                unlock(&ffc->lck);
            }
            logmsg("seek to %lld error %d\n", seek_value, n);
            return -1;
        }
        ffc->u.main.eof = 0;
    } else if (ffc->u.main.eof == 0) {
        ffc->u.main.eof = 1;
    }

    lock(&ffc->lck);
    if (cursor != ffc->fcontext.cursor) {
        ffc->cursor = seekto;
    } else {
        ffc->cursor = (uint64_t) -1;
    }

    ffc->u.main.maxpts = ffmpeg_file_max_pts(ffc);
    ffc->seekto = -1;
    unlock(&ffc->lck);

    ffc->u.main.pts_offset = seekto - ffc->u.main.maxpts;
    return 0;
}

static useconds_t chores(ffmpeg_context_t* ffc, uint64_t seekto, intptr_t* reflost)
{
    if (ffc->u.main.eof == 1 || ffc->u.main.eof == 3) {
        intptr_t n = 0;
        lock(&ffc->lck);
        n = list_empty(&ffc->u.main.head);
        unlock(&ffc->lck);

        if (n == 1) {
            if (ffc->u.main.eof == 1) {
                ffc->fcontext.cursor = ffc->fcontext.duration;
                ffc->cb.notify(ffc->cb.uptr, context_update, &ffc->fcontext);
                ffc->u.main.eof = 2;
            }

            intptr_t eof_seekable = 0;
            if (eof_seekable == 0 || ffc->conf.seekable == 0 || ffc->u.main.eof == 3) {
                return (useconds_t) -1;
            }
        }
    }

    if (ffc->seekto != -1) {
        intptr_t n = ffmpeg_do_seek(ffc, seekto);
        if (n == -1) {
            return 1000 * 400;
        }
        *reflost = 1;
    }

    if (ffc->u.main.eof != 0) {
        return 1000 * 400;
    }

    if (ffc->fcontext.have > ffc->conf.prefetch) {
        return (useconds_t) (1000 * (ffc->conf.latch[1]) / 2);
    }
    return 0;
}

static ffmpeg_stream_t* stream_get(my_handle* ffh, ffmpeg_context_t* ffc, int idx)
{
    struct list_head* ent;
    ffmpeg_stream_t* ffs = NULL;

    lock(&ffc->lck);
    for (ent = ffc->u.main.head.next; ent != &ffc->u.main.head; ent = ent->next) {
        ffmpeg_stream_t* ptr = list_entry(ent, ffmpeg_stream_t, entry);
        if (ptr->idx == idx) {
            ffs = handle_get(ptr->self);
            if (ffs != NULL) {
                handle_clone(ptr->self);
            }
            unlock(&ffc->lck);
            return ffs;
        }
    }
    unlock(&ffc->lck);

    AVStream* stream = NULL;
    int i, ns = ffc->fmtctx->nb_streams;
    for (i = 0; i < ns; ++i) {
        stream = ffc->fmtctx->streams[i];
        if (illegal(stream)) {
            continue;
        }

        if (stream->index == idx) {
            break;
        }
    }

    if (i == ns) {
        return NULL;
    }

    my_handle* handle = ffmpeg_stream_add(stream, ffh, ffc);
    if (handle == NULL) {
        return NULL;
    }

    ffs = (ffmpeg_stream_t *) handle_get(handle);
    if (ffs == NULL) {
        handle_release(handle);
    }
    return ffs;
}

static void stream_put(ffmpeg_stream_t* ffs)
{
    handle_put(ffs->self);
    handle_release(ffs->self);
}

static void nalu_free(void* opaque, uint8_t* data)
{
    av_free(data);
}

static uint8_t* next_nalu(uint8_t* nalu, intptr_t bytes)
{
    static uint8_t annexb[3] = {0, 0, 1};
    nalu = (uint8_t *) memmem(nalu, bytes, annexb, 3);
    if (nalu == NULL) {
        return NULL;
    }
    return nalu + 2;
}

static ffmpeg_stream_t* buffer_rebuild(my_handle* ffh, ffmpeg_context_t* ffc, struct my_buffer* mbuf, intptr_t* reflost)
{
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    ff_payload_t* payload = payload_of(mbuf);
    ffmpeg_stream_t* ffs = stream_get(ffh, ffc, payload->packet.stream_index);
    if (ffs == NULL) {
        return NULL;
    }

    int64_t tms = ms_from_ffmpeg_tick(payload->packet.pts - ffs->pts_base, ffs->pts_unit);
    tms = (tms - ffc->u.main.pts_offset);

    if (ffs->codectx->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (tms < (int64_t) ffc->u.main.maxpts) {
            tms = ffc->u.main.maxpts;
        }

        if (payload->packet.flags & AV_PKT_FLAG_KEY) {
            *reflost = 0;
        }

        if (*reflost == 1) {
            stream_put(ffs);
            if (tms > ffc->u.main.maxpts) {
                ffc->u.main.maxpts = tms;
            }
            return NULL;
        }

        if (ffs->bsfc != NULL) {
            uint8_t* nalu;
            int bytes = 0;
            int ret = av_bitstream_filter_filter(ffs->bsfc, ffs->codectx, NULL, &nalu,
                                                 &bytes, (const uint8_t *) payload->packet.data, payload->packet.size, 0);
            if (ret < 0) {
                stream_put(ffs);
                return NULL;
            }

            AVBufferRef* ref = av_buffer_create(nalu, bytes, nalu_free, NULL, 0);
            if (ref == NULL) {
                av_free(nalu);
                stream_put(ffs);
                return NULL;
            }
            av_buffer_unref(&payload->packet.buf);
            payload->packet.data = nalu;
            payload->packet.size = bytes;
            payload->packet.buf = ref;
        }

        memset(media->vp, 0, sizeof(media->vp));
        media->frametype = video_type_unkown;

        if (ffs->header_recieved == 0) {
            uint8_t* nalu = &payload->packet.data[2];
            if (nalu[0] == 0) {
                nalu += 1;
            }
            intptr_t bytes;
LABEL:
            bytes = (intptr_t) (nalu - payload->packet.data);
            if ((nalu[1] & 0x1f) != 7) {
                nalu = next_nalu(nalu, payload->packet.size - bytes);
                if (nalu == NULL) {
                    stream_put(ffs);
                    return NULL;
                }
                goto LABEL;
            }
            ffs->header_recieved = 1;
            media->frametype = video_type_sps;
        } else if (payload->packet.flags & AV_PKT_FLAG_KEY) {
            media->frametype = video_type_idr;
        }

        media->vp[0].stride = (uint32_t) payload->packet.size;
        media->vp[0].ptr = (char *) payload->packet.data;
        mbuf->ptr[1] = (char *) payload->packet.data;
    } else {
        if (tms < (int64_t) ffc->u.main.maxpts) {
            struct list_head head;
            lock(&ffs->lck);
            list_add(&head, &ffs->head);
            list_del_init(&ffs->head);
            unlock(&ffs->lck);
            free_buffer(&head);
            stream_put(ffs);
            return NULL;
        }
        memset(media->vp, 0, sizeof(media->vp));
        media->frametype = 0;
        mbuf->ptr[1] = (char *) payload->packet.data;
        media->vp[0].ptr = mbuf->ptr[1];
        media->vp[0].stride = (uint32_t) payload->packet.size;
    }
    mbuf->length = payload->packet.size;

    media->angle = ffs->media.angle;
    media->fragment[0] = 0;
    media->fragment[1] = 1;
    media->pptr_cc = ffs->media.pptr_cc;
    media->seq = ffs->media.seq++;
    media->pts = tms;
    media->iden = ffs->media.iden;

    return ffs;
}

static uint64_t imprecise_duration(ffmpeg_stream_t* ffs)
{
    if (list_empty(&ffs->head)) {
        return 0;
    }

    struct my_buffer* mbuf1 = list_entry(ffs->head.next, struct my_buffer, head);
    struct my_buffer* mbuf2 = list_entry(ffs->head.prev, struct my_buffer, head);
    media_buffer* media1 = (media_buffer *) mbuf1->ptr[0];
    media_buffer* media2 = (media_buffer *) mbuf2->ptr[0];

    uint64_t n = 0;
    if (media2->pts > media1->pts) {
        n = media2->pts - media1->pts;
    } else {
        n = 1;
    }

    if (n > 60 * 1000) {
        logmsg("%p, %p media2->pts = %llu, media1->pts = %llu, %s\n",
               mbuf2, mbuf1, media2->pts, media1->pts, format_name(media1->pptr_cc));
    }
    return n;
}

static void audio_drop_until(ffmpeg_stream_t* ffs, int64_t tms)
{
    while (!list_empty(&ffs->head)) {
        struct list_head* ent = ffs->head.next;
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->pts >= tms) {
            break;
        }
        list_del(ent);
        mbuf->mop->free(mbuf);
    }
}

static void stream_push(ffmpeg_context_t* ffc, ffmpeg_stream_t* ffs, struct my_buffer* mbuf)
{
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    int64_t tms = (int64_t) media->pts;
    uint32_t type = media_type(*media->pptr_cc);

    // 300 ms greater than any timestamp
    // swapped by B frame (except video less than 3.3 fps)
    // then it should be a seeking back
    intptr_t bp_frame_interval = 300;
    if (audio_type == type) {
        bp_frame_interval = 0;
    }

    if (tms + bp_frame_interval < ffs->maxpts) {
        logmsg_d("this line should only appear once after "
                 "a backward seek %llu for stream %d\n", ffs->maxpts - tms, (int) ffs->idx);
        // this can not provent the pts smaller than previous one
        // being observed by puller, which will cause problems in backward seek audio.
        // while, seekable stream will have a big buffer, after seeking, the buffer
        // will be empty and this happens in this buffering time in most possibility.
        ffmpeg_stream_clear_buffer(ffs);
    }

    intptr_t cb = 0;
    lock(&ffs->lck);
    list_add_tail(&mbuf->head, &ffs->head);
    ffs->maxpts = my_max(tms, ffs->maxpts);
    ffs->duration = imprecise_duration(ffs);
    if (audio_type == type && ffs->duration > ffc->conf.latch[1]) {
        audio_drop_until(ffs, tms - ffc->conf.latch[1]);
        ffs->duration = imprecise_duration(ffs);
    }

    lock(&ffc->lck);
    if (ffc->fcontext.have < ffs->duration) {
        ffc->fcontext.have = ffs->duration;
        if (ffc->fcontext.more != 0) {
            cb = 1;
            if (ffc->fcontext.have >= ffc->conf.latch[0]) {
                ffc->fcontext.more = 0;
            } else {
                ffc->fcontext.more = ffc->conf.latch[0] - ffc->fcontext.have;
            }
        }
    }
    unlock(&ffc->lck);
    unlock(&ffs->lck);

    if (cb == 1) {
        ffc->cb.notify(ffc->cb.uptr, context_update, &ffc->fcontext);
    }
}

static int pts_suggest(ffmpeg_context_t* ffc, ffmpeg_stream_t* ffs, int pts)
{
    if (!list_empty(&ffs->head)) {
        struct list_head* ent = ffs->head.prev;
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->pts > pts + ffc->conf.latch[1]) {
            uint64_t n = media->pts - ffc->conf.latch[1];
            return (int) (n - pts);
        }
    }
    return 0;
}

static int stream_read_video(ffmpeg_context_t* ffc, ffmpeg_stream_t* ffs, const fraction* frac, struct list_head* headp)
{
    int pts = frac->num, nr = 0;
    lock(&ffs->lck);

    if (frac->den == 0) {
        int n = pts_suggest(ffc, ffs, pts);
        if (n != 0) {
            unlock(&ffs->lck);
            return n;
        }
    }

    while (!list_empty(&ffs->head)) {
        struct list_head* ent = ffs->head.next;
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->pts > pts) {
            break;
        }

        list_del(ent);
        list_add_tail(ent, headp);

        if (media->pts > ffs->outpts) {
            ffs->outpts = media->pts;
        }

        if (ffs == ffc->u.main.ffs) {
            lock(&ffc->lck);
            // to provent updating cursor when seeking
            // or else the cursor may over write the position user is seeking
            if (ffc->seekto == -1) {
                ffc->fcontext.cursor = media->pts + ffc->u.main.pts_offset;
                if (ffc->fcontext.cursor >= ffc->fcontext.duration) {
                    ffc->fcontext.cursor = ffc->fcontext.duration - 1;
                }
            }
            unlock(&ffc->lck);
        }
        ++nr;
    }

    ffs->duration = 0;
    if (!list_empty(&ffs->head)) {
        ffs->duration = imprecise_duration(ffs);
    } else {
        ffs->maxpts = 0;
        lock(&ffc->lck);
        if (ffc->u.main.eof != 0) {
            handle_clone(ffs->self);
            handle_dettach(ffs->self);
            if (nr == 0) {
                nr = -1;
            }
        }
        unlock(&ffc->lck);
    }

    lock(&ffc->lck);
    if (nr > 0) {
        ffc->fcontext.have = ffmpeg_file_max_duration(ffc);
        if (ffc->fcontext.have == 0 && ffc->u.main.eof == 0) {
            ffc->fcontext.more = ffc->conf.latch[0];
        }
    }
    unlock(&ffc->lck);

    unlock(&ffs->lck);
    return nr;
}

static int stream_read_audio(ffmpeg_context_t* ffc, ffmpeg_stream_t* ffs, const fraction* frac, struct list_head* headp)
{
    int ms = (frac->num + frac->den - 1) / frac->den, bytes = 0;
    lock(&ffs->lck);

    while (ms > 0 && !list_empty(&ffs->head)) {
        struct list_head* ent = ffs->head.next;
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];

        list_del(ent);
        list_add_tail(ent, headp);

        if (ffs == ffc->u.main.ffs) {
            lock(&ffc->lck);
            // to provent updating cursor when seeking
            // or else the cursor may over write the position user is seeking
            if (ffc->seekto == -1) {
                ffc->fcontext.cursor = media->pts + ffc->u.main.pts_offset;
                if (ffc->fcontext.cursor >= ffc->fcontext.duration) {
                    ffc->fcontext.cursor = ffc->fcontext.duration - 1;
                }
            }
            unlock(&ffc->lck);
        }

        if (media->pts < ffs->outpts) {
            logmsg("media->pts = %llu, ffs->outpts = %llu, ffc->maxpts = %llu, "
                   "pts_offset = %llu\n", media->pts, ffs->outpts, ffc->u.main.maxpts, ffc->u.main.pts_offset);
            my_assert(0);
        } else {
            ms -= (media->pts - ffs->outpts);
            ffs->outpts = media->pts;
        }
        bytes += mbuf->length;
    }

    ffs->duration = 0;
    if (!list_empty(&ffs->head)) {
        ffs->duration = imprecise_duration(ffs);
    } else {
        ffs->maxpts = 0;
        lock(&ffc->lck);
        if (ffc->u.main.eof != 0) {
            handle_clone(ffs->self);
            handle_dettach(ffs->self);
            if (bytes == 0) {
                bytes = -1;
            }
        }
        unlock(&ffc->lck);
    }

    lock(&ffc->lck);
    if (bytes > 0) {
        ffc->fcontext.have = ffmpeg_file_max_duration(ffc);
        if (ffc->fcontext.have == 0 && ffc->u.main.eof == 0) {
            ffc->fcontext.more = ffc->conf.latch[0];
        }
    }
    unlock(&ffc->lck);

    unlock(&ffs->lck);
    return bytes;
}

static void fetch_from_remote(my_handle* handle, uint64_t seekto)
{
    intptr_t reflost = 1;
    useconds_t us = 0;
    while (1) {
        if (us > 0) {
            usleep(us);
        }

        ffmpeg_context_t* ffc = (ffmpeg_context_t *) handle_get(handle);
        if (ffc == NULL) {
            return;
        }

        us = chores(ffc, seekto, &reflost);
        seekto = (uint64_t) -1;
        if (us != 0) {
            handle_put(handle);
            if (us != (useconds_t) -1) {
                continue;
            }
            return;
        }

        struct my_buffer* mbuf = ff_buffer_alloc();
        if (mbuf == NULL) {
            handle_put(handle);
            continue;
        }

        ff_payload_t* payload = payload_of(mbuf);
        ffc->u.main.readtms = now();
        ffc->u.main.timeout = 0;
        int n = av_read_frame(ffc->fmtctx, &payload->packet);
        if (n < 0) {
            int err = -n;
            char* pch = (char *) &err;
            logmsg("read frame timeout: %d err: %d(%c%c%c%c)\n",
                   ffc->u.main.timeout, err, pch[0], pch[1], pch[2], pch[3]);

            if (ffc->u.main.timeout == 1 || n != AVERROR_EOF) {
                n = 3;
            } else {
                n = 1;
            }
            mbuf->mop->free(mbuf);

            lock(&ffc->lck);
            ffc->u.main.eof = n;
            if (ffc->fcontext.more != 0) {
                ffc->fcontext.more = 0;
                ffc->cb.notify(ffc->cb.uptr, context_update, &ffc->fcontext);
            }
            unlock(&ffc->lck);

            handle_put(handle);
            us = 1000 * 100;
            continue;
        }

        ffmpeg_stream_t* ffs = buffer_rebuild(handle, ffc, mbuf, &reflost);
        if (ffs == NULL) {
            mbuf->mop->free(mbuf);
        } else {
            stream_push(ffc, ffs, mbuf);
            stream_put(ffs);
        }
        handle_put(handle);
    }
}

static uint64_t ffmpeg_context_reinit(ffmpeg_context_t* ffc)
{
    my_assert(ffc->u.main.eof != 1);
    if (ffc->conf.eof_reconn) {
        ffc->fcontext.cursor = 0;
    } else if (ffc->u.main.eof == 2) {
        return (uint64_t) -1;
    }

    avformat_close_input(&ffc->fmtctx);
    ffc->fmtctx = avformat_alloc_context();
    // if no mem, then no reconnect any more
    if (ffc->fmtctx == NULL) {
        return (uint64_t) -1;
    }
    return ffc->fcontext.cursor;
}

static void* ffmpeg_file_loop(void* any)
{
    mark("thread start");
    intptr_t n;
    uint64_t seekto;
    my_handle* handle = (my_handle *) any;
    handle_clone(handle);

    ffmpeg_context_t* ffc = (ffmpeg_context_t *) handle_get(handle);
    if (ffc == NULL) {
        goto LABEL3;
    }

    char pathname[1024];
    if (strlen(ffc->u.pathname) >= sizeof(pathname)) {
        handle_put(handle);
        goto LABEL3;
    }

    strcpy(pathname, ffc->u.pathname);
    ffc->u.main.mediaid = media_id_uniq();
    ffc->u.main.ffs = NULL;
    INIT_LIST_HEAD(&ffc->u.main.head);
    ffc->u.main.zero = 0;
    intptr_t interrupt = 0;

LABEL1:
    ffc->fcontext.more = ffc->conf.latch[0];
    ffc->fmtctx->interrupt_callback.callback = av_net_callback;
    ffc->fmtctx->interrupt_callback.opaque = handle;
    ffc->u.main.readtms = now();
    ffc->u.main.timeout = 0;
    n = avformat_open_input(&ffc->fmtctx, pathname, NULL, NULL);
    if (n != 0) {
        if (interrupt == 1) {
            usleep(1000 * 50);
            goto LABEL2;
        }

        char msg[256];
        av_strerror((int) n, msg, sizeof(msg));
        logmsg("avformat_open_input failed: %s\n", msg);

        ffc->fcontext.cursor = ffc->fcontext.duration = 0;
        ffc->cb.notify(ffc->cb.uptr, context_update, &ffc->fcontext);
        handle_put(handle);
        goto LABEL3;
    }

    n = ffmpeg_read_header(handle, ffc);
    if (n < 0) {
        if (interrupt == 1) {
            usleep(1000 * 50);
            goto LABEL2;
        }

        ffc->fcontext.cursor = ffc->fcontext.duration = 0;
        ffc->cb.notify(ffc->cb.uptr, context_update, &ffc->fcontext);
        handle_put(handle);
        goto LABEL3;
    }

    seekto = (uint64_t) -1;
    if (ffc->seekto != 0) {
        if (interrupt == 0) {
            seekto = (uint64_t) ffc->seekto;
        }
        ffmpeg_seek(handle, ffc->seekto);
    } else {
        ffc->seekto = -1;
    }

    handle_put(handle);
    fetch_from_remote(handle, seekto);

    ffc = (ffmpeg_context_t *) handle_get(handle);
    if (ffc != NULL) {
LABEL2:
        ffc->seekto = ffmpeg_context_reinit(ffc);
        if (ffc->seekto != (uint64_t) -1) {
            interrupt = 1;
            goto LABEL1;
        }
        handle_put(handle);
    }

LABEL3:
    handle_release(handle);
    handle_dettach(handle);
    logmsg("ffmpeg_file_loop exit %p\n", handle);
    return NULL;
}

static void ffc_free(void* addr)
{
    ffmpeg_context_t* ffc = (ffmpeg_context_t *) addr;
    if (ffc->u.main.zero == 0) {
        if (ffc->u.main.ffs != NULL) {
            stream_release(ffc->u.main.ffs);
        }

        while (!list_empty(&ffc->u.main.head)) {
            struct list_head* ent = ffc->u.main.head.next;
            list_del(ent);
            ffmpeg_stream_t* ffs = list_entry(ent, ffmpeg_stream_t, entry);
            handle_dettach(ffs->self);
        }
    }

    if (ffc->fmtctx != NULL) {
        avformat_close_input(&ffc->fmtctx);
    }

    if (ffc->cb.uptr_put != NULL) {
        ffc->cb.uptr_put(ffc->cb.uptr);
    }

    if (ffc->joint != NULL) {
        file_jointor_close(ffc->joint);
    }
    my_free(addr);
}

static void* mediafile_open(void* any)
{
    media_struct_t* media = (media_struct_t *) any;
    any = media->any;
    media->any = no_stream;
    return any;
}

static int mediafile_read(void* id, struct list_head* headp, const fraction* frac)
{
    my_handle* handle = (my_handle *) id;
    ffmpeg_stream_t* ffs = (ffmpeg_stream_t *) handle_get(handle);
    if (ffs == NULL) {
        return -1;
    }

    if (frac->num == 0) {
        struct my_buffer* mbuf = mbuf_alloc_2(sizeof(media_buffer));
        if (mbuf != NULL) {
            mbuf->ptr[1] = mbuf->ptr[0] + mbuf->length;
            mbuf->length = 0;
            media_buffer* media = (media_buffer *) mbuf->ptr[0];
            *media = ffs->media;
            list_add(&mbuf->head, headp);
        }
        handle_put(handle);
        return 0;
    }

    if (ffs->file == NULL) {
        handle_put(handle);
        return -2;
    }

    ffmpeg_context_t* ffc = (ffmpeg_context_t *) handle_get(ffs->file);
    if (ffc == NULL) {
        handle_put(handle);
        return -1;
    }
    // we have got ffc
    // meaning no file_jointor_close has been called
    // so frac never be NULL
    my_assert(frac != NULL);

    int n = -2;
    if (ffc->fcontext.more == 0) {
        if (audio_type == media_type(*(ffs->media.pptr_cc))) {
            n = stream_read_audio(ffc, ffs, frac, headp);
        } else {
            n = stream_read_video(ffc, ffs, frac, headp);
        }
    }

    handle_put(ffs->file);
    handle_put(handle);
    return n;
}

static void mediafile_close(void* any)
{
    my_handle* handle = (my_handle *) any;
    handle_release(handle);
}

static media_operation_t mediafile_ops = {
    .open = mediafile_open,
    .read = mediafile_read,
    .close = mediafile_close
};

static void* ffmpeg_open(const char* uri, callback_t* cb, int64_t seekto, ffmpeg_file_config_t* conf)
{
    av_register_all();
    avformat_network_init();
    errno = EINVAL;
    if (cb == NULL || cb->notify == NULL || uri == NULL || strlen(uri) >= path_bytes) {
        return NULL;
    }

    errno = ENOMEM;
    ffmpeg_context_t* ffc = (ffmpeg_context_t *) my_malloc(sizeof(ffmpeg_context_t));
    if (ffc == NULL) {
        return NULL;
    }
    memset(ffc, 0, sizeof(ffmpeg_context_t));

    ffc->conf = *conf;
    ffc->lck = lock_val;
    ffc->seekto = seekto;
    ffc->cursor = (uint64_t) -1;

    ffc->fcontext.ftype = conf->magic;
    ffc->fcontext.duration = (uint64_t) -1;
    ffc->fcontext.more = ffc->conf.latch[0];
    strcpy(ffc->u.pathname, uri);

    my_handle* handle = handle_attach(ffc, ffc_free);
    if (handle == NULL) {
        my_free(ffc);
        return NULL;
    }

    errno = EFAILED;
    ffc->fmtctx = avformat_alloc_context();
    if (ffc->fmtctx == NULL) {
        handle_dettach(handle);
        return NULL;
    }

    media_struct_t mediafile;
    mediafile.ops = &mediafile_ops;
    handle_clone(handle);
    mediafile.any = handle;

    ffc->joint = file_jointor_open(&mediafile);
    if (ffc->joint == NULL) {
        ffc->cb.uptr_put = NULL;
        handle_release(handle);
        handle_dettach(handle);
        return NULL;
    }

    ffc->cb = *cb;
    handle_clone(handle);

    pthread_t tid;
    int n = pthread_create(&tid, NULL, ffmpeg_file_loop, handle);
    if (n != 0) {
        ffc->cb.uptr_put = NULL;
        handle_release(handle);
        handle_dettach(handle);
        return NULL;
    }
    pthread_detach(tid);

    return (void *) handle;
}

static fcontext_t* ffmpeg_fcontext_get(void* rptr)
{
    my_handle* handle = (my_handle *) rptr;
    ffmpeg_context_t* ffc = (ffmpeg_context_t *) handle_get(handle);
    if (ffc == NULL) {
        errno = EBADF;
        return NULL;
    }
    handle_clone(handle);
    return &ffc->fcontext;
}

static void ffmpeg_fcontext_put(void* rptr)
{
    my_handle* handle = (my_handle *) rptr;
    handle_put(handle);
    handle_release(handle);
}

static jointor* ffmpeg_jointor_get(void* rptr)
{
    my_handle* handle = (my_handle *) rptr;
    ffmpeg_context_t* ffc = (ffmpeg_context_t *) handle_get(handle);
    if (ffc == NULL) {
        errno = EBADF;
        return NULL;
    }

    jointor* joint = ffc->joint;
    int n = joint->ops->jointor_get(joint);
    my_assert(n > 0);
    handle_put(handle);
    return joint;
}

static void ffmpeg_close(void* rptr)
{
    handle_dettach((my_handle *) rptr);
}

static void* m3u8_open(const char* uri, callback_t* cb, file_mode_t* mode)
{
    ffmpeg_file_config_t conf;
    conf.magic = ftype_m3u8;
    conf.bsfc = NULL;
    conf.latch[0] = mode->latch[0];
    conf.latch[1] = mode->latch[1];
    conf.prefetch = my_min(30000, conf.latch[1] - 500);
    conf.prefetch = my_max(conf.prefetch, mode->latch[0]);
    conf.avsync = mode->sync;
    conf.eof_reconn = 0;
    conf.seekable = 1;

    if (mode->seekto == (uint64_t) -1) {
        conf.seekable = 0;
        mode->seekto = 0;
    }
    return ffmpeg_open(uri, cb, mode->seekto, &conf);
}

static fileops_t m3u8_ops = {
    .open = m3u8_open,
    .fcontext_get = ffmpeg_fcontext_get,
    .fcontext_put = ffmpeg_fcontext_put,
    .seek = ffmpeg_seek,
    .jointor_get = ffmpeg_jointor_get,
    .close = ffmpeg_close
};

filefmt_t m3u8_fmt = {
    .magic = ftype_m3u8,
    .ops = &m3u8_ops,
    .next = NULL,
};

static void* mp4_open(const char* uri, callback_t* cb, file_mode_t* mode)
{
    ffmpeg_file_config_t conf;
    conf.magic = ftype_mp4;
    conf.bsfc = h264_mp4toannexb;
    conf.latch[0] = mode->latch[0];
    conf.latch[1] = mode->latch[1];
    conf.prefetch = my_min(30000, conf.latch[1] - 500);
    conf.prefetch = my_max(conf.prefetch, mode->latch[0]);
    conf.avsync = mode->sync;
    conf.eof_reconn = 0;

    conf.seekable = 1;
    if (mode->seekto == (uint64_t) -1) {
        conf.seekable = 0;
        mode->seekto = 0;
    }
    return ffmpeg_open(uri, cb, mode->seekto, &conf);
}

static fileops_t mp4_ops = {
    .open = mp4_open,
    .fcontext_get = ffmpeg_fcontext_get,
    .fcontext_put = ffmpeg_fcontext_put,
    .seek = ffmpeg_seek,
    .jointor_get = ffmpeg_jointor_get,
    .close = ffmpeg_close
};

filefmt_t mp4_fmt = {
    .magic = ftype_mp4,
    .ops = &mp4_ops,
    .next = NULL
};

static void* rtmp_open(const char* uri, callback_t* cb, file_mode_t* mode)
{
    ffmpeg_file_config_t conf;
    conf.magic = ftype_rtmp;
    conf.bsfc = h264_mp4toannexb;
    conf.latch[0] = mode->latch[0];
    conf.latch[1] = mode->latch[1];
    conf.prefetch = (uintptr_t) -1;
    conf.avsync = mode->sync;
    conf.seekable = 0;
    conf.eof_reconn = 1;
    conf.seekable = 0;
    mode->seekto = 0;
    return ffmpeg_open(uri, cb, mode->seekto, &conf);
}

static fileops_t rtmp_ops = {
    .open = rtmp_open,
    .fcontext_get = ffmpeg_fcontext_get,
    .fcontext_put = ffmpeg_fcontext_put,
    .seek = ffmpeg_seek,
    .jointor_get = ffmpeg_jointor_get,
    .close = ffmpeg_close
};

filefmt_t rtmp_fmt = {
    .magic = ftype_rtmp,
    .ops = &rtmp_ops,
    .next = NULL
};
