
#include "udp.h"
#include "fdset_in.h"
#include "my_errno.h"
#include "mbuf.h"
#include "lock.h"
#include "now.h"
#include "sortcache.h"
#include "media_buffer.h"
#include "fmt.h"
#include "resendbuf.h"
#include "media_addr.h"
#include "address.h"
#include "proto/packet.h"
#include <string.h>

#ifndef _MSC_VER
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

typedef struct {
    uint64_t uid;
    uint32_t sid;
    uint8_t token;
    uint32_t ttl;
    uint32_t on;
    uint32_t last_probe;
    my_handle* cache;
    media_buffer media;
    struct list_head entry;
    struct list_head ttl_entry;
} cache_entry;

typedef struct {
    struct fd_struct fds;
    lock_t lck;
    my_handle* self;

    book_t book;
    uint32_t life[2];
    struct sockaddr_in* addr;
    rc4_stat rc4;

    uint64_t uid;
    uint32_t sid;
    uint32_t dont_play;
    uint64_t token[2];
    uint32_t last_read;
    uint32_t last_io_stat;
    uint64_t bytes[2];

    struct list_head upload;
    udp_rtt_request rtt_req;
    uint32_t rtt[6];
    uint32_t* rtt_id;
    uint32_t* maintain_id;
    struct my_buffer* sps;
    lock_t sps_lck;

    struct list_head alive, killed, age;
    resend_buffer resend[2];
    notify_fptr fptr;
} udp_context;

#define stable_nr 10
#define udp_rtt_interval 1000
#define ttl_ms (udp_rtt_interval * 2)
#define frozen_ms 0
#define udp_packet_bytes 1500
#define resend_buffer_ms 2000

static void udp_add_output_packet(udp_context* ctx, struct my_buffer* mbuf)
{
    lock(&ctx->lck);
    list_add_tail(&mbuf->head, &ctx->upload);
    ctx->fds.mask |= EPOLLOUT;
    unlock(&ctx->lck);
    fdset_update_fdmask(ctx->self);
}

static inline uint64_t encrypt_uid(udp_context* ctx, uint64_t uid)
{
    rc4_stat stat = ctx->rc4;
    rc4(&stat, (char *) &uid, sizeof(uid));
    return uid;
}

void udp_push_audio(void* something, struct my_buffer* mbuf, uint32_t encrypt)
{
    struct fd_struct* fds = (struct fd_struct *) something;
    udp_context* ctx = (udp_context *) fds;

    audio_packet* packet = (audio_packet *) mbuf->ptr[0];
    packet->header.uid = ctx->uid;

    if (encrypt) {
        rc4_stat s = ctx->rc4;
        packet->media.encrypt = 1;

        char* pch = mbuf->ptr[0] + sizeof(audio_packet);
        uint32_t bytes = (uint32_t) (mbuf->length - sizeof(audio_packet));
        rc4(&s, pch, bytes);
    }

    uint32_t* intp = resend_information(mbuf);
    if (__builtin_expect(resend_need_init(&ctx->resend[0]), 0)) {
        uint32_t capacity = resend_buffer_ms / intp[0];
        if (-1 == resend_buffer_init(&ctx->resend[0], capacity)) {
            goto LABEL;
        }
    }

    struct my_buffer* mbuf2 = mbuf->mop->clone(mbuf);
    if (mbuf2 != NULL) {
        resend_buffer_push(&ctx->resend[0], mbuf2, intp[2]);
    }
LABEL:
    udp_add_output_packet(ctx, mbuf);
}

int32_t udp_push_video(void* something, struct my_buffer* sps, struct my_buffer* mbuf, uint32_t encrypt, uint32_t renew)
{
    struct fd_struct* fds = (struct fd_struct *) something;
    udp_context* ctx = (udp_context *) fds;

    video_packet* packet = (video_packet *) mbuf->ptr[0];
    packet->header.uid = ctx->uid;

    if (encrypt) {
        rc4_stat s = ctx->rc4;
        packet->media.encrypt = 1;

        char* pch = mbuf->ptr[0] + sizeof(video_packet);
        uint32_t bytes = (uint32_t) (mbuf->length - sizeof(video_packet));
        rc4(&s, pch, bytes);
    }

    if (__builtin_expect(renew == 1, 0)) {
        video_packet* vpk = (video_packet *) sps->ptr[0];
        vpk->header.uid = ctx->uid;

        sps = sps->mop->clone(sps);
        if (sps == NULL) {
            mbuf->mop->free(mbuf);
            return -1;
        }

        lock(&ctx->sps_lck);
        if (ctx->sps != NULL) {
            ctx->sps->mop->free(ctx->sps);
        }
        ctx->sps = sps;
        unlock(&ctx->sps_lck);

        sps = sps->mop->clone(sps);
        if (sps != NULL) {
            udp_add_output_packet(ctx, sps);
        }
    } else if (ctx->sps == NULL) {
        ctx->sps = sps->mop->clone(sps);
    }

    uint32_t* intp = resend_information(mbuf);
    if (__builtin_expect(resend_need_init(&ctx->resend[1]), 0)) {
        if (packet->frame != video_type_idr) {
            mbuf->mop->free(mbuf);
            return 0;
        }

        video_packet* sps_packet = (video_packet *) sps->ptr[0];
        uint32_t frame_duration = intp[0];
        uint32_t gop_duration = ntohs(sps_packet->sps.gop) * frame_duration;
        uint32_t capacity = resend_buffer_ms * ntohs(sps_packet->sps.gop_mtus) / gop_duration;
        if (-1 == resend_buffer_init(&ctx->resend[1], capacity)) {
            goto LABEL;
        }
    }

    struct my_buffer* mbuf2 = mbuf->mop->clone(mbuf);
    if (mbuf2 != NULL) {
        resend_buffer_push(&ctx->resend[1], mbuf2, intp[2]);
    }
LABEL:
    udp_add_output_packet(ctx, mbuf);
    return 0;
}

static void stream_stat_changed(udp_context* ctx, cache_entry* entry, uint32_t on)
{
    if (entry->on == on) {
        return;
    }

    entry->on = on;
    lock(&ctx->lck);
    if (ctx->fptr.fptr != NULL) {
        usr_stream_t stream;
        stream.uid = ntohll(encrypt_uid(ctx, entry->uid));
        stream.cid = ntohl(entry->sid);
        stream.aud0_vid1 = (video_type == media_type(*entry->media.pptr_cc));
        if (on) {
            stream.media = &entry->media;
        } else {
            stream.media = NULL;
        }
        logmsg_d("%lld stream %d %d\n", stream.uid, stream.aud0_vid1, on);
        ctrace(ctx->fptr.fptr((void *) ctx->fptr.ctx, notify_user_stream, &stream));
    }
    unlock(&ctx->lck);
}

static void udp_detach(struct fd_struct* fds)
{
    udp_context* ctx = (udp_context *) fds;
    my_close(fds->fd);
    handle_release(ctx->self);

    while (!list_empty(&ctx->alive)) {
        struct list_head* ent = ctx->alive.next;
        list_del(ent);
        cache_entry* entry = list_entry(ent, cache_entry, entry);
        stream_stat_changed(ctx, entry, 0);
        handle_dettach(entry->cache);
        my_free(entry);
    }

    while (!list_empty(&ctx->killed)) {
        struct list_head* ent = ctx->killed.next;
        list_del(ent);
        cache_entry* entry = list_entry(ent, cache_entry, entry);
        handle_release(entry->cache);
        my_free(entry);
    }

    resend_buffer_reset(&ctx->resend[0]);
    resend_buffer_reset(&ctx->resend[1]);
    free_buffer(&ctx->upload);

    if (ctx->maintain_id != NULL) {
        sched_handle_free(ctx->maintain_id);
    }

    if (ctx->rtt_id != NULL) {
        sched_handle_free(ctx->rtt_id);
    }

    if (ctx->sps != NULL) {
        ctx->sps->mop->free(ctx->sps);
    }

    sockaddr_put(&ctx->book, ctx->addr);
    sockaddr_delete(&ctx->book, 0);
    my_free(ctx);
}

static media_buffer* fill_media_buffer(udp_context* ctx, struct my_buffer* mbuf, uint8_t* token)
{
    udp_proto_header* header = (udp_proto_header *) mbuf->ptr[1];
    mbuf->ptr[0] -= sizeof(media_buffer);
    jitter_info_t* jitter_info = (jitter_info_t *) mbuf->ptr[0];
    mbuf->ptr[0] += sizeof(jitter_info_t);

    video_packet* video = NULL;
    // media and header is overlapped
    // however, writing to pptr_cc does not corrupt header->sid/uid
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    switch (header->pcode) {
        case P_AUDIO_SILK_8K:
            media->pptr_cc = fourcc_get(codec_silk, 8000, 1);
            mbuf->ptr[1] += sizeof(audio_packet);
            mbuf->length -= sizeof(audio_packet);
            break;
        case P_AUDIO_SILK_16K:
            media->pptr_cc = fourcc_get(codec_silk, 16000, 1);
            mbuf->ptr[1] += sizeof(audio_packet);
            mbuf->length -= sizeof(audio_packet);
            break;
        case P_VIDEO_H264_288x352:
            media->pptr_cc = fourcc_get(codec_h264, 288, 352);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        case P_VIDEO_H264_352x288:
            media->pptr_cc = fourcc_get(codec_h264, 352, 288);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        case P_VIDEO_H264_480x640:
            media->pptr_cc = fourcc_get(codec_h264, 480, 640);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        case P_VIDEO_H264_640x480:
            media->pptr_cc = fourcc_get(codec_h264, 640, 480);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        case P_VIDEO_H264_720x1080:
            media->pptr_cc = fourcc_get(codec_h264, 720, 1080);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        case P_VIDEO_H264_1080x720:
            media->pptr_cc = fourcc_get(codec_h264, 1080, 720);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        case P_VIDEO_H264_720x1280:
            media->pptr_cc = fourcc_get(codec_h264, 720, 1280);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        case P_VIDEO_H264_1280x720:
            media->pptr_cc = fourcc_get(codec_h264, 1280, 720);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        case P_VIDEO_H264_1080x1920:
            media->pptr_cc = fourcc_get(codec_h264, 1080, 1920);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        case P_VIDEO_H264_1920x1080:
            media->pptr_cc = fourcc_get(codec_h264, 1920, 1080);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        case P_VIDEO_H264_540x960:
            media->pptr_cc = fourcc_get(codec_h264, 540, 960);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        case P_VIDEO_H264_960x540:
            media->pptr_cc = fourcc_get(codec_h264, 960, 540);
            video = (video_packet *) mbuf->ptr[1];
            mbuf->ptr[1] += sizeof(video_packet);
            mbuf->length -= sizeof(video_packet);
            break;
        default:
            my_assert(0);
            return NULL;
    }

    uint32_t sid = ntohl(header->sid);
    uint64_t uid = ntohll(encrypt_uid(ctx, header->uid));
    media->iden = make_media_id(sid, uid);

    media_header_t* media_header = (media_header_t *) (header + 1);
    media->pts = ntohl(media_header->stamp);

    memset(media->vp, 0, sizeof(media->vp));
    if (video != NULL) {
        media->seq = ctx->token[1] | ntohl(media_header->seq);
        media->frametype = video->frame;
        media->angle = video->angle;
        media->fragment[0] = ntohs(video->vlc.gidx);
        media->fragment[1] = ntohs(video->vlc.group);
        media->vp[0].stride = (uint32_t) mbuf->length;
        media->vp[0].ptr = mbuf->ptr[1];
        media->vp[0].height = 1;
        offset_field(media) = ntohs(video->vlc.offset);
        if (video->vlc.offset == reflost_indicator) {
            offset_field(media) = reflost_flag;
        }
        jitter_info->gopidx = (((uint32_t) video->gopidx1) << 16) | ((uint32_t) ntohs(video->gopidx2));
    } else {
        media->seq = ctx->token[0] | ntohl(media_header->seq);
        media->frametype = 0;
        media->angle = 0;
        media->fragment[0] = 0;
        media->fragment[1] = 1;
        media->vp[0].stride = (uint32_t) mbuf->length;
        media->vp[0].ptr = mbuf->ptr[1];
        jitter_info->gopidx = 0;
    }

    if (media_header->encrypt == 1) {
        rc4_stat stat = ctx->rc4;
        rc4(&stat, mbuf->ptr[1], (int) mbuf->length);
    } else if (media_header->encrypt != 0) {
        logmsg("unkown crypt flag %d\n", (int) media_header->encrypt);
        return NULL;
    }

    jitter_info->s.lost = media_header->lost;
    jitter_info->s.delay = 100;
    *token = media_header->token;
    return media;
}

static void move_to_killed(udp_context* ctx, cache_entry* entry, uint32_t current)
{
    list_del_init(&entry->ttl_entry);
    list_del(&entry->entry);
    entry->ttl = current + frozen_ms;
    list_add(&entry->entry, &ctx->killed);

    handle_clone(entry->cache);
    cache_shutdown(entry->cache);
}

static cache_entry* find_cache_entry(
        udp_context* ctx, media_buffer* media, uint32_t sid, uint64_t uid, uint8_t token)
{
    cache_entry* entry = NULL;
    struct list_head* ent;
    uint32_t on = 0;
    for (ent = ctx->alive.next; ent != &ctx->alive; ent = ent->next) {
        entry = list_entry(ent, cache_entry, entry);

        if (entry->sid == sid && entry->uid == uid) {
            if (*media->pptr_cc == *entry->media.pptr_cc) {
                if (entry->token == token) {
                    return entry;
                }

                if (audio_type == media_type(*entry->media.pptr_cc)) {
                    return entry;
                }
            }

            if (media_type(*media->pptr_cc) == media_type(*entry->media.pptr_cc)) {
                on = entry->on;
                move_to_killed(ctx, entry, 0);
                break;
            }
        }
    }

    for (ent = ctx->killed.next; ent != &ctx->killed; ent = ent->next) {
        entry = list_entry(ent, cache_entry, entry);
        if (entry->sid == sid && entry->uid == uid && *media->pptr_cc == *entry->media.pptr_cc) {
            return NULL;
        }
    }

    entry = (cache_entry *) my_malloc(sizeof(cache_entry));
    INIT_LIST_HEAD(&entry->ttl_entry);
    entry->ttl = 0;
    entry->sid = sid;
    entry->uid = uid;
    entry->media = *media;
    entry->token = 0;
    entry->on = on;
    entry->last_probe = 0;
    entry->cache = cache_create(800, media, frames_per_packet);
    if (entry->cache == NULL) {
        my_free(entry);
        return NULL;
    }

    int n = -1;
    handle_clone(entry->cache);

    lock(&ctx->lck);
    if (ctx->fptr.fptr != NULL) {
        n = ctx->fptr.fptr((void *) ctx->fptr.ctx, notify_new_stream, (void *) entry->cache);
    }
    unlock(&ctx->lck);

    if (n == -1) {
        handle_release(entry->cache);
        cache_shutdown(entry->cache);
        my_free(entry);
        return NULL;
    }

    list_add(&entry->entry, &ctx->alive);
    return entry;
}

static push_retval* add_to_cache(udp_context* ctx, cache_entry* entry, struct my_buffer* mbuf, uint32_t rtt)
{
    push_retval* ret = cache_push(entry->cache, mbuf, rtt);
    if (ret == NULL) {
        if (mbuf != NULL) {
            mbuf->mop->free(mbuf);
        }
        return ret;
    }

    if (ret->ret != 0 && mbuf != NULL) {
        mbuf->mop->free(mbuf);
    }

    if (ret->seq != (uint32_t) -1) {
        uintptr_t type = media_type(*entry->media.pptr_cc);
        struct my_buffer* resend_req = udp_resend_req(type, ctx->uid, entry->sid, entry->uid, ret->seq);
        if (resend_req != NULL) {
            udp_add_output_packet(ctx, resend_req);
        }
    }
    return ret;
}

static void media_arrived(udp_context* ctx, struct my_buffer* mbuf)
{
    ctx->bytes[1] += mbuf->length;
    udp_proto_header* header = (udp_proto_header *) mbuf->ptr[1];
    // sid, uid will be corrupted after fill_media_buffer, save them
    uint32_t sid = header->sid;
    uint64_t uid = header->uid;

    uint8_t token;
    media_buffer* media = fill_media_buffer(ctx, mbuf, &token);
    if (media == NULL) {
        mbuf->mop->free(mbuf);
        return;
    }
    uintptr_t type = media_type(*media->pptr_cc);

    cache_entry* entry = find_cache_entry(ctx, media, sid, uid, token);
    if (entry == NULL) {
        mbuf->mop->free(mbuf);
        return;
    }
    stream_stat_changed(ctx, entry, 1);

    if (token != entry->token) {
        if (type == video_type) {
            ctx->token[1] = token_get_udp();
            ctx->token[1] <<= 32;
            media->seq = ctx->token[1] | (media->seq & 0xffffffff);
        } else {
            ctx->token[0] = token_get_udp();
            ctx->token[0] <<= 32;
            media->seq = ctx->token[0] | (media->seq & 0xffffffff);
        }
        entry->token = token;
    }

    uint32_t current = now();
    list_del(&entry->ttl_entry);
#if 0
    entry->ttl = ((video_type == type) ? ((uint32_t) -1) : (current + ttl_ms));
#else
    entry->ttl = current + ttl_ms;
#endif
    list_add_tail(&entry->ttl_entry, &ctx->age);

    if (ctx->maintain_id != NULL) {
        int err = fdset_sched(ctx->self, 10, ctx->maintain_id);
        if (err == 0) {
            ctx->maintain_id = NULL;
        }
    }

    if (ctx->dont_play == 1 && audio_type == type) {
        mbuf->mop->free(mbuf);
        return;
    }

    uint32_t rtt = ctx->rtt[elements(ctx->rtt) - 1];
    if (__builtin_expect(rtt < 100, 1)) {
        rtt = 100;
    }

    // downstream close this cache, do a cleanup and continue --- a new cache
    // will create if the network stream still comes
    push_retval* ret = add_to_cache(ctx, entry, mbuf, rtt);
    if (ret == NULL) {
        list_del(&entry->entry);
        list_del(&entry->ttl_entry);
        handle_release(entry->cache);
        my_free(entry);
        return;
    }

    if (current > entry->last_probe + 10000) {
        cache_probe* probe = cache_probe_get(entry->cache);
        if (probe != NULL) {
            uint32_t lost = (uint32_t) __sync_lock_test_and_set(&probe->lost, 0);
            logmsg("*******%s, recv: %d, lost = %d, late = %d, dup = %d, drop = %d\n",
                   (probe->aud0 == 0) ? "audio" : "video", probe->recv, lost, probe->late, probe->dup, probe->drop);

            lock(&ctx->lck);
            if (ctx->fptr.fptr != NULL) {
                media_stat_t stat;
                stat.aud0_vid1 = (video_type == type);
                stat.sid = ntohl(entry->sid);
                stat.uid = ntohll(encrypt_uid(ctx, entry->uid));
                stat.lost = lost;
                stat.recv = probe->recv;
                ctx->fptr.fptr((void *) ctx->fptr.ctx, notify_statistic, &stat);
            }
            unlock(&ctx->lck);

            probe->recv = 0;
            probe->late = 0;
            probe->dup = 0;
            probe->drop = 0;
            entry->last_probe = current;
        }
    }
}

static void resend_packet(udp_context* ctx, struct my_buffer* mbuf, resend_buffer* resend)
{
    udp_resend* req = (udp_resend *) mbuf->ptr[1];
    req->seq_from = ntohl(req->seq_from);
    req->seq_to = ntohl(req->seq_to);
    if (req->seq_to < req->seq_from) {
        logmsg("Bugs: resend request from %d to %d\n", req->seq_from, req->seq_to);
        req->seq_to = req->seq_from;
    }

    if (req->seq_from == 0 && resend == &ctx->resend[1]) {
        if (ctx->sps != NULL) {
            lock(&ctx->sps_lck);
            mbuf = ctx->sps->mop->clone(ctx->sps);
            unlock(&ctx->sps_lck);

            if (mbuf != NULL) {
                udp_add_output_packet(ctx, mbuf);
            }
        }

        req->seq_from = 1;
        if (req->seq_to == 0) {
            return;
        }
    }

    struct list_head head;
    INIT_LIST_HEAD(&head);
    resend_buffer_pull(resend, req->seq_from, req->seq_to + 1, &head);
    while (!list_empty(&head)) {
        struct list_head* ent = head.next;
        list_del(ent);
        mbuf = list_entry(ent, struct my_buffer, head);
        udp_add_output_packet(ctx, mbuf);
    }
}

static void cache_entry_proc(udp_context* ctx, uint32_t current)
{
    // no lock, it is not a problem
    // because we only use one network thread in this project
    struct list_head* ent;
    cache_entry* entry = NULL;

    for (ent = ctx->killed.next; ent != &ctx->killed;) {
        entry = list_entry(ent, cache_entry, entry);
        ent = ent->next;

        void* p = handle_get(entry->cache);
        if (p != NULL) {
            handle_put(entry->cache);
            continue;
        }

        if (entry->ttl <= current) {
            list_del(&entry->entry);
            handle_release(entry->cache);
            my_free(entry);
        }
    }

    for (ent = ctx->age.next; ent != &ctx->age;) {
        entry = list_entry(ent, cache_entry, ttl_entry);
        ent = ent->next;

        // this assure stream_stat_changed will be called
        // before cache_shutdown things
        my_assert(ttl_ms > 1000);
        if (current > entry->ttl - ttl_ms + 1000) {
            stream_stat_changed(ctx, entry, 0);
        }

        if (current >= entry->ttl) {
            move_to_killed(ctx, entry, current);
        }
    }
}

static struct my_buffer* calc_or_echo(udp_context* ctx, struct my_buffer* mbuf)
{
    uint32_t nr = elements(ctx->rtt) - 2;
    uint32_t indx = ctx->rtt[nr] % nr;

    udp_rtt* rtt = (udp_rtt *) mbuf->ptr[1];
    if (rtt->header.uid != ctx->uid) {
        proto_udp_header_reencode(mbuf);
        udp_add_output_packet(ctx, mbuf);
        return NULL;
    }

    ctx->rtt[indx] = now() - (uint32_t) rtt->tms;
    ctx->rtt[nr]++;
    ctx->rtt[nr + 1] = 0;
    for (uint32_t i = 0; i < my_min(ctx->rtt[nr], nr); ++i) {
        ctx->rtt[nr + 1] += ctx->rtt[i];
    }
    ctx->rtt[nr + 1] /= my_min(ctx->rtt[nr], nr);
    return mbuf;
}

uint32_t udp_no_response(my_handle* handle, uint32_t current)
{
    void* ptr = handle_get(handle);
    if (ptr == NULL) {
        return 1;
    }

    struct fd_struct* fds = fd_struct_from(ptr);
    udp_context* ctx = (udp_context *) fds;

    uint32_t no_response = current > (ctx->last_read + 3000);
    handle_put(handle);
    return no_response;
}

static int32_t udp_read(struct fd_struct* fds,
                        struct my_buffer* mbuf, int err, struct sockaddr_in* in)
{
    udp_context* ctx = (udp_context *) fds;
    if (err != 0) {
        handle_clone(ctx->self);
        handle_dettach(ctx->self);
        if (mbuf != NULL) {
            mbuf->mop->free(mbuf);
        }
        return -1;
    }

    uint32_t current = now();
    if (current > ctx->last_io_stat) {
        iostat_t stat;
        stat.nb0 = ctx->bytes[0];
        stat.nb1 = ctx->bytes[1];
        ctx->bytes[0] = ctx->bytes[1] = 0;
        lock(&ctx->lck);
        if (ctx->fptr.fptr != NULL) {
            ctx->fptr.fptr((void *) ctx->fptr.ctx, notify_io_stat, &stat);
        }
        unlock(&ctx->lck);
        ctx->last_io_stat = current + 5000;
    } else if (ctx->last_io_stat == 0) {
        ctx->last_io_stat = current + 5000;
    }

    if (mbuf == NULL) {
        return udp_packet_bytes;
    }

    if (in->sin_addr.s_addr != ctx->addr->sin_addr.s_addr) {
        int32_t n = address_renew(&ctx->book, (uint32_t) in->sin_addr.s_addr, stable_nr);
        if (n == -1) {
            return udp_packet_bytes;
        }
        ctx->addr = sockaddr_get(&ctx->book, ctx->addr);
        assert(ctx->addr != NULL);
    }

    ctx->life[0] = 0;
    ctx->last_read = current;

    uint16_t pcode = proto_udp_header_parsel(mbuf);
    switch (pcode) {
        case P_AUDIO_RTT_TEST:
            mbuf = calc_or_echo(ctx, mbuf);
            break;
        case P_AUDIO_SILK_8K:
        case P_AUDIO_SILK_16K:
        case P_VIDEO_H264_288x352:
        case P_VIDEO_H264_352x288:
        case P_VIDEO_H264_480x640:
        case P_VIDEO_H264_640x480:
        case P_VIDEO_H264_720x1080:
        case P_VIDEO_H264_1080x720:
        case P_VIDEO_H264_720x1280:
        case P_VIDEO_H264_1280x720:
        case P_VIDEO_H264_1080x1920:
        case P_VIDEO_H264_1920x1080:
        case P_VIDEO_H264_540x960:
        case P_VIDEO_H264_960x540:
            media_arrived(ctx, mbuf);
            mbuf = NULL;
            break;
        case P_AUDIO_RESEND_REQ:
            resend_packet(ctx, mbuf, &ctx->resend[0]);
            break;
        case P_VIDEO_RESEND_REQ:
            resend_packet(ctx, mbuf, &ctx->resend[1]);
            break;
        case P_AUDIO_HEARTBEAT:
            proto_udp_header_reencode(mbuf);
            udp_add_output_packet(ctx, mbuf);
            mbuf = NULL;
            break;
        default:
            logmsg("unkown proto %x recved\n", (uint32_t) pcode);
            break;
    }

    if (mbuf != NULL) {
        mbuf->mop->free(mbuf);
    }
    return udp_packet_bytes;
}

static struct my_buffer* udp_write(struct fd_struct* fds, struct sockaddr_in** in, int* err)
{
    my_assert(err == NULL);
    udp_context* ctx = (udp_context *) fds;

    struct my_buffer* mbuf = NULL;
    lock(&ctx->lck);
    struct list_head* ent = ctx->upload.next;
    if (!list_empty(ent)) {
        list_del(ent);
        mbuf = list_entry(ent, struct my_buffer, head);
        if (mbuf != &ctx->rtt_req.mbuf) {
            ctx->bytes[0] += mbuf->length;
        }
        *in = ctx->addr;
    } else {
        fds->mask &= (~EPOLLOUT);
    }
    unlock(&ctx->lck);

    return mbuf;
}

static struct my_buffer* udp_buffer_get(struct fd_struct* fds, uint32_t bytes)
{
    struct my_buffer* mbuf = mbuf_alloc_2(sizeof(media_buffer) + bytes);
    if (__builtin_expect(mbuf != NULL, 1)) {
        mbuf->ptr[0] += sizeof(media_buffer);
        mbuf->ptr[1] = mbuf->ptr[0];
        mbuf->length -= sizeof(media_buffer);
    }
    return mbuf;
}

void udp_drop_stream(my_handle* handle, uint32_t drop)
{
    void* ptr = handle_get(handle);
    if (ptr == NULL) {
        return;
    }

    struct fd_struct* fds = fd_struct_from(ptr);
    udp_context* ctx = (udp_context *) fds;
    ctx->dont_play = drop;
    handle_put(handle);
}

static void send_rtt_packet(udp_context* ctx)
{
    int32_t idle = packet_idle(&ctx->rtt_req.mbuf);
    if (idle == 1) {
        udp_rtt_init(&ctx->rtt_req, 0, 0, 0);
        ctx->life[0] += 1;
        if (ctx->life[0] >= ctx->life[1]) {
            ctx->life[0] = 0;
            ctx->addr = sockaddr_get(&ctx->book, ctx->addr);
            assert(ctx->addr != NULL);
        }
        udp_add_output_packet(ctx, &ctx->rtt_req.mbuf);
    }
}

static void udp_notify(struct fd_struct* fds, uint32_t* id)
{
    udp_context* ctx = (udp_context *) fds;
    int nr = elements(ctx->rtt) - 2;

    if (id[0] == 0) {
        send_rtt_packet(ctx);

        uint32_t ms = udp_rtt_interval;
        if (ctx->rtt[nr] == 0) {
            ms = 100;
        } else {
            ctx->life[1] = 5;
        }

        int err = fdset_sched(ctx->self, ms, id);
        if (err == -1) {
            ctx->rtt_id = id;
        }
        cache_entry_proc(ctx, now());
    } else if (!list_empty(&ctx->age)) {
        struct list_head* ent;
        for (ent = ctx->age.next; ent != &ctx->age; ent = ent->next) {
            cache_entry* entry = list_entry(ent, cache_entry, ttl_entry);
            add_to_cache(ctx, entry, NULL, 0);
        }

        int err = fdset_sched(ctx->self, 10, id);
        if (err == -1) {
            ctx->maintain_id = id;
        }
    } else {
        ctx->maintain_id = id;
    }
}

static struct fds_ops udp_link_ops = {
    udp_read,
    NULL,
    udp_write,
    udp_detach,
    udp_notify,
    udp_buffer_get
};

my_handle* udp_new(void* set, uint32_t ip, uint16_t* port,
                   uint32_t sid, uint64_t uid, uint32_t token, uint32_t dont_play,
                   rc4_stat* rc4, notify_fptr fptr)
{
    errno = ENOMEM;
    udp_context* ctx = (udp_context *) my_malloc(sizeof(udp_context));
    if (ctx == NULL) {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));

    ctx->rtt_id = sched_handle_alloc(0);
    if (ctx->rtt_id == NULL) {
        my_free(ctx);
        return NULL;
    }

    ctx->maintain_id = sched_handle_alloc(1);
    if (ctx->maintain_id == NULL) {
        sched_handle_free(ctx->rtt_id);
        my_free(ctx);
        return NULL;
    }

    INIT_LIST_HEAD(&ctx->upload);
    INIT_LIST_HEAD(&ctx->alive);
    INIT_LIST_HEAD(&ctx->killed);
    INIT_LIST_HEAD(&ctx->age);
    resend_buffer_reset(&ctx->resend[0]);
    resend_buffer_reset(&ctx->resend[1]);

    init_book(&ctx->book);
    int32_t n = address_new(&ctx->book, ip, &port[1], port[0], 0);
    if (n == -1) {
        sched_handle_free(ctx->maintain_id);
        sched_handle_free(ctx->rtt_id);
        my_free(ctx);
        return NULL;
    }

    ctx->addr = sockaddr_get(&ctx->book, NULL);
    assert(ctx->addr != NULL);
    ctx->life[0] = 0;
    ctx->life[1] = 20;

    ctx->lck = lock_val;
    ctx->sps_lck = lock_val;
    ctx->sid = sid;
    ctx->dont_play = dont_play;
    ctx->token[0] = ctx->token[1] = 0;
    ctx->fptr = fptr;
    ctx->rc4 = *rc4;
    ctx->uid = encrypt_uid(ctx, uid);
    ctx->sps = NULL;
    ctx->last_read = now();

    for (int i = 0; i < elements(ctx->rtt); ++i) {
        ctx->rtt[i] = 0;
    }
    udp_rtt_init(&ctx->rtt_req, ctx->uid, sid, token);

    ctx->fds.fd = my_socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->fds.fd == -1) {
        sockaddr_put(&ctx->book, ctx->addr);
        sockaddr_delete(&ctx->book, 0);
        sched_handle_free(ctx->maintain_id);
        sched_handle_free(ctx->rtt_id);
        my_free(ctx);
        return NULL;
    }

    ctx->fds.tcp0_udp1 = 1;
    ctx->fds.mask = EPOLLIN;
    ctx->fds.bytes = udp_packet_bytes;
    ctx->fds.fop = &udp_link_ops;
    ctx->fds.priv = NULL;

    my_handle* handle = fdset_attach_fd(set, &ctx->fds);
    if (handle == NULL) {
        my_close(ctx->fds.fd);
        sockaddr_put(&ctx->book, ctx->addr);
        sockaddr_delete(&ctx->book, 0);
        sched_handle_free(ctx->maintain_id);
        sched_handle_free(ctx->rtt_id);
        my_free(ctx);
    } else {
        handle_clone(handle);
        ctx->self = handle;

        int err = fdset_sched(handle, 10, ctx->rtt_id);
        if (err == 0) {
            ctx->rtt_id = NULL;
        }
    }
    return handle;
}

void* udp_lock_handle(my_handle* handle)
{
    void* ptr = handle_get(handle);
    if (ptr == NULL) {
        errno = EBADF;
        return NULL;
    }

    struct fd_struct* fds = fd_struct_from(ptr);
    my_assert(fds);
    return fds;
}

void udp_unlock_handle(my_handle* handle)
{
    handle_put(handle);
}

int32_t udp_pull_audio(void* id, struct list_head* headp, const fraction* desire)
{
    return cache_pull((my_handle *) id, headp, desire);
}

void udp_close_audio(void* id)
{
    handle_dettach((my_handle *) id);
}

void udp_clear_nofify_fptr(my_handle* handle)
{
    void* ptr = handle_get(handle);
    if (ptr == NULL) {
        return;
    }

    struct fd_struct* fds = fd_struct_from(ptr);
    udp_context* ctx = (udp_context *) fds;
    lock(&ctx->lck);
    if (ctx->fptr.fptr != NULL) {
        ctx->fptr.fptr = NULL;
    }
    unlock(&ctx->lck);
    handle_put(handle);
}
