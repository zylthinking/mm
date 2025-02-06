
#include "sortcache.h"
#include "list_head.h"
#include "mem.h"
#include "my_errno.h"
#include "string.h"
#include "lock.h"
#include "now.h"
#include "mbuf.h"

typedef struct {
    fraction capacity;
    struct list_head head;
    uint64_t seq;
} repos;

typedef struct {
    uint32_t tick;
    uint32_t nr;
    uint64_t seq;
} seqlost;

typedef struct {
    uint16_t reached;
    uint16_t total;
    uint64_t seq;
    uint64_t gopseq;
} frame_t;

typedef struct {
    uint32_t size_ms;
    uint32_t size_nr;
    uint64_t pts;
    uint64_t refseq;
    uint64_t gopseq;

    uint64_t gops[32];
    uintptr_t nr;
    frame_t* frames;
} video_block_t;

typedef struct {
    uint32_t latch[2];
    uint64_t next_seq;
    uint64_t next_pts;
    fraction not_feed;
    fraction packet_ms;
} audio_block_t;

typedef struct {
    struct list_head entry;
    media_buffer media;

    lock_t lck;
    uint32_t nr, eof;
    uint64_t seq[2];
    uint64_t indx[2];

    struct my_buffer** wait_list;
    struct list_head timer_head;

    struct my_buffer** lost_list;
    struct list_head lost_head, *pos;
    intptr_t hbuf;
    uint32_t count;
    uint32_t shrink_time;

    repos repos;
    cache_probe probe;
    push_retval retval;

    audio_block_t ablk;
    video_block_t vblk;
    uint32_t ready;
} media_cache;

typedef enum {continuous, expired, overflow} flush_mode_t;
#define real(x) ((uint32_t) (x & 0xffffffff))
#define seqbase(x) ((uint32_t) (x >> 32))
#define overload_ttl(ms) (ms * 9)

#define cache_type_video(cache) (video_type == media_type(*cache->media.pptr_cc))
#define cache_type_audio(cache) (audio_type == media_type(*cache->media.pptr_cc))
#define gop_begin(type) (video_type_idr == type || video_type_sps == type)
#define reflost(media, vblk)  \
( \
    (offset_field(media) == reflost_flag) || \
    (offset_field(media) != 0 && media->seq - offset_field(media) != vblk.refseq) \
)

static inline fraction media_capacity(media_cache* cache, media_buffer* media)
{
    fraction frac;
    frac.den = 1;
    if (cache_type_video(cache)) {
        frac.num = media->fragment[1];
        return frac;
    }
    return cache->ablk.packet_ms;
}

static void cache_destroy(void* addr)
{
    media_cache* cache = (media_cache *) addr;
    for (uint32_t i = 0; i < cache->nr; ++i) {
        if (cache->lost_list[i] != NULL) {
            cache->lost_list[i]->mop->free(cache->lost_list[i]);
        }

        if (cache->wait_list[i] != NULL) {
            cache->wait_list[i]->mop->free(cache->wait_list[i]);
        }
    }

    if (cache->nr != 0) {
        my_free(cache->wait_list);
        my_free(cache->lost_list);
    }

    if (cache->vblk.frames != NULL) {
        my_free(cache->vblk.frames);
    }

    if (cache->hbuf) {
        mbuf_reap(cache->hbuf);
    }

    free_buffer(&cache->repos.head);
    my_free(cache);
}

my_handle* cache_create(uint32_t latch, media_buffer* media, uint32_t audio_frames)
{
    errno = ENOMEM;
    media_cache* cache = (media_cache *) my_malloc(sizeof(media_cache));
    if (cache == NULL) {
        return NULL;
    }

    my_handle* handle = handle_attach(cache, cache_destroy);
    if (handle == NULL) {
        my_free(cache);
        return NULL;
    }

    INIT_LIST_HEAD(&cache->entry);
    INIT_LIST_HEAD(&cache->timer_head);
    INIT_LIST_HEAD(&cache->lost_head);
    cache->lck = lock_val;
    cache->media = *media;
    cache->seq[0] = cache->seq[1] = media->seq - 1;
    cache->nr = cache->eof = 0;
    cache->indx[0] = cache->indx[1] = media->seq;
    cache->wait_list = NULL;
    cache->lost_list = NULL;
    cache->pos = &cache->lost_head;
    cache->hbuf = mbuf_hget(sizeof(seqlost), 50, 0);
    cache->count = 3;
    cache->shrink_time = (uint32_t) -1;

    fraction_zero(&cache->ablk.not_feed);
    fraction_zero(&cache->ablk.packet_ms);
    cache->ablk.next_pts = (uint64_t) (-1);
    cache->ablk.next_seq = 0;
    cache->ablk.latch[0] = now();
    cache->ablk.latch[1] = latch;

    cache->vblk.refseq = 0;
    cache->vblk.gopseq = 0;
    cache->vblk.frames = NULL;
    cache->vblk.size_ms = latch;
    cache->vblk.size_nr = 0;
    cache->vblk.pts = 0;
    cache->vblk.nr = 0;

    fraction_zero(&cache->repos.capacity);
    INIT_LIST_HEAD(&cache->repos.head);
    cache->repos.seq = 0;
    memset(&cache->probe, 0, sizeof(cache_probe));
    cache->probe.aud0 = cache_type_video(cache);

    if (cache->probe.aud0 == 0) {
        cache->ready = 1;
        audio_format* fmt = to_audio_format(media->pptr_cc);
        cache->ablk.packet_ms = fmt->ops->ms_from(fmt, audio_frames, 1);
    } else {
        cache->ready = 0;
    }

    return handle;
}

void cache_set_resend_count(my_handle* handle, uint32_t nr)
{
    if (nr == 0) {
        nr = 1;
    }

    media_cache* cache = (media_cache *) handle_get(handle);
    if (cache == NULL) {
        return;
    }

    cache->count = nr;
    handle_put(handle);
}

cache_probe* cache_probe_get(my_handle* handle)
{
    media_cache* cache = (media_cache *) handle_get(handle);
    if (cache == NULL) {
        return NULL;
    }

    cache_probe* p = &cache->probe;
    handle_put(handle);
    return p;
}

static struct my_buffer** realloc_buffer_list(uint32_t nr)
{
    struct my_buffer** mbufp = (struct my_buffer **) my_malloc(sizeof(struct my_buffer *) * nr);
    if (mbufp == NULL) {
        return NULL;
    }
    memset(mbufp, 0, sizeof(struct my_buffer *) * nr);
    return mbufp;
}

static int32_t realloc_wait_list(media_cache* cache, media_buffer* media)
{
    uint32_t nr, ms = 2400;
    if (cache_type_video(cache)) {
        if (media->frametype != video_type_sps) {
            errno = EINVAL;
            return -1;
        }

        uint32_t fps = media->fragment[0];
        uint32_t gop = offset_field(media);
        uint32_t gop_mtus = media->fragment[1];
        offset_field(media) = 0;
        media->fragment[0] = 0;
        media->fragment[1] = 1;

        logmsg("gop_mtus = %u\n", gop_mtus);
        cache->vblk.size_nr = (cache->vblk.size_ms * gop_mtus * fps) / (1000 * gop);
        nr = (ms * gop_mtus * fps) / (1000 * gop);
    } else {
        fraction frac = media_capacity(cache, media);
        nr = frac.den * ms / frac.num;
    }
    my_assert(nr > 0);

    errno = ENOMEM;
    cache->lost_list = realloc_buffer_list(nr);
    if (cache->lost_list == NULL) {
        return -1;
    }

    cache->wait_list = realloc_buffer_list(nr);
    if (cache->wait_list == NULL) {
        my_free(cache->lost_list);
        cache->lost_list = NULL;
        return -1;
    }

    if (cache_type_video(cache)) {
        cache->vblk.frames = (frame_t *) my_malloc(nr * sizeof(frame_t));
        if (cache->vblk.frames == NULL) {
            my_free(cache->wait_list);
            my_free(cache->lost_list);
            cache->wait_list = NULL;
            cache->lost_list = NULL;
            return -1;
        }
        memset(cache->vblk.frames, 0, nr * sizeof(frame_t));
    }

    cache->nr = nr;
    return 0;
}

static void check_sequence(const char* msg, media_cache* cache, struct list_head* headp)
{
#if 0
    uint64_t seq = 0, seq1 = 0;
    struct list_head* ent;
    logmsg("%s", msg);

    for (ent = headp->next; ent != headp; ent = ent->next) {
        struct my_buffer* buf = list_entry(ent, struct my_buffer, head);
        media_buffer* cur = (media_buffer *) buf->ptr[0];
        my_assert(cur->seq >= seq);
        seq = cur->seq;
        if (seq1 == 0) {
            seq1 = seq;
        }
    }
    logmsg("from %llu to %llu\n", seq1, seq);
#endif
}

static void add_to_repos(struct my_buffer* mbuf, media_cache* cache)
{
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    if (media->seq <= cache->seq[0]) {
        cache->probe.late++;
        mbuf->mop->free(mbuf);
        return;
    }
    fraction frac = media_capacity(cache, media);

    lock(&cache->lck);
    struct list_head* ent = cache->repos.head.prev;
    for (; ent != &cache->repos.head; ent = ent->prev) {
        struct my_buffer* buf = list_entry(ent, struct my_buffer, head);
        media_buffer* cur = (media_buffer *) buf->ptr[0];
        if (media->seq == cur->seq) {
            cache->probe.dup++;
            mbuf->mop->free(mbuf);
            unlock(&cache->lck);
            return;
        }

        if (media->seq > cur->seq) {
            break;
        }
    }

    list_add(&mbuf->head, ent);
    check_sequence("check add_to_repos: ", cache, &cache->repos.head);
    fraction_add(&cache->repos.capacity, &frac);
    unlock(&cache->lck);
    cache->probe.recv++;
}

static void add_lost(media_cache* cache, uint64_t from, uint64_t to, uint32_t current, uint16_t* info)
{
    uint64_t n = to - info[0];

    for (uint64_t idx = from; idx < to; ++idx) {
        uint32_t i = (uint32_t) (idx % cache->nr);
        my_assert(cache->lost_list[i] == NULL);
        struct my_buffer* mbuf = NULL;

        if (cache->hbuf == -1) {
            cache->hbuf = mbuf_hget(sizeof(seqlost), 50, 0);
        }

        if (cache->hbuf != -1) {
            mbuf = mbuf_alloc_1(cache->hbuf);
        } else {
            mbuf = mbuf_alloc_3(sizeof(seqlost));
        }

        if (mbuf == NULL) {
            continue;
        }

        seqlost* seql = (seqlost *) mbuf->ptr[1];
        if (idx < n) {
            seql->tick = current + 10;
        } else {
            seql->tick = current + info[1];
        }

        seql->nr = cache->count;
        seql->seq = idx;
        list_add_tail(&mbuf->head, &cache->lost_head);
        cache->lost_list[i] = mbuf;

        if (cache->pos == &cache->lost_head) {
            cache->pos = cache->lost_head.next;
        }
    }
}

static void del_lost(media_cache* cache, uint64_t from, uint64_t to, intptr_t arrive)
{
    for (uint64_t idx = from; idx < to; ++idx) {
        uint64_t i = idx % cache->nr;
        struct my_buffer* mbuf = cache->lost_list[i];

        if (mbuf == NULL) {
            continue;
        }

        struct list_head* ent = &mbuf->head;
        list_del(ent);
#if 0
        seqlost* seql = (seqlost *) mbuf->ptr[1];
        if (arrive == 0) {
            logmsg_d("seq %u lost after %d resend\n", real(idx), cache->count - seql->nr);
        } else if (cache->count - seql->nr > 0) {
            logmsg_d("seq %u arrive after %d resend\n", real(idx), cache->count - seql->nr);
        }
#endif

        if (cache->pos == ent) {
            cache->pos = ent->next;
            if (cache->pos == &cache->lost_head) {
                cache->pos = cache->lost_head.next;
            }
        }
        mbuf->mop->free(mbuf);
        cache->lost_list[i] = NULL;
    }
}

static uint32_t get_lost(media_cache* cache, uint32_t current, uint32_t rtt)
{
    struct my_buffer* mbuf = NULL;
    seqlost* seql = NULL;
    struct list_head* ent;

    for (ent = cache->pos; ent != &cache->lost_head;) {
        mbuf = list_entry(ent, struct my_buffer, head);
        ent = ent->next;

        seql = (seqlost *) mbuf->ptr[1];
        if (seql->tick > current) {
            continue;
        }

        if (cache_type_video(cache) &&
            seql->seq < cache->vblk.gopseq)
        {
            continue;
        }
        goto LABEL;
    }

    for (ent = cache->lost_head.next; ent != cache->pos;) {
        mbuf = list_entry(ent, struct my_buffer, head);
        ent = ent->next;

        seql = (seqlost *) mbuf->ptr[1];
        if (seql->tick > current) {
            continue;
        }

        if (cache_type_video(cache) &&
            seql->seq < cache->vblk.gopseq)
        {
            continue;
        }
        goto LABEL;
    }

    return (uint32_t) -1;
LABEL:
    --seql->nr;
    uint32_t seq = real(seql->seq);
    if (seql->nr == 0) {
        list_del(&mbuf->head);
        uint32_t i = (uint32_t) (seql->seq % cache->nr);
        my_assert(cache->lost_list[i] == mbuf);
        cache->lost_list[i] = NULL;
        mbuf->mop->free(mbuf);
    } else {
        seql->tick = current + rtt;
    }

    cache->pos = ent;
    if (cache->pos == &cache->lost_head) {
        cache->pos = cache->lost_head.next;
    }
    return seq;
}

static void check_list(media_cache* cache, uint64_t from, uint64_t to)
{
#if 0
    if (to > from + cache->nr) {
        to = from + cache->nr;
    }

    for (; from < to; ++from) {
        uint32_t idx = (uint32_t) (from % cache->nr);
        my_assert(cache->wait_list[idx] == NULL);
    }
#endif
}

static inline uint16_t unlink_from_waitlist(struct list_head* headp, media_cache* cache, uint32_t idx, uint16_t nr)
{
    uint16_t n = 0;
    uint32_t idx2;

    for (int16_t i = 0; i < nr; ++i, idx = idx2) {
        idx2 = (idx + 1) % cache->nr;
        struct my_buffer* mbuf = cache->wait_list[idx];
        if (mbuf == NULL) {
            continue;
        }

        ++n;
        cache->wait_list[idx] = NULL;
        list_del(&mbuf->head);
        list_add_tail(&mbuf->head, headp);
    }
    return n;
}

static void gop_push(uint64_t seq, video_block_t* vblk)
{
    my_assert(vblk->nr < elements(vblk->gops));
    if (vblk->nr == 0) {
        vblk->gops[vblk->nr] = seq;
        ++vblk->nr;
        goto LABEL;
    }
    my_assert(seq >= vblk->gops[0]);

    if (__builtin_expect(vblk->gops[vblk->nr - 1] == seq, 1)) {
        return;
    }

    if (seq > vblk->gops[vblk->nr - 1]) {
        vblk->gops[vblk->nr] = seq;
        ++vblk->nr;
        goto LABEL;
    }

    if (vblk->nr == 1) {
        return;
    }
    my_assert(vblk->gopseq != (uint64_t) -1);

    intptr_t i = vblk->nr - 2;
    for (; i >= 0; --i) {
        if (vblk->gops[i] == seq) {
            return;
        }

        if (vblk->gops[i] < seq) {
            break;
        }
    }
    my_assert(i >= 0);

    memmove(&vblk->gops[i + 2], &vblk->gops[i + 1], (vblk->nr - i - 1) * sizeof(uint64_t));
    vblk->gops[i + 1] = seq;
    ++vblk->nr;
LABEL:
    if (vblk->gopseq > seq) {
        vblk->gopseq = seq;
    }
}

static void gop_pop(uint64_t seq, video_block_t* vblk)
{
    intptr_t i = 0;
    for (; i < vblk->nr; ++i) {
        if (vblk->gops[i] == seq) {
            break;
        }
    }

    my_assert(i != vblk->nr);
    if (i > 0) {
        memmove(&vblk->gops[0], &vblk->gops[i], (vblk->nr - i) * sizeof(uint64_t));
        vblk->nr -= i;
    }
}

static uint64_t gop_next(uint64_t seq, video_block_t* vblk)
{
    intptr_t i = 0;
    for (; i < vblk->nr; ++i) {
        if (vblk->gops[i] == seq) {
            break;
        }
    }
    my_assert(i < vblk->nr);

    if (i == vblk->nr - 1) {
        return (uint64_t) -1;
    }
    return vblk->gops[i];
}

static inline uint16_t move_item_to_pool(uint32_t idx, media_cache* cache, repos* pool, frame_t* frame)
{
    struct list_head head, *headp = &head;
    INIT_LIST_HEAD(headp);
    frame_t audio_frame = {1, 1};
    if (frame == NULL) {
        frame = &audio_frame;
    }

    uint16_t n = unlink_from_waitlist(headp, cache, idx, frame->total);
    my_assert(n > 0);
    if (frame != &audio_frame) {
        my_assert2(n == frame->reached,
                   "frame->total = %d, reached = %d, seq = %llu, gopseq = %llu, n = %llu, idx = %u",
                   frame->total, frame->reached, frame->seq, frame->gopseq, n, idx);
        gop_pop(frame->gopseq, &cache->vblk);
    }

    if (pool != NULL) {
        my_assert(n == frame->total);
        struct my_buffer* buf = list_entry(headp->prev, struct my_buffer, head);
        media_buffer* media = (media_buffer *) buf->ptr[0];
        fraction frac = media_capacity(cache, media);
        fraction_add(&pool->capacity, &frac);
        list_join(&pool->head, headp);
        list_del(headp);
    } else {
        uint32_t nr = free_buffer(headp);
        logmsg_d("drop %d slices, from %d to %d\n", nr, cache->probe.drop, cache->probe.drop + nr);
        cache->probe.drop += nr;
    }

    frame->seq = frame->reached = frame->total = 0;
    return n;
}

static inline frame_t* video_frame_get(media_cache* cache, media_buffer* media)
{
    video_block_t* vblk = &cache->vblk;
    uint32_t gidx = media->fragment[0];
    uint64_t seq = media->seq - gidx;
    if (seq <= cache->seq[1]) {
        mark("seq %llu <= maxout %llu", seq, cache->seq[1]);
        return NULL;
    }

    jitter_info_t* jitter_info = (jitter_info_t *) ((uintptr_t) media - sizeof(jitter_info_t));
    uint32_t idx = (uint32_t) (seq % cache->nr);
    frame_t* frame = &vblk->frames[idx];
    if (frame->total == 0) {
        my_assert(frame->reached == 0);
        my_assert(frame->seq == 0);
        frame->seq = media->seq - gidx;
        frame->total = media->fragment[1];
        frame->gopseq = media->seq - jitter_info->gopidx;
        gop_push(frame->gopseq, vblk);
    } else if (frame->seq <= cache->seq[1]) {
        frame->reached = 0;
        frame->total = media->fragment[1];
        frame->seq = media->seq - gidx;
        frame->gopseq = media->seq - jitter_info->gopidx;
        gop_push(frame->gopseq, vblk);
    } else {
        my_assert2(frame->reached > 0,
                   "frame->total = %d, reached = %d, seq = %llu, gopseq = %llu, frag[1] = %llu, seq = %llu",
                   frame->total, frame->reached, frame->seq, frame->gopseq, media->seq, media->seq);
        my_assert2(frame->seq == media->seq - gidx,
                   "frame->total = %d, reached = %d, seq = %llu, gopseq = %llu, frag[1] = %llu, seq = %llu",
                   frame->total, frame->reached, frame->seq, frame->gopseq, media->seq, media->seq);
        my_assert2(frame->total == media->fragment[1],
                   "frame->total = %d, reached = %d, seq = %llu, gopseq = %llu, frag[1] = %llu, seq = %llu",
                   frame->total, frame->reached, frame->seq, frame->gopseq, media->seq, media->seq);
        my_assert2(frame->gopseq == media->seq - jitter_info->gopidx,
                   "frame->total = %d, reached = %d, seq = %llu, gopseq = %llu, frag[1] = %llu, seq = %llu",
                   frame->total, frame->reached, frame->seq, frame->gopseq, media->seq, media->seq);
    }
    return frame;
}

static uint32_t frame_flush(uint32_t idx, media_cache* cache, repos* pool, flush_mode_t mode)
{
    uint32_t flushed = 1;
    struct my_buffer* mbuf = cache->wait_list[idx];
    media_buffer* media = (media_buffer *) mbuf->ptr[0];

    frame_t* frame = NULL;
    if (cache_type_audio(cache)) {
        goto LABEL;
    }

    frame = video_frame_get(cache, media);
    if (frame == NULL) {
        pool = NULL;
        goto LABEL;
    }
    my_assert(frame->reached > 0);

    uintptr_t must = (mode == overflow);
    if (mode == expired) {
        must = (media->pts < cache->vblk.pts + 160);
    }

    if ((must == 0) &&
        (frame->reached != frame->total || reflost(media, cache->vblk)))
    {
        return 0;
    }

    uint32_t frame_type = media->frametype;
    uint32_t gidx = media->fragment[0];
    if (frame->reached == frame->total) {
        my_assert(gidx == 0);

        if (gop_begin(frame_type)) {
            cache->vblk.refseq = media->seq;
            cache->vblk.gopseq = frame->gopseq;
        } else if (reflost(media, cache->vblk)) {
            if (video_type_i != frame_type) {
                logmsg_d("mode %d, pts = %llu:%llu, %u reflost, fragments = %d\n",
                         (int) mode, cache->vblk.pts, media->pts, real(frame->seq), frame->total);
                pool = NULL;
            }
        } else {
            cache->vblk.gopseq = frame->gopseq;
            if (reference_able(frame_type)) {
                cache->vblk.refseq = media->seq - gidx;
            }
        }
    } else {
        logmsg_d("mode %d, vblk.pts = %llu:%llu, %u not full: %u/%u\n",
                 (int) mode, cache->vblk.pts, media->pts, real(frame->seq), frame->reached, frame->total);
        if (reference_able(frame_type) && cache->vblk.gopseq <= frame->gopseq) {
            cache->vblk.gopseq = gop_next(frame->gopseq, &cache->vblk);
        }
        frame->total -= gidx;
        pool = NULL;
    }

    flushed = frame->total;
LABEL:
    move_item_to_pool(idx, cache, pool, frame);
    return flushed;
}

static void flush_continuous(media_cache* cache, repos* pool)
{
    uint64_t from = cache->indx[0];
    uint64_t to = cache->indx[1];

    uint32_t i, flushed = 0;
    for (i = 0; from < to; i += flushed, from += flushed) {
        uint32_t idx = (uint32_t) (from % cache->nr);
        if (cache->wait_list[idx] == NULL) {
            break;
        }

        flushed = frame_flush(idx, cache, pool, continuous);
        if (flushed == 0) {
            break;
        }
    }

    check_list(cache, cache->indx[0], cache->indx[0] + i);
    cache->seq[1] += i;
    cache->indx[0] += i;
    if (cache->indx[1] < cache->indx[0]) {
        my_assert(cache_type_video(cache));
        cache->indx[1] = cache->indx[0];
    }
}

static void check_wait_list(media_cache* cache)
{
#if 0
    uint32_t i = 0;
    uint64_t from = cache->indx[0];
    uint64_t seq = 0;

    for (i = 0; i < cache->nr; ++i) {
        uint32_t idx = (uint32_t) ((from + i) % cache->nr);
        struct my_buffer* buf = cache->wait_list[idx];

        if ((from + i) >= cache->indx[1]) {
            my_assert(buf == NULL);
        }

        if (buf == NULL) {
            continue;
        }

        media_buffer* cur = (media_buffer *) buf->ptr[0];
        my_assert(cur->seq > seq);
        seq = cur->seq;
    }
#endif
}

static intptr_t flush_overflowed(media_cache* cache, intptr_t nr, repos* pool)
{
    uint32_t flushed = 0;
    intptr_t i = 0;
    for (; i < nr; i += flushed) {
        uint32_t idx = (uint32_t) ((cache->indx[0] + i) % cache->nr);
        if (cache->wait_list[idx] == NULL) {
            flushed = 1;
            continue;
        }
        flushed = frame_flush(idx, cache, pool, overflow);
    }
    return i - nr;
}

static struct my_buffer* filter_late_packet(struct my_buffer* mbuf, media_cache* cache)
{
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    uint32_t type = media->frametype;
    uint32_t group = media->fragment[1];

    if (group > 1) {
        cache->probe.drop++;
        mbuf->mop->free(mbuf);
        return NULL;
    }

    if (reflost(media, cache->vblk)) {
        if (!reference_self(type)) {
            cache->probe.drop++;
            mbuf->mop->free(mbuf);
            return NULL;
        }

        if (video_type_i == type) {
            return mbuf;
        }

        if (media->seq == cache->vblk.refseq) {
            cache->probe.dup++;
            mbuf->mop->free(mbuf);
            return NULL;
        }

        if (media->seq < cache->vblk.refseq) {
            return mbuf;
        }
    }

    if (reference_able(type)) {
        cache->vblk.refseq = media->seq;
    }
    return mbuf;
}

static void skip_incomplete_frame(media_buffer* media, media_cache* cache)
{
    uint64_t next_frame = media->seq - media->fragment[0] + media->fragment[1];
    if (next_frame <= cache->indx[0]) {
        my_assert2(0, "impossible, %llu:%llu:%llu\n", cache->indx[0], media->seq, next_frame);
        return;
    }

    intptr_t nr = (intptr_t) (next_frame - cache->indx[0]);
    nr = flush_overflowed(cache, nr, NULL);
    my_assert(nr == 0);

    del_lost(cache, cache->indx[0], next_frame, 0);
    check_list(cache, cache->indx[0], next_frame);

    cache->indx[0] = next_frame;
    cache->seq[1] = cache->indx[0] - 1;
    if (cache->indx[1] < cache->indx[0]) {
        cache->indx[1] = cache->indx[0];
    }
}

static struct my_buffer* add_to_wait(struct my_buffer* mbuf,
                            media_cache* cache, repos* pool, uint32_t current, uint32_t rtt)
{
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    // seq[1] saved the media_cache max output seq
    my_assert(cache->seq[1] == cache->indx[0] - 1);
    if (media->seq <= cache->seq[1]) {
        if (cache_type_audio(cache)) {
            return mbuf;
        }
        return filter_late_packet(mbuf, cache);
    }

    if (cache_type_audio(cache)) {
        my_assert(cache->wait_list[cache->indx[0] % cache->nr] == NULL);
    } else if (cache->nr < media->fragment[1]) {
        // this should not happen because cache->nr includes gop and
        // the number of packets in average of frames larger than mtu.
        // while, if it does happen, we have to drop the frame because
        // it absolutely will flush wait_list before getting the full frame.
        cache->probe.drop++;
        mbuf->mop->free(mbuf);
        return NULL;
    } else if (cache->ready == 0) {
        if (media->frametype != video_type_idr) {
            if (media->frametype != video_type_sps) {
                cache->probe.drop++;
                mbuf->mop->free(mbuf);
                return NULL;
            }
        } else {
            cache->media = *media;
            cache->ready = 1;
            logmsg("%u idr recved pts = %llu, seq = %llu\n", now(), media->pts, media->seq);
        }
    }

    uint32_t idx = (uint32_t) (media->seq % cache->nr);
    int64_t nr = media->seq - cache->indx[0] + 1 - cache->nr;
    intptr_t loop = (intptr_t) my_min(nr, (int64_t) cache->nr);
    if (loop > 0) {
        intptr_t n = flush_overflowed(cache, loop, pool);
        nr += n;
        del_lost(cache, cache->indx[0], cache->indx[0] + loop + n, 0);
        check_list(cache, cache->indx[0], cache->indx[0] + nr);

        cache->indx[0] += nr;
        cache->seq[1] = cache->indx[0] - 1;
        if (cache->indx[1] < cache->indx[0]) {
            cache->indx[1] = cache->indx[0];
        }
    } else if (cache->wait_list[idx] != NULL) {
        my_assert(cache->indx[1] > media->seq);
        cache->probe.dup++;
        mbuf->mop->free(mbuf);
        return NULL;
    }

    uint64_t gopseq = cache->indx[1];
    intptr_t seq_offset = 0;
    if (cache_type_video(cache)) {
        frame_t* frame = video_frame_get(cache, media);
        if (frame == NULL) {
            my_assert2(loop < 0, "loop = %d", (int) loop);
            skip_incomplete_frame(media, cache);
            mbuf->mop->free(mbuf);
            flush_continuous(cache, pool);
            return NULL;
        }
        gopseq = frame->gopseq;

        frame->reached += 1;
        my_assert2(frame->total >= frame->reached,
                   "frame->total = %d, reached = %d, "
                   "seq = %llu, gopseq = %llu, frag[1] = %llu, seq = %llu",
                   (int) frame->total, (int) frame->reached,
                   frame->seq, frame->gopseq, media->seq, media->seq);
        if (frame->reached == 1) {
            seq_offset = media->fragment[1] - media->fragment[0] - 1;
        } else {
            seq_offset = -1;
        }
    }

    uint16_t info[2] = {0};
    jitter_info_t* jitter_info = (jitter_info_t *) ((uintptr_t) media - sizeof(jitter_info_t));
    info[0] = jitter_info->s.lost;
    info[1] = jitter_info->s.delay;

    if (seq_offset != -1) {
        jitter_info->expire = current + ((rtt > 30) ? rtt * 4 : 120) + (uint32_t) (seq_offset * 5);
        list_add_tail(&mbuf->head, &cache->timer_head);
    }
    my_assert(cache->wait_list[idx] == NULL);
    cache->wait_list[idx] = mbuf;

    if (media->seq >= cache->indx[1]) {
        if (cache->indx[1] > gopseq) {
            gopseq = cache->indx[1];
        }

        add_lost(cache, gopseq, media->seq, current, info);
        cache->indx[1] = media->seq + 1;
    } else {
        del_lost(cache, media->seq, media->seq + 1, 1);
    }
    cache->probe.recv++;

    flush_continuous(cache, pool);
    return NULL;
}

static struct my_buffer* expire_buffer(media_cache* cache, uint32_t current)
{
    struct my_buffer* mbuf = NULL;
    struct list_head* ent;

    for (ent = cache->timer_head.next; ent != &cache->timer_head; ent = ent->next) {
        struct my_buffer* buf = list_entry(ent, struct my_buffer, head);
        jitter_info_t* jitter_info = (jitter_info_t *) ((uintptr_t) buf->ptr[0] - sizeof(jitter_info_t));
        uint32_t expire = jitter_info->expire;
        if (expire > current) {
            break;
        }
        mbuf = buf;
    }

    return mbuf;
}

static void flush_expired(media_cache* cache, repos* pool, uint32_t current)
{
    struct my_buffer* mbuf = NULL;
LABEL1:
    mbuf = expire_buffer(cache, current);
    if (mbuf == NULL) {
        check_sequence("check expired: ", cache, &pool->head);
        return;
    }

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    uint32_t i = 0, flushed = 0, rollback = 0;
    uint64_t from = cache->indx[0];
    uint64_t to = cache->indx[1];

    for (; from <= media->seq; i += flushed, from += flushed) {
        uint32_t idx = (uint32_t) (from % cache->nr);
        if (cache->wait_list[idx] == NULL) {
            flushed = 1;
            ++rollback;
            continue;
        }

        flushed = frame_flush(idx, cache, pool, expired);
        if (flushed == 0) {
            i -= rollback;
            goto LABEL2;
        }
        rollback = 0;
    }

    for (; from < to; i += flushed, from += flushed) {
        uint32_t idx = (uint32_t) (from % cache->nr);
        if (cache->wait_list[idx] == NULL) {
            break;
        }

        flushed = frame_flush(idx, cache, pool, expired);
        if (flushed == 0) {
            goto LABEL2;
        }
    }

LABEL2:
    to = my_min(cache->indx[0] + i, cache->indx[1]);
    check_list(cache, cache->indx[0], to);

    del_lost(cache, cache->indx[0], to, 0);
    cache->seq[1] += i;
    cache->indx[0] += i;
    if (cache->indx[1] < cache->indx[0]) {
        my_assert(cache_type_video(cache));
        cache->indx[1] = cache->indx[0];
    }

    if (flushed != 0) {
        goto LABEL1;
    }
    check_sequence("check expired: ", cache, &pool->head);
}

static void merge_to_repos(repos* pool, media_cache* cache)
{
    lock(&cache->lck);
    check_sequence("check new: ", cache, &pool->head);

    fraction_add(&cache->repos.capacity, &pool->capacity);
    list_join(&cache->repos.head, &pool->head);
    list_del(&pool->head);

    check_sequence("check merged: ", cache, &cache->repos.head);
    unlock(&cache->lck);
}

push_retval* cache_push(my_handle* handle, struct my_buffer* mbuf, uint32_t rtt)
{
    media_cache* cache = (media_cache *) handle_get(handle);
    if (cache == NULL) {
        return NULL;
    }

    uint32_t current = now();
    media_buffer* media = NULL;
    if (mbuf != NULL) {
        media = (media_buffer *) mbuf->ptr[0];
    }

    push_retval* retp = &cache->retval;
    if (__builtin_expect(cache->nr == 0, 0)) {
        if (media == NULL) {
            handle_put(handle);
            return NULL;
        }

        if (-1 == realloc_wait_list(cache, media)) {
            retp->ret = -1;
            if (EINVAL == errno && cache->ablk.latch[0] < current) {
                my_assert(cache_type_video(cache));
                cache->ablk.latch[0] = current + 100;
                retp->seq = 0;
            } else {
                retp->seq = (uint32_t) (-1);
            }
            handle_put(handle);
            return retp;
        }

        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        cache->seq[0] = cache->seq[1] = media->seq - 1;
        cache->indx[0] = cache->indx[1] = media->seq;
    } else if (cache_type_video(cache) && media != NULL) {
        if (media->frametype == video_type_sps) {
            mbuf->mop->free(mbuf);
            mbuf = NULL;
            media = NULL;
        }
    }
    retp->ret = 0;

    repos pool;
    INIT_LIST_HEAD(&pool.head);
    fraction_zero(&pool.capacity);

    flush_expired(cache, &pool, current);
    if (mbuf != NULL) {
        check_wait_list(cache);
        mbuf = add_to_wait(mbuf, cache, &pool, current, rtt);
        check_wait_list(cache);
    }

    if (!list_empty(&pool.head)) {
        merge_to_repos(&pool, cache);
    }

    if (mbuf != NULL) {
        add_to_repos(mbuf, cache);
    }

    retp->seq = get_lost(cache, current, rtt);
    handle_put(handle);
    return retp;
}

static int32_t recovery_silence_frame(media_buffer* media, fraction* f,
                                      struct list_head* headp, media_cache* cache)
{
    struct my_buffer* buf = NULL;
    fraction gen;
    gen.num = 0;
    gen.den = 1;

    if (media->seq == cache->ablk.next_seq && cache->ablk.next_pts < media->pts) {
        audio_format* fmt = to_audio_format(media->pptr_cc);
        gen.num = (uint32_t) (media->pts - cache->ablk.next_pts);
        // this is not an accurate algorithm, only to reduce
        // the effect of no feeding by cache_pull because of dtx.
        if (cache->ablk.not_feed.num > 0) {
            if (cache->ablk.not_feed.num >= gen.num) {
                cache->ablk.not_feed.num -= gen.num;
                gen.num = 0;
            } else {
                gen.num -= cache->ablk.not_feed.num;
                cache->ablk.not_feed.num = 0;
            }
        }

        if (gen.num > 0) {
            buf = fmt->ops->muted_frame_get(fmt, gen.num);
        }
    }

    uint32_t bytes = 0;
    if (buf != NULL) {
        logmsg("recovery %d ms, media->pts = %lld, cache->next_pts = %lld\n",
               gen.num, media->pts, cache->ablk.next_pts);

        media_buffer* mediabuf = (media_buffer *) buf->ptr[0];
        mediabuf->iden = media->iden;
        mediabuf->pts = cache->ablk.next_pts;
        mediabuf->seq = real(cache->ablk.next_seq);
        list_add_tail(&buf->head, headp);
        bytes = (uint32_t) buf->length;
    }

    cache->ablk.next_seq = media->seq + 1;
    cache->ablk.next_pts = media->pts + f->num / f->den;

    fraction_add(f, &gen);
    return bytes;
}

static uint32_t shrink_audio(uint32_t ms, media_cache* cache, struct list_head* headp)
{
    repos* pool = &cache->repos;
    uint32_t pulled = 0;
    uint64_t pts = 0;

    while (!list_empty(&pool->head)) {
        struct list_head* ent = pool->head.next;
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];

        fraction frac = media_capacity(cache, media);
        if (pts == 0) {
            pts = media->pts + ms;
        } else if (media->pts > pts) {
            break;
        }

        list_del(ent);
        list_add_tail(&mbuf->head, headp);
        pulled += 1;
        fraction_sub(&pool->capacity, &frac);
    }
    return pulled;
}

static uint32_t shrink_video(uint32_t packets, media_cache* cache, struct list_head* headp)
{
    repos* pool = &cache->repos;
    uint32_t pulled = 0, nr = packets;

    for (struct list_head* ent = pool->head.next->next;
         ent != &pool->head && 0 != (packets--);
         ent = ent->next)
    {
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (0 != media->fragment[0] ||
            media->frametype != video_type_idr)
        {
            continue;
        }

        list_split(&pool->head, ent);
        list_join(&pool->head, headp);
        list_del(&pool->head);
        list_add_tail(&pool->head, ent);
        pulled += nr - packets;
        nr = packets;
    }

    fraction frac;
    frac.num = pulled;
    frac.den = 1;
    fraction_sub(&pool->capacity, &frac);
    return pulled;
}

static uint64_t free_shrinked_stream(struct list_head* headp)
{
    struct list_head* ent = headp->prev;
    struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
    media_buffer* media = (media_buffer *) mbuf->ptr[0];

    uint64_t seq = media->seq;
    free_buffer(headp);
    return seq;
}

static uint32_t repos_buffered_ms(media_cache* cache, uint32_t val, uint32_t* hole)
{
    uint32_t total = 0;
    repos* pool = &cache->repos;

    if (!list_empty(&pool->head)) {
        struct list_head* ent1 = pool->head.next;
        struct my_buffer* mbuf1 = list_entry(ent1, struct my_buffer, head);
        media_buffer* media1 = (media_buffer *) mbuf1->ptr[0];

        struct list_head* ent2 = pool->head.prev;
        struct my_buffer* mbuf2 = list_entry(ent2, struct my_buffer, head);
        media_buffer* media2 = (media_buffer *) mbuf2->ptr[0];

        // seqbase changed, let the cached get out asap.
        if (__builtin_expect(seqbase(media1->seq) != seqbase(media2->seq), 0)) {
            total = val;
            if (hole) {
                hole[0] = 0;
            }
        } else {
            my_assert(media2->seq >= media1->seq);
            my_assert(media2->pts >= media1->pts);
            uint64_t seq = media2->seq - media1->seq + 1;

            fraction frac = media_capacity(cache, media1);
            // differ in real pts
            uint32_t n1 = (uint32_t) (media2->pts - media1->pts) + frac.num / frac.den;
            // differ in pts calc by frames, maybe smaller than real pts
            // because in dtx, pts increase larger while seq reamining puls 1 per packet
            uint32_t n2 = (uint32_t) ((frac.num * seq) / frac.den);
            // real differ of the pts in fact, it is smaller because some seq may lost
            uint32_t n3 = pool->capacity.num / pool->capacity.den;
            my_assert(n1 >= n2);
            my_assert(n2 >= n3);
            total = n1 - (n2 - n3);

            if (hole) {
                my_assert(media1->seq > pool->seq);
                // this is the seq hole between boundary
                uint32_t seqs = (uint32_t) (media1->seq - pool->seq - 1);
                hole[0] = n2 - n3 + (uint32_t) ((frac.num * seqs) / frac.den);
            }
        }
    }

    return total;
}

static uint32_t round_pts(media_cache* cache, uint32_t latch)
{
    if (latch > 0) {
        fraction ms = media_capacity(cache, NULL);
        uint32_t n = ms.num / ms.den;
        latch = latch / n;
        latch *= n;
    }
    return latch;
}

typedef struct {
    int32_t nr;
    uint32_t pulled;
    uint64_t seq;
    struct list_head head;
} pull_result_t;

static void cache_pull_audio(media_cache* cache, pull_result_t* result, const fraction* desire)
{
    struct list_head shrink;
    repos* pool = &cache->repos;
    INIT_LIST_HEAD(&shrink);
    uint32_t current = now();

    if (cache->ablk.latch[0] != 0) {
        uint32_t hole = 0;
        uint32_t marking = my_max(200, cache->ablk.latch[1] / 4);
        lock(&cache->lck);
        uint32_t ms = repos_buffered_ms(cache, marking, &hole);
        unlock(&cache->lck);

        if (marking > ms) {
            fraction_add(&cache->ablk.not_feed, desire);
            return;
        }

        // hole is the lost, maybe conceal by codec, should not included in.
        // when seqbase changes, take it as no hole at all, it is too rarely to
        // make sense.
        cache->ablk.not_feed.num = cache->ablk.not_feed.num / cache->ablk.not_feed.den;
        if (cache->ablk.not_feed.num > hole) {
            cache->ablk.not_feed.num -= hole;
        }
        cache->ablk.not_feed.num = round_pts(cache, cache->ablk.not_feed.num);
        cache->ablk.not_feed.den = 1;
        cache->ablk.latch[0] = 0;
    }

    fraction need = *desire;
    lock(&cache->lck);
    while (!list_empty(&pool->head)) {
        struct list_head* ent = pool->head.next;
        list_del(ent);

        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        fraction frac = media_capacity(cache, media);
        fraction_sub(&pool->capacity, &frac);

        result->nr += recovery_silence_frame(media, &frac, &result->head, cache);
        result->seq = media->seq;
        media->seq = real(media->seq);
        list_add_tail(&mbuf->head, &result->head);
        result->nr += mbuf->length;
        result->pulled += 1;

        int n = fraction_sub(&need, &frac);
        if (__builtin_expect(n <= 0, 0)) {
            break;
        }
    }

    uint32_t total = repos_buffered_ms(cache, cache->ablk.latch[1], NULL);
    // when seqbase changes, no enough information for shrinking
    // repos_buffered_ms will disable shrink.
    if (total <= cache->ablk.latch[1]) {
        cache->shrink_time = (uint32_t) -1;
    } else if (cache->shrink_time == (uint32_t) -1) {
        cache->shrink_time = current + overload_ttl(cache->ablk.latch[1]);
    } else if (current > cache->shrink_time) {
        result->pulled += shrink_audio(total - cache->ablk.latch[1], cache, &shrink);
        cache->shrink_time = (uint32_t) -1;
    }

    if (result->pulled > 0) {
        my_assert(result->seq != 0);

        if (!list_empty(&shrink)) {
            result->seq = free_shrinked_stream(&shrink);
        }
        cache->seq[0] = result->seq;
    } else {
        my_assert(result->seq == 0);
        my_assert(pool->capacity.num == 0);
        my_assert(cache->shrink_time == (uint32_t) -1);
        cache->ablk.latch[0] = current;
        fraction_zero(&cache->ablk.not_feed);
    }
    unlock(&cache->lck);
}

static void cache_pull_video(media_cache* cache, pull_result_t* result, const fraction* desire)
{
    struct list_head shrink;
    INIT_LIST_HEAD(&shrink);
    repos* pool = &cache->repos;
    fraction frac;
    frac.den = 1;
    uint32_t current = now();
    cache->vblk.pts = desire->num;

    lock(&cache->lck);
    while (!list_empty(&pool->head)) {
        struct list_head* ent = pool->head.next;
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->pts > desire->num) {
            cache->vblk.pts = media->pts;
            goto LABEL;
        }
        list_del(ent);

        result->pulled += 1;
        result->seq = media->seq;
        media->seq = real(media->seq);
        list_add_tail(&mbuf->head, &result->head);
        offset_field(media) = 0;
    }

    if (result->pulled == 0) {
        cache->vblk.pts = desire->num;
    }

LABEL:
    result->nr = result->pulled;
    frac.num = result->pulled;
    fraction_sub(&pool->capacity, &frac);

    uint32_t size_nr = pool->capacity.num / pool->capacity.den;
    if (size_nr > cache->vblk.size_nr) {
        if (cache->shrink_time == (uint32_t) -1) {
            cache->shrink_time = current + overload_ttl(cache->vblk.size_ms);
        } else if (current > cache->shrink_time) {
            result->pulled += shrink_video(size_nr - cache->vblk.size_nr, cache, &shrink);
            size_nr = pool->capacity.num / pool->capacity.den;
        }
    }

    if (size_nr <= cache->vblk.size_nr) {
        cache->shrink_time = (uint32_t) -1;
    }

    if (result->pulled > 0) {
        if (!list_empty(&shrink)) {
            result->seq = free_shrinked_stream(&shrink);
        }
        cache->seq[0] = result->seq;
    }
    unlock(&cache->lck);
}

int32_t cache_pull(my_handle* handle, struct list_head* headp, const fraction* desire)
{
    if (desire == NULL) {
        return 0;
    }

    media_cache* cache = (media_cache *) handle_get(handle);
    if (cache == NULL) {
        return -1;
    }

    if (desire->num == 0) {
        int32_t n = -1;
        if (!cache->eof) {
            if (cache->ready == 1) {
                struct my_buffer* mbuf = mbuf_alloc_2(sizeof(media_buffer));
                if (mbuf != NULL) {
                    mbuf->ptr[1] = mbuf->ptr[0] + mbuf->length;
                    mbuf->length = 0;
                    media_buffer* media = (media_buffer *) mbuf->ptr[0];
                    *media = cache->media;
                    list_add(&mbuf->head, headp);
                }
            }
            n = 0;
        }
        handle_put(handle);
        return n;
    }

    pull_result_t result;
    memset(&result, 0, sizeof(pull_result_t));
    INIT_LIST_HEAD(&result.head);
    if (cache_type_audio(cache)) {
        cache_pull_audio(cache, &result, desire);
    } else {
        cache_pull_video(cache, &result, desire);
    }

    repos* pool = &cache->repos;
    if (result.nr == 0) {
        if (__builtin_expect(cache->eof, 0)) {
            result.nr = -1;
        } else if (cache_type_video(cache)) {
            if (cache->vblk.pts != (uint64_t) -1 &&
                cache->vblk.pts > desire->num + 500 &&
                cache->shrink_time != (uint32_t) -1)
            {
                result.nr = (int32_t) (cache->vblk.pts - desire->num);
            } else if (cache->vblk.pts == (uint64_t) -1) {
                result.nr = -2;
            }
        }
    }

    if (result.pulled > 0) {
        if (__builtin_expect((pool->seq != 0 && (seqbase(result.seq) == seqbase(pool->seq))), 1)) {
            int32_t nr = (int32_t) (result.seq - pool->seq);
            if (nr < result.pulled) {
                logmsg("bugs: newseq = %lld, oldseq = %lld, pulled = %d, "
                       "there should be some duplicated packet\n", result.seq, pool->seq, result.pulled);
            } else if (nr > (int32_t) result.pulled) {
                __sync_add_and_fetch(&cache->probe.lost, nr - result.pulled);
            }
        }
        pool->seq = result.seq;
        list_join(&result.head, headp);
        list_del(&result.head);
    }

    handle_put(handle);
    return result.nr;
}

void cache_shutdown(my_handle* handle)
{
    media_cache* cache = (media_cache *) handle_get(handle);
    if (cache != NULL) {
        cache->eof = 1;
        handle_put(handle);
    }
    logmsg("cache_shutdown %p\n", handle);
    handle_release(handle);
}
