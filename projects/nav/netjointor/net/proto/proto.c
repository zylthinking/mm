
#include "mbuf.h"
#include <string.h>
#include "../tcp.h"
#include "fmt_in.h"
#include "mem.h"
#include "now.h"
#include "proto.h"

#ifndef _MSC_VER
#include <arpa/inet.h>
#endif
#define magic 0x4851

void tcp_proto_ping_req(tcp_ping* ping,  uint64_t uid, uint32_t sid)
{
    ping->header.tag = magic;
    ping->header.ctx = 0;
    ping->header.sid = sid;
    ping->header.flag = 0;
    ping->header.pcode = htonl(P_SESS_REQ_HEARTBEAT);
    ping->header.bytes = htonl(sizeof(tcp_list) - tcp_header_bytes);
    ping->uid = uid;
}

void tcp_list_req(tcp_list* list, uint64_t uid, uint32_t sid)
{
    list->header.tag = magic;
    list->header.ctx = 0;
    list->header.sid = sid;
    list->header.flag = 0;
    list->header.pcode = htonl(P_SESS_REQ_USER_LIST);
    list->header.bytes = htonl(sizeof(tcp_ping) - tcp_header_bytes);
    list->uid = uid;
}

void tcp_leave_req(tcp_leave* leave, uint64_t uid, uint32_t sid, uint32_t code)
{
    leave->header.tag = magic;
    leave->header.ctx = 0;
    leave->header.sid = sid;
    leave->header.flag = 0;
    leave->header.pcode = htonl(P_SESS_REQ_LEAVE_CHANNEL);
    leave->header.bytes = htonl(sizeof(tcp_leave) - tcp_header_bytes);
    leave->uid = uid;
    leave->reason = code;
}

void proto_req_join_channel(join_channel* join, uint64_t uid,
                            uint32_t sid, uint32_t token, uint32_t level)
{
    join->header.tag = magic;
    join->header.ctx = 0;
    join->header.sid = sid;
    join->header.flag = 0;
    join->header.pcode = htonl(P_SESS_REQ_JOIN_CHANNEL);
    join->header.bytes = htonl(sizeof(join_channel) - tcp_header_bytes);

    join->uid = uid;
    join->token = token;
    join->level = (level == 0 ? 6 : 0);
    join->left = htons(3);
    join->len = htons(1);
    join->encrypt = 1;
}

int32_t proto_parsel_header(char* pch, tcp_proto_header* header)
{
    header->tag = *(uint16_t *) pch;
    if (header->tag != magic) {
        return -1;
    }
    header->flag = ntohs(*(uint16_t *) (pch + 2));
    header->ctx = *(uint32_t *) (pch + 4);
    header->pcode = ntohl(*(uint32_t *) (pch + 8));
    header->sid = *(uint32_t *) (pch + 12);
    header->bytes = ntohl(*(uint32_t *) (pch + 16));
    return 0;
}

void join_channel_resp_free(join_channel_resp* resp)
{
    my_free(resp);
}

join_channel_resp* proto_join_response_parsel(struct my_buffer* mbuf, uintptr_t address)
{
    char* pch = mbuf->ptr[1];
    join_channel_resp* resp = (join_channel_resp *) my_malloc(sizeof(join_channel_resp));
    if (resp == NULL) {
        return NULL;
    }

    char ip[64] = {0};
    resp->result = ntohl(*(uint32_t *) mbuf->ptr[1]);
    pch += (sizeof(uint32_t));

    uint16_t len = ntohs(*(uint16_t *) pch);
    pch += sizeof(uint16_t);
    if (len == 0 || address == 0) {
        resp->ip = (uint32_t) -1;
        resp->port = NULL;
        return resp;
    }

    memcpy(ip, pch, len);
    resp->ip = inet_addr(ip);
    pch += len;

    uint16_t port = *(uint16_t *) pch;
    pch += sizeof(uint16_t);

    uint16_t nr = (uint16_t) (ntohl(*(uint32_t *) pch) + 2);
    pch += sizeof(uint32_t);

    resp->port = (uint16_t *) my_malloc(nr * sizeof(uint16_t));
    if (resp->port == NULL) {
        join_channel_resp_free(resp);
        return NULL;
    }

    resp->port[0] = nr - 1;
    resp->port[1] = port;
    for (int i = 2; i < nr; ++i) {
        resp->port[i] =  *(uint16_t *) (pch + 2);
        pch += sizeof(uint32_t);
    }
    return resp;
}

uint16_t proto_udp_header_parsel(struct my_buffer* mbuf)
{
    udp_proto_header* header = (udp_proto_header *) mbuf->ptr[1];
    header->pcode = ntohs(header->pcode);
    header->bytes = ntohs(header->bytes);
    return header->pcode;
}

void proto_udp_header_reencode(struct my_buffer* mbuf)
{
    udp_proto_header* header = (udp_proto_header *) mbuf->ptr[1];
    header->pcode = htons(header->pcode);
    header->bytes = htons(header->bytes);
}

struct my_buffer* udp_proto_resend_req(uintptr_t type, uint64_t uid, uint32_t sid, uint64_t who, uint32_t seq)
{
    struct my_buffer* req = mbuf_alloc_2(sizeof(udp_resend));
    if (req == NULL) {
        return NULL;
    }
    uint16_t bytes = req->length - udp_header_bytes;

    udp_resend* resend = (udp_resend *) req->ptr[1];
    resend->header.pcode = (type == video_type) ?  htons(P_VIDEO_RESEND_REQ) : htons(P_AUDIO_RESEND_REQ);
    resend->header.sid = sid;
    resend->header.uid = uid;
    resend->header.bytes = htons(bytes);
    resend->uid = who;
    resend->seq_from = htonl(seq);
    resend->seq_to = htonl(seq);
    return req;
}

void udp_proto_rtt_req(udp_rtt* rtt, uint64_t uid, uint32_t sid, uint32_t token, uint32_t current)
{
    rtt->header.pcode = htons(P_AUDIO_RTT_TEST);
    rtt->header.sid = sid;
    rtt->header.uid = uid;
    rtt->header.bytes = htons(8);
    rtt->token = token;
    rtt->tms = current;
}

struct my_buffer* udp_proto_audio_req(struct list_head* headp, uint32_t bytes,
                                      uint32_t frames, media_packet_info* info, uint8_t token)
{
#define meta_bytes sizeof(uint32_t) * 4
    uint32_t nb = sizeof(audio_packet) + bytes;
    struct my_buffer* mbuf = mbuf_alloc_2(roundup(nb, sizeof(uint32_t)) + meta_bytes);
    if (mbuf == NULL) {
        return NULL;
    }

    mbuf->length = nb;
    uint32_t* intp = resend_information(mbuf);
    intp[1] = 1;
    intp[2] = info->seq;
    intp[3] = 0;

    audio_packet* packet = (audio_packet *) mbuf->ptr[0];
    struct my_buffer* buf = list_entry(headp->next, struct my_buffer, head);
    media_buffer* media = (media_buffer *) buf->ptr[0];

    if (media->pptr_cc == &silk_8k16b1.cc) {
        packet->header.pcode = htons(P_AUDIO_SILK_8K);
        fraction frac = silk_8k16b1.ops->ms_from(&silk_8k16b1, frames, 1);
        intp[0] = frac.num / frac.den;
    } else if (media->pptr_cc == &silk_16k16b1.cc) {
        packet->header.pcode = htons(P_AUDIO_SILK_16K);
        fraction frac = silk_16k16b1.ops->ms_from(&silk_16k16b1, frames, 1);
        intp[0] = frac.num / frac.den;
    } else {
        my_assert(0);
    }

    packet->header.sid = info->sid;
    packet->header.uid = info->uid;
    packet->header.bytes = htons(mbuf->length - udp_header_bytes);
    packet->media.seq = htonl(info->seq);
    packet->media.stamp = htonl((uint32_t) media->pts);
    packet->media.token = token;
    packet->media.encrypt = 0;
    packet->media.lost = 0;

    nb = 0;
    audio_format* fmt = to_audio_format(media->pptr_cc);
    char* pch = mbuf->ptr[0] + sizeof(audio_packet);

    while (!list_empty(headp)) {
        struct list_head* ent = headp->next;
        list_del(ent);

        buf = list_entry(ent, struct my_buffer, head);
        if (!fmt->ops->frame_muted(buf)) {
            intp[3] = 1;
        }
        memcpy(&pch[nb], buf->ptr[1], buf->length);
        nb += buf->length;
        buf->mop->free(buf);
    }
    my_assert(nb == bytes);
    return mbuf;
#undef meta_bytes
}

struct my_buffer* udp_proto_video_req(uint64_t uid, uint32_t sid, media_buffer* media, char* payload,
                                      uint32_t bytes, uint8_t token, uint32_t seq[2], uint32_t gidx, uint32_t group, uintptr_t reflost)
{
#define meta_bytes sizeof(uint32_t) * 3
    uint32_t nb = sizeof(video_packet) + bytes;
    struct my_buffer* mbuf = mbuf_alloc_2(roundup(nb, sizeof(uint32_t)) + meta_bytes);
    if (mbuf == NULL) {
        return NULL;
    }

    mbuf->length = nb;
    uint32_t* intp = resend_information(mbuf);
    video_packet* packet = (video_packet *) mbuf->ptr[0];
    video_format* fmt = to_video_format(media->pptr_cc);

    if (fmt->pixel->size->width == 288 && fmt->pixel->size->height == 352) {
        packet->header.pcode = htons(P_VIDEO_H264_288x352);
    } else if (fmt->pixel->size->width == 352 && fmt->pixel->size->height == 288) {
        packet->header.pcode = htons(P_VIDEO_H264_352x288);
    } else if (fmt->pixel->size->width == 640 && fmt->pixel->size->height == 480) {
        packet->header.pcode = htons(P_VIDEO_H264_640x480);
    } else if (fmt->pixel->size->width == 480 && fmt->pixel->size->height == 640) {
        packet->header.pcode = htons(P_VIDEO_H264_480x640);
    } else if (fmt->pixel->size->width == 720 && fmt->pixel->size->height == 1080) {
        packet->header.pcode = htons(P_VIDEO_H264_720x1080);
    } else if (fmt->pixel->size->width == 1080 && fmt->pixel->size->height == 720) {
        packet->header.pcode = htons(P_VIDEO_H264_1080x720);
    } else if (fmt->pixel->size->width == 720 && fmt->pixel->size->height == 1280) {
        packet->header.pcode = htons(P_VIDEO_H264_720x1280);
    } else if (fmt->pixel->size->width == 1280 && fmt->pixel->size->height == 720) {
        packet->header.pcode = htons(P_VIDEO_H264_1280x720);
    } else if (fmt->pixel->size->width == 1080 && fmt->pixel->size->height == 1920) {
        packet->header.pcode = htons(P_VIDEO_H264_1080x1920);
    } else if (fmt->pixel->size->width == 1920 && fmt->pixel->size->height == 1080) {
        packet->header.pcode = htons(P_VIDEO_H264_1920x1080);
    } else if (fmt->pixel->size->width == 540 && fmt->pixel->size->height == 960) {
        packet->header.pcode = htons(P_VIDEO_H264_540x960);
    } else if (fmt->pixel->size->width == 960 && fmt->pixel->size->height == 540) {
        packet->header.pcode = htons(P_VIDEO_H264_960x540);
    }

    intp[0] = 1000 / ((uint32_t) fmt->caparam->fps);
    intp[1] = 1;
    intp[2] = seq[0] + gidx;

    packet->header.sid = sid;
    packet->header.uid = uid;
    packet->header.bytes = htons(mbuf->length - udp_header_bytes);
    packet->media.seq = htonl(intp[2]);
    packet->media.stamp = htonl((uint32_t) media->pts);
    packet->media.token = token;
    packet->media.encrypt = 0;
    packet->media.lost = 0;
    packet->vlc.gidx = htons((uint16_t) gidx);
    packet->vlc.group = htons((uint16_t) group);
    packet->frame = media->frametype;

    if (video_type_sps == packet->frame) {
        packet->sps.gop = htons(fmt->caparam->gop);
        packet->sps.fps = htons(fmt->caparam->fps);
        packet->sps.gop_mtus = (fmt->caparam->kbps * 1000 * fmt->caparam->gop) / (8 * fmt->caparam->fps * mtu) + fmt->caparam->gop * 0.8;
        packet->sps.gop_mtus = htons(packet->sps.gop_mtus);
        packet->gopidx1 = 0;
        packet->gopidx2 = 0;
    } else if (video_type_idr == packet->frame) {
        packet->vlc.offset = 0;
        packet->gopidx1 = gidx >> 16;
        packet->gopidx2 = gidx & 0xffff;
    } else if (video_type_i == packet->frame && reflost) {
        packet->vlc.offset = htons((uint16_t) reflost_indicator);
        uint32_t gopidx = intp[2] - seq[2];
        packet->gopidx1 = gopidx >> 16;
        packet->gopidx2 = gopidx & 0xffff;
    } else {
        my_assert(reflost == 0);
        packet->vlc.offset = htons((uint16_t) (intp[2] - seq[1]));
        uint32_t gopidx = intp[2] - seq[2];
        packet->gopidx1 = gopidx >> 16;
        packet->gopidx2 = gopidx & 0xffff;
    }
    packet->angle = (uint8_t) (media->angle & 0x0f);
    packet->gopidx2 = htons(packet->gopidx2);

    char* pch = mbuf->ptr[0] + sizeof(video_packet);
    memcpy(pch, payload, bytes);
    return mbuf;
#undef meta_bytes
}
