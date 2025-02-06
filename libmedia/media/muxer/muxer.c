
#include "muxer.h"
#include "fmt.h"
#include "my_errno.h"
#include "mem.h"
#include "my_handle.h"
#include "lock.h"
#include "codec.h"
#include "now.h"
#include "mbuf.h"
#include <string.h>
#define max_streams 8

typedef struct {
    jointor* join;
    struct list_head joints;
    struct list_head remain;
    audio_format* fmt;
    void* decoder;
    uint32_t bytes;
} audio_item;

typedef struct {
    jointor* join;
    struct list_head joints;
    video_format* fmt;
    struct my_buffer* mbuf;
    void* decoder;
    int32_t delay;
    int32_t duration;
    uint64_t seq_next;
    uint64_t pts_old;
} video_item;

typedef struct {
    union {
        media_iden_t id;
        jointor* join;
    } u;
    void* mux;
    struct list_head entry;

    audio_item audio, *ap;
    video_item video, *vp;
    uint64_t pts[2];
    uintptr_t refs;
    rwlock_t glck;
} upitem;

typedef struct {
    lock_t lck[4];
    rwlock_t glck;
    int nr;
    jointor join;
    callback_t format_hook;

    struct list_head head;
    struct list_head pool;
    struct list_head unkown;
    struct list_head ups;
    struct list_head killed;
    jointor *downs;

    my_handle* self;
    int refs;
    int realtime;
} muxter;

static int jointor_get_1(jointor* join)
{
    muxter* mux = (muxter *) join->any;
    my_assert(mux->refs > 0);
    handle_clone(mux->self);
    int n = __sync_add_and_fetch(&mux->refs, 1);
    return n;
}

static int jointor_put_1(jointor* join)
{
    muxter* mux = (muxter *) join->any;
    int n = __sync_sub_and_fetch(&mux->refs, 1);
    if (n == 0) {
        handle_dettach(mux->self);
    } else {
        my_assert(n > 0);
        handle_release(mux->self);
    }
    return n;
}

static int jointor_get_2(jointor* join)
{
    handle_clone((my_handle *) join->any);
    int32_t* intp = (int32_t *) (join + 1);
    int n = __sync_add_and_fetch(intp, 1);
    return n;
}

static int jointor_put_2(jointor* join)
{
    int32_t* intp = (int32_t *) (join + 1);
    int n = __sync_sub_and_fetch(intp, 1);
    handle_release((my_handle *) join->any);
    if (n == 0) {
        my_free(join);
    }
    return n;
}

static inline upitem* find_upitem(muxter* mux, media_iden_t id)
{
    struct list_head* ent;
    for (ent = mux->head.next; ent != &mux->head; ent = ent->next) {
        upitem* upi = list_entry(ent, upitem, entry);
        if (media_id_eqal(upi->u.id, id)) {
            return upi;
        }
    }
    return NULL;
}

static inline void init_upitem_audio(upitem* upi, media_buffer* media, jointor* join)
{
    upi->ap = &upi->audio;
    upi->ap->join = join;
    upi->ap->fmt = to_audio_format(media->pptr_cc);
    upi->ap->decoder = NULL;
    INIT_LIST_HEAD(&upi->ap->remain);
}

static inline void init_upitem_video(upitem* upi, media_buffer* media, jointor* join, uint32_t current)
{
    upi->vp = &upi->video;
    upi->vp->join = join;
    upi->vp->fmt = to_video_format(media->pptr_cc);
    upi->vp->decoder = NULL;
    upi->pts[0] = media->pts;
    upi->pts[1] = current;
    upi->vp->delay = 0;
    upi->vp->duration = 40;
    upi->vp->pts_old = 0;
    upi->vp->seq_next = 0;

    struct my_buffer* mbuf = mbuf_alloc_2(sizeof(media_buffer));
    if (mbuf != NULL) {
        media_buffer* buffer = (media_buffer *) mbuf->ptr[0];
        *buffer = *media;
        memset(buffer->vp, 0, sizeof(buffer->vp));
        mbuf->ptr[1] += mbuf->length;
        mbuf->length = 0;
    }
    upi->vp->mbuf = mbuf;
}

static inline int32_t return_upitem_to_pool(muxter* mux, upitem* upi)
{
    upi->u.id = media_id_unkown;

    lock(&mux->lck[1]);
    list_add(&upi->entry, &mux->pool);
    int32_t nr = --mux->nr;
    unlock(&mux->lck[1]);
    return nr;
}

static void check_unkown_ups(muxter* mux, uint32_t current)
{
    struct list_head head1, head2, head3;
    INIT_LIST_HEAD(&head2);
    INIT_LIST_HEAD(&head3);

    lock(&mux->lck[1]);
    list_add(&head1, &mux->unkown);
    list_del_init(&mux->unkown);
    unlock(&mux->lck[1]);

    fraction frac;
    frac.num = 0;
    frac.den = 1;

    while (!list_empty(&head1)) {
        struct list_head* ent = head1.next;
        list_del(ent);

        upitem* upi = list_entry(ent, upitem, entry);
        jointor* join = upi->u.join;
        int n = join->ops->jointor_pull(join->any, &head2, &frac);
        if (n == -1) {
            join->ops->jointor_put(join);
            return_upitem_to_pool(mux, upi);
            continue;
        }

        if (list_empty(&head2)) {
            list_add(ent, &head3);
            continue;
        }

        my_assert(list_is_singular(&head2));
        ent = head2.next;
        list_del_init(&head2);

        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        int type = media_type(*media->pptr_cc);

        lock(&mux->lck[3]);
        upitem* found = find_upitem(mux, media->iden);
        logmsg_d("find_upitem %llu get %p\n", media->iden, found);
        if (found != NULL) {
            write_lock(&found->glck);
            if (type == audio_type) {
                if (found->ap == NULL) {
                    init_upitem_audio(found, media, join);
                } else {
                    list_add_tail(&join->entry[1], &found->ap->joints);
                }
            } else {
                if (found->vp == NULL) {
                    init_upitem_video(found, media, join, current);
                } else {
                    list_add_tail(&join->entry[1], &found->vp->joints);
                }
            }
            write_unlock(&found->glck);
            return_upitem_to_pool(mux, upi);
        } else {
            upi->u.id = media->iden;
            if (type == audio_type) {
                init_upitem_audio(upi, media, join);
            } else {
                init_upitem_video(upi, media, join, current);
            }
            list_add(&upi->entry, &mux->head);
        }
        unlock(&mux->lck[3]);
        mbuf->mop->free(mbuf);
    }

    if (!list_empty(&head3)) {
        lock(&mux->lck[1]);
        list_join(&head3, &mux->unkown);
        list_del(&head3);
        unlock(&mux->lck[1]);
    }
}

static uint32_t audio_transcoding(upitem* upi, audio_format* fmt, struct list_head* headp)
{
    muxter* mux = (muxter *) upi->mux;
    if (upi->ap->decoder == NULL) {
        upi->ap->decoder = codec_open(&upi->ap->fmt->cc, &fmt->cc, &mux->format_hook);
        if (upi->ap->decoder == NULL) {
            free_buffer(headp);
            return 0;
        }
    }

    uint32_t bytes = 0;
    while (!list_empty(headp)) {
        struct my_buffer* mbuf = list_entry(headp->next, struct my_buffer, head);
        list_del(headp->next);

        int32_t nb = codec_write(upi->ap->decoder, mbuf, &upi->ap->remain, NULL);
        if (nb == -1) {
            mbuf->mop->free(mbuf);
        } else {
            bytes += nb;
        }
    }
    return bytes;
}

static void drop_unnecessary_frames(struct list_head* headp)
{
    struct list_head* ent;
    int32_t keyframe = 0;
    for (ent = headp->prev; ent != headp; ent = ent->prev) {
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];

        uint32_t frametype = (media->frametype & 0xf);
        if ((keyframe == 0) && (video_type_idr == frametype)) {
            keyframe = 1;
        } else if ((keyframe == 1) && (video_type_idr != frametype)) {
            break;
        }
    }

    if (ent != headp) {
        ent = ent->next;
        list_split(headp, ent);

        struct my_buffer* mbuf = list_entry(headp->next, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->frametype & video_type_sps) {
            list_del(&mbuf->head);
        } else {
            mbuf = NULL;
        }

        free_buffer(headp);
        list_add_tail(headp, ent);
        if (mbuf != NULL) {
            list_add(&mbuf->head, headp);
        }
    }
}

static uint32_t video_transcoding(upitem* upi, video_format* fmt, struct list_head* headp)
{
    muxter* mux = (muxter *) upi->mux;

    if (upi->vp->decoder == NULL) {
        upi->vp->decoder = codec_open(&upi->vp->fmt->cc, &fmt->cc, &mux->format_hook);
        if (upi->vp->decoder == NULL) {
            free_buffer(headp);
            return 0;
        }
    }

    struct list_head head;
    INIT_LIST_HEAD(&head);

    if (mux->realtime) {
        drop_unnecessary_frames(headp);
    }

    uint32_t frames = 0;
    uint32_t stat[2] = {0};
    while (!list_empty(headp)) {
        struct my_buffer* mbuf = list_entry(headp->next, struct my_buffer, head);
        list_del(headp->next);

        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->fragment[0] == 0) {
            if (upi->vp->seq_next == media->seq && upi->vp->pts_old != 0) {
                stat[0] += media->pts - upi->vp->pts_old;
                stat[1] += 1;
            }
            upi->vp->seq_next = media->seq + media->fragment[1];
            upi->vp->pts_old = media->pts;
        }

        int32_t delay = 0;
        int32_t n = codec_write(upi->vp->decoder, mbuf, &head, &delay);
        if (n == -1) {
            mbuf->mop->free(mbuf);
        } else {
            frames += n;
            upi->vp->delay = delay;
        }
    }

    if (stat[1] > 0) {
        uint32_t duration = stat[0] / stat[1];
        if (upi->vp->duration != 0) {
            upi->vp->duration = (float) upi->vp->duration * 0.9 + (float) duration * 0.1;
        } else {
            upi->vp->duration = duration;
        }
    }

    list_add(headp, &head);
    list_del(&head);
    return frames;
}

static void free_joints(struct list_head* headp)
{
    while (!list_empty(headp)) {
        struct list_head* ent = headp->next;
        list_del_init(ent);
        jointor* join = list_entry(ent, jointor, entry);
        join->ops->jointor_put(join);
    }
}

static void clean_upstream_audio(upitem* upi)
{
    if (upi->ap != NULL) {
        upi->ap->fmt = NULL;
        if (upi->ap->join != NULL) {
            upi->ap->join->ops->jointor_put(upi->ap->join);
            upi->ap->join = NULL;
        }
        free_joints(&upi->ap->joints);

        if (upi->ap->decoder != NULL) {
            codec_close(upi->ap->decoder);
            upi->ap->decoder = NULL;
        }
        free_buffer(&upi->ap->remain);
        upi->ap->bytes = 0;
        upi->ap = NULL;
    }
}

static audio_format* audio_decode_to(audio_format* fmt)
{
    if (fmt->ops->raw_format != NULL) {
        return fmt->ops->raw_format(fmt);
    }
    return audio_raw_format(fmt->pcm);
}

static int32_t pull_upstream_audio(upitem* upi, struct my_buffer* mbuf)
{
    if (upi->ap == NULL) {
        return -1;
    }

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    if (media->pptr_cc == NULL) {
        audio_format* raw = audio_decode_to(upi->ap->fmt);
        media->pptr_cc = &raw->cc;
    }
    audio_format* fmt = to_audio_format(media->pptr_cc);
    my_assert(fmt == audio_raw_format(fmt->pcm));

    if (upi->ap->bytes >= mbuf->length) {
        return upi->ap->bytes;
    }

    fraction need = pcm_ms_from_bytes(fmt->pcm, (uint32_t) mbuf->length - upi->ap->bytes);
    if (upi->ap != NULL) {
        struct list_head head;
        INIT_LIST_HEAD(&head);
        int n = -1;
        if (upi->ap->join != NULL) {
LABEL:
            n = upi->ap->join->ops->jointor_pull(upi->ap->join->any, &head, &need);
            if (n == -1) {
                // if audio's remain does not empty, mbuf in remain
                // will be returned. meaning -1 can't be returned,
                // e.g. caller won't know and will call again for more data
                // release ap->join to avoid jointor_put being called again
                upi->ap->join->ops->jointor_put(upi->ap->join);
            }
        }

        if (n == -1) {
            if (!list_empty(&upi->ap->joints)) {
                struct list_head* ent = upi->ap->joints.next;
                list_del_init(ent);
                upi->ap->join = list_entry(ent, jointor, entry[1]);
                goto LABEL;
            }
            upi->ap->join = NULL;
        } else if (n > 0) {
            muxter* mux = (muxter *) upi->mux;
            if (media_same(&upi->ap->fmt->cc, &fmt->cc) && mux->format_hook.notify == NULL) {
                upi->ap->bytes += n;
            } else {
                upi->ap->bytes += audio_transcoding(upi, fmt, &head);
            }
            list_join(&head, &upi->ap->remain);
            list_del(&head);
        }
    }

    if (upi->ap->bytes > 0) {
        return upi->ap->bytes;
    }

    if (upi->ap->join == NULL) {
        clean_upstream_audio(upi);
        return -1;
    }
    return 0;
}

static void sync_pts(upitem* upi, uint32_t need, uint32_t audio_bytes, uint32_t current)
{
    struct list_head* ent = upi->ap->remain.next;
    my_assert(ent != &upi->ap->remain);
    struct my_buffer* mbuf = NULL;
    media_buffer* media = NULL;
    audio_format* fmt = NULL;

LABEL:
    mbuf = list_entry(ent, struct my_buffer, head);
    media = (media_buffer *) mbuf->ptr[0];
    fmt = to_audio_format(media->pptr_cc);
    my_assert(fmt == audio_raw_format(fmt->pcm));

    if (audio_bytes <= need || mbuf->length >= need) {
        audio_bytes = my_min(audio_bytes, need);
        fraction frac = pcm_ms_from_bytes(fmt->pcm, audio_bytes);
        media->pts += frac.num / frac.den;
        upi->pts[0] = media->pts;
        upi->pts[1] = current;
        return;
    }

    need -= mbuf->length;
    audio_bytes -= mbuf->length;
    ent = ent->next;
    my_assert(ent != &upi->ap->remain);
    goto LABEL;
}

static void clean_upstream_video(upitem* upi)
{
    if (upi->vp != NULL) {
        upi->vp->fmt = NULL;
        if (upi->vp->join != NULL) {
            upi->vp->join->ops->jointor_put(upi->vp->join);
            upi->vp->join = NULL;
        }
        free_joints(&upi->vp->joints);

        if (upi->vp->decoder != NULL) {
            codec_close(upi->vp->decoder);
            upi->vp->decoder = NULL;
        }
        upi->vp = NULL;
    }
}

static int32_t pull_upstream_video(upitem* upi, struct list_head* headp, uint32_t current)
{
    if (upi->vp == NULL) {
        return -1;
    }

    int n;
    fraction need;
    struct list_head head;
    INIT_LIST_HEAD(&head);
LABEL1:
    need.den = (upi->ap != NULL);
    need.num = (uint32_t) (upi->pts[0] + current - upi->pts[1]) + upi->vp->delay * upi->vp->duration;
    if (need.num == 0) {
        need.num = 1;
    }

LABEL2:
    n = upi->vp->join->ops->jointor_pull(upi->vp->join->any, &head, &need);
    if (n == -1) {
        if (!list_empty(&upi->vp->joints)) {
            upi->vp->join->ops->jointor_put(upi->vp->join);
            upi->vp->join = list_entry(upi->vp->joints.next, jointor, entry);
            list_del_init(upi->vp->joints.next);
            goto LABEL2;
        }

        if (upi->vp->mbuf != NULL) {
            list_add_tail(&upi->vp->mbuf->head, headp);
            upi->vp->mbuf = NULL;
        }
        clean_upstream_video(upi);
    } else if (n > 0) {
        if (__builtin_expect((!list_empty(&head)), 1)) {
            muxter* mux = (muxter *) upi->mux;
            video_format* fmt = upi->vp->fmt;
            video_format* raw = video_raw_format(fmt->pixel, csp_any);
            if (!media_same(&fmt->cc, &raw->cc) || mux->format_hook.notify != NULL) {
                n = video_transcoding(upi, raw, &head);
            }
            list_join(headp, &head);
            list_del(&head);
        } else if (upi->ap == NULL) {
            upi->pts[0] += n;
            upi->pts[1] = current;
            goto LABEL1;
        }
    } else if (n == -2) {
        upi->pts[1] = current;
    }
    return n;
}

static int32_t clean_upstream(muxter* mux, upitem* upi)
{
    if (upi->vp != NULL && upi->vp->mbuf != NULL) {
        list_add_tail(&upi->vp->mbuf->head, &mux->killed);
        upi->vp->mbuf = NULL;
    }
    clean_upstream_audio(upi);
    clean_upstream_video(upi);
    return return_upitem_to_pool(mux, upi);
}

static int copy_buffer(char* buf, int len, audio_item* adi)
{
    char* saved = buf;
    while (len > 0 && !list_empty(&adi->remain)) {
        struct list_head* ent = adi->remain.next;
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);

        int32_t bytes = my_min(len, (int32_t) mbuf->length);
        memcpy(buf, mbuf->ptr[1], bytes);
        mbuf->ptr[1] += bytes;
        mbuf->length -= bytes;
        len -= bytes;
        buf += bytes;

        if (mbuf->length == 0) {
            list_del(ent);
            mbuf->mop->free(mbuf);
        }
    }

    int bytes = (int) (buf - saved);
    adi->bytes -= bytes;
    return bytes;
}

static inline short resampled_next_sample(audio_item* adi)
{
    struct list_head* headp = &adi->remain;
    struct list_head* ent = headp->next;
    struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
    my_assert(mbuf->length ^ 1);

    short s = *(short *) mbuf->ptr[1];
    mbuf->length -= sizeof(short);
    adi->bytes -= sizeof(short);

    if (__builtin_expect(mbuf->length > 0, 1)) {
        mbuf->ptr[1] += sizeof(short);
    } else {
        list_del(ent);
        if (mbuf->mop != NULL && mbuf->mop->free) {
            mbuf->mop->free(mbuf);
        }
    }

    return s;
}

static int32_t mix(audio_item** ads, int32_t nr, struct my_buffer* mbuf)
{
    char* buf = mbuf->ptr[1];
    int len = (int) mbuf->length;
    if (nr == 1) {
        return copy_buffer(buf, len, ads[0]);
    }

    len /= 2;
    short* sbuf = (short *) buf;

    for (int i = 0; i < len; ++i) {
        int val = 0, k = 0;

        for (int j = 0; j < nr; ++j) {
            if (!list_empty(&ads[j]->remain)) {
                val += resampled_next_sample(ads[j]);
                k = 1;
            }
        }

        if (__builtin_expect(k == 0, 0)) {
            break;
        }

        if (val > 32767) {
            val = 32767;
        } else if (val < -32768) {
            val = -32768;
        }

        *sbuf = (short) val;
        sbuf++;
    }

    return (int32_t) ((intptr_t) sbuf - (intptr_t) buf);
}

static int killed_notify(muxter* mux, struct list_head* headp)
{
    int n = 0;
    struct list_head* ent;
    for (ent = mux->killed.next; ent != &mux->killed; ent = ent->next) {
        ++n;
    }

    if (n > 0) {
        list_join(&mux->killed, headp);
        list_del_init(&mux->killed);
    }
    return n;
}

static int jointor_pull(void* any, struct list_head* headp, const fraction* f)
{
    muxter* mux = (muxter *) handle_get((my_handle *) any);
    if (mux == NULL) {
        return -1;
    }
    uint32_t current = now();
    read_lock(&mux->glck);

    struct list_head *ent, *next;
    struct my_buffer* mbuf = NULL;
    int32_t audio_killed = 0;
    if (f != NULL) {
        my_assert(!list_empty(headp));
        mbuf = list_entry(headp->next, struct my_buffer, head);
        audio_killed = 1;
    } else {
        check_unkown_ups(mux, current);
    }

    int32_t idx = 0;
    audio_item* ads[max_streams];

    lock(&mux->lck[3]);
    intptr_t n, nr = mux->nr;
    for (ent = mux->head.next; ent != &mux->head; ent = next) {
        my_assert(nr > 0);
        upitem* upi = list_entry(ent, upitem, entry);
        ++upi->refs;
        unlock(&mux->lck[3]);

        read_lock(&upi->glck);
        if (mbuf != NULL) {
            n = pull_upstream_audio(upi, mbuf);
            if (n != -1) {
                if (n > 0) {
                    ads[idx++] = upi->ap;
                    sync_pts(upi, (uint32_t) mbuf->length, (int32_t) n, current);
                } else if (0) {
                    // pause video until !have_audio
                    upi->pts[1] = current;
                }
                audio_killed = 0;
            }
        } else {
            int nr = killed_notify(mux, headp);
            n = pull_upstream_video(upi, headp, current);
            if (nr > 0) {
                if (n == -1) {
                    n = 0;
                }
                n += nr;
            }
        }
        read_unlock(&upi->glck);

        lock(&mux->lck[3]);
        next = ent->next;
        --upi->refs;

        // check_unkown_ups maybe has add audio/video of upi
        // after pull_upstream failed with -1.
        // must recheck it to avoid eating the event.
        // notice we are holding lck[3] to prevent cocurrent
        // access to upi in check_unkown_ups
        if ((upi->refs == 0) &&
            (n == -1) &&
            (upi->ap == NULL) &&
            (upi->vp == NULL))
        {
            list_del(ent);
            nr = return_upitem_to_pool(mux, upi);
        }
    }
    unlock(&mux->lck[3]);

    int32_t bytes = 0;
    if (nr == 0) {
        my_assert(idx == 0);
        my_assert(audio_killed == 1 || mbuf == NULL);
        bytes = -1;
    } else if (idx > 0) {
        bytes = mix(ads, idx, mbuf);
    } else if (audio_killed == 1) {
        list_del_init(&mbuf->head);
    }
    read_unlock(&mux->glck);
    handle_put((my_handle *) any);
    return bytes;
}

static int notify_downs(muxter* mux)
{
    jointor* join = (jointor *) my_malloc(sizeof(jointor) + sizeof(int32_t));
    if (join == NULL) {
        errno = ENOMEM;
        return -1;
    }
    int32_t* intp = (int32_t *) (join + 1);
    intp[0] = 1;

    static struct jointor_ops upi_ops = {
        jointor_get_2, jointor_put_2, jointor_pull, NULL
    };

    handle_clone(mux->self);
    join->any = mux->self;
    join->ctx.extra = 0;
    join->ctx.stand = stand_mixer;
    join->ctx.identy = (intptr_t) mux;
    join->ops = &upi_ops;
    INIT_LIST_HEAD(&join->entry[0]);
    INIT_LIST_HEAD(&join->entry[1]);

    int n = mux->downs->ops->jointor_push(mux->downs->any, link_from_up, join);
    join->ops->jointor_put(join);
    return n;
}

static int link_downs(muxter* mux, jointor* jointor)
{
    int n = 0;
    if (jointor->ctx.stand != stand_join) {
        errno = EPERM;
        return -1;
    }

    lock(&mux->lck[0]);
    if (mux->downs == NULL) {
        mux->downs = jointor;
        mux->realtime = !!(jointor->ctx.extra & flag_realtime);
    } else {
        errno = EBUSY;
        n = -1;
        jointor = NULL;
    }
    unlock(&mux->lck[0]);

    if (jointor != NULL) {
        lock(&mux->lck[1]);
        if (mux->nr > 0) {
            n = notify_downs(mux);
        }
        unlock(&mux->lck[1]);

        if (n == 0) {
            jointor->ops->jointor_get(jointor);
        } else {
            lock(&mux->lck[0]);
            mux->downs = NULL;
            unlock(&mux->lck[0]);
        }
    }

    return n;
}

static int unlink_downs(muxter* mux, jointor* jointor)
{
    int n = 0;

    lock(&mux->lck[0]);
    if (mux->downs != jointor) {
        n = -1;
        errno = EINVAL;
        jointor = NULL;
    } else {
        mux->downs = NULL;
    }
    unlock(&mux->lck[0]);

    if (jointor != NULL) {
        jointor->ops->jointor_put(jointor);
    }
    return n;
}

static int link_ups(muxter* mux, jointor* jointer)
{
    if (jointer->ctx.stand != stand_file) {
        errno = EPERM;
        return -1;
    }

    lock(&mux->lck[1]);
    if (list_empty(&mux->pool)) {
        unlock(&mux->lck[1]);
        errno = EBUSY;
        return -1;
    }

    int n = 0;
    ++mux->nr;
    if (mux->nr == 1) {
        lock(&mux->lck[0]);
        if (mux->downs != NULL) {
            n = notify_downs(mux);
        }
        unlock(&mux->lck[0]);

        if (n == -1) {
            mux->nr = 0;
            unlock(&mux->lck[1]);
            return -1;
        }
    }

    struct list_head* ent = mux->pool.next;
    upitem* upi = list_entry(ent, upitem, entry);
    upi->u.join = jointer;
    list_del(ent);
    list_add(&upi->entry, &mux->unkown);
    unlock(&mux->lck[1]);

    jointer->ops->jointor_get(jointer);
    return 0;
}

static int jointor_push(void* any, int cmd, void* data)
{
    int n = 0;
    muxter* mux = (muxter *) any;
    jointor* jointer = (jointor *) data;

    if (cmd == link_from_down) {
        n = link_downs(mux, jointer);
    } else if (cmd == unlink_from_down) {
        n = unlink_downs(mux, jointer);
    } else if (cmd == link_from_up) {
        n = link_ups(mux, jointer);
    } else {
        errno = ENOSYS;
        n = -1;
    }
    return n;
}

static void muxter_delete(void* addr)
{
    muxter* mux = (muxter *) addr;

    while (!list_empty(&mux->head)) {
        struct list_head* ent = mux->head.next;
        list_del(ent);
        upitem* upi = list_entry(ent, upitem, entry);
        clean_upstream(mux, upi);
    }

    while (!list_empty(&mux->unkown)) {
        struct list_head* ent = mux->unkown.next;
        list_del(ent);
        upitem* upi = list_entry(ent, upitem, entry);
        clean_upstream(mux, upi);
    }

    free_buffer(&mux->killed);
    my_free(mux);
}

jointor* muxter_create(uint32_t nr, callback_t* format_hook)
{
    my_assert(nr > 0);
    errno = ENOMEM;
    if (nr > max_streams) {
        nr = max_streams;
    }

    muxter* mux = (muxter *) my_malloc(sizeof(muxter) + nr * sizeof(upitem));
    upitem* upi = (upitem *) (mux + 1);
    if (mux == NULL) {
        return NULL;
    }

    mux->self = handle_attach(mux, muxter_delete);
    if (mux->self == NULL) {
        my_free(mux);
        return NULL;
    }

    mux->lck[0] = mux->lck[1] = lock_val;
    mux->lck[2] = mux->lck[3] = lock_val;
    mux->glck = rw_lock_val;
    mux->refs = 1;
    mux->nr = 0;
    mux->downs = NULL;
    mux->realtime = 0;

    if (format_hook != NULL) {
        mux->format_hook = *format_hook;
    } else {
        memset(&mux->format_hook, 0, sizeof(callback_t));
    }

    INIT_LIST_HEAD(&mux->head);
    INIT_LIST_HEAD(&mux->pool);
    INIT_LIST_HEAD(&mux->unkown);
    INIT_LIST_HEAD(&mux->ups);
    INIT_LIST_HEAD(&mux->killed);

    for (int i = 0; i < nr; ++i) {
        upi[i].u.join = NULL;
        upi[i].mux = mux;
        upi[i].ap = NULL;
        upi[i].vp = NULL;
        upi[i].refs = 0;
        upi[i].glck = rw_lock_val;
        upi[i].pts[0] = upi[i].pts[1] = 0;
        list_add(&upi[i].entry, &mux->pool);

        INIT_LIST_HEAD(&upi[i].audio.joints);
        upi[i].audio.fmt = NULL;
        upi[i].audio.decoder = NULL;
        upi[i].audio.bytes = 0;
        INIT_LIST_HEAD(&upi[i].audio.remain);

        INIT_LIST_HEAD(&upi[i].video.joints);
        upi[i].video.delay = 0;
        upi[i].video.duration = 40;
        upi[i].video.seq_next = 0;
        upi[i].video.pts_old = 0;
        upi[i].video.fmt = NULL;
        upi[i].video.decoder = NULL;
        upi[i].video.mbuf = NULL;
    }

    static struct jointor_ops muxter_ops = {
        jointor_get_1, jointor_put_1, NULL, jointor_push
    };
    mux->join.any = mux;
    mux->join.ctx.stand = stand_join;
    mux->join.ctx.extra = 0;
    mux->join.ctx.identy = (intptr_t) mux;
    mux->join.ops = &muxter_ops;
    INIT_LIST_HEAD(&mux->join.entry[0]);
    INIT_LIST_HEAD(&mux->join.entry[1]);
    return &mux->join;
}

int muxter_link_upstream(jointor* self, jointor* upstream)
{
    if (upstream->ops->jointor_push == NULL || self->ops->jointor_push == NULL) {
        errno = EINVAL;
        return -1;
    }

    muxter* mux = (muxter *) self->any;
    if (list_empty(&upstream->entry[0])) {
        lock(&mux->lck[2]);
        list_add(&upstream->entry[0], &mux->ups);
        unlock(&mux->lck[2]);
    } else {
        upstream = NULL;
    }

    if (upstream == NULL) {
        errno = EBUSY;
        return -1;
    }

    int n = upstream->ops->jointor_push(upstream->any, link_from_down, self);
    if (n == -1) {
        lock(&mux->lck[2]);
        list_del_init(&upstream->entry[0]);
        unlock(&mux->lck[2]);
    } else {
        upstream->ops->jointor_get(upstream);
    }
    return n;
}

static inline intptr_t identy(upitem* upi)
{
    jointor* join = NULL;
    if (upi->ap != NULL) {
        join = upi->ap->join;
    }

    if (join == NULL && upi->vp != NULL) {
        join = upi->vp->join;
    }

    if (join == NULL) {
        return (intptr_t) NULL;
    }
    return join->ctx.identy;
}

void muxter_unlink_upstream(jointor* self, jointor* upstream)
{
    struct list_head* ent;
    muxter* mux = (muxter *) self->any;
    lock(&mux->lck[2]);
    for (ent = mux->ups.next; ent != &mux->ups; ent = ent->next) {
        jointor* ups = list_entry(ent, jointor, entry);
        if (ups == upstream) {
            list_del_init(ent);
            break;
        }
    }
    unlock(&mux->lck[2]);

    if (ent == &mux->ups) {
        return;
    }

    upstream->ops->jointor_push(upstream->any, unlink_from_down, self);
    upstream->ops->jointor_put(upstream);

    write_lock(&mux->glck);
    struct list_head* next;
    for (ent = mux->unkown.next; ent != &mux->unkown; ent = next) {
        upitem* upi = list_entry(ent, upitem, entry);
        next = ent->next;
        if (upi->u.join->ctx.identy == upstream->ctx.identy) {
            list_del(ent);
            upi->u.join->ops->jointor_put(upi->u.join);
            return_upitem_to_pool(mux, upi);
        }
    }

    for (ent = mux->head.next; ent != &mux->head; ent = next) {
        upitem* upi = list_entry(ent, upitem, entry);
        next = ent->next;

        intptr_t iden = identy(upi);
        if (iden == upstream->ctx.identy) {
            list_del(ent);
            clean_upstream(mux, upi);
        }
    }
    write_unlock(&mux->glck);
}
