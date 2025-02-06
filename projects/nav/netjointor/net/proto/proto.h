

#ifndef proto_h
#define proto_h

#include "pcode.h"
#include "mydef.h"
#include "my_buffer.h"
#include "media_buffer.h"

#pragma pack(1)

typedef struct tag_tcp_proto_header {
    uint16_t tag;
    uint16_t flag;
    uint32_t ctx;
    uint32_t pcode;
    uint32_t sid;
    uint32_t bytes;
} tcp_proto_header;

typedef struct tag_tcp_ping {
    tcp_proto_header header;
    uint64_t uid;
} tcp_ping;

typedef struct tag_tcp_list {
    tcp_proto_header header;
    uint64_t uid;
} tcp_list;

typedef struct tag_tcp_leave {
    tcp_proto_header header;
    uint64_t uid;
    uint32_t reason;
} tcp_leave;

typedef struct tag_tcp_ustat {
    tcp_proto_header header;
    uint64_t uid;
    uint8_t stat;
    uint32_t reason;
} tcp_ustat;

typedef struct tag_join_channel {
    tcp_proto_header header;
    uint64_t uid;
    uint32_t token;
    uint8_t level;
    uint16_t left;
    uint16_t len;
    uint8_t encrypt;
} join_channel;

typedef struct tag_udp_proto_header {
    uint16_t pcode;
    uint32_t sid;
    uint64_t uid;
    uint16_t bytes;
} udp_proto_header;

typedef struct tag_udp_rtt {
    udp_proto_header header;
    uint32_t tms;
    uint32_t token;
} udp_rtt;

typedef struct tag_udp_resend {
    udp_proto_header header;
    uint64_t uid;
    uint32_t seq_from;
    uint32_t seq_to;
} udp_resend;

typedef struct {
    uint32_t seq;
    uint32_t stamp;
    uint8_t token;
    uint8_t encrypt;
    uint8_t lost;
} media_header_t;

typedef struct tag_audio_packet {
    udp_proto_header header;
    media_header_t media;
} audio_packet;

#define reflost_indicator 0xffff
#define mtu 1280

typedef struct tag_video_packet {
    udp_proto_header header;
    media_header_t media;
    uint8_t frame;

    union {
        struct {
            uint16_t offset;
            uint16_t gidx;
            uint16_t group;
        } vlc;

        struct {
            uint16_t gop;
            uint16_t fps;
            uint16_t gop_mtus;
        } sps;
    };

    uint8_t angle;
    uint8_t gopidx1;
    uint16_t gopidx2;
} video_packet;

#pragma pack()

typedef struct tag_join_channel_resp {
    int32_t result;
    uint32_t ip;
    uint16_t* port;
} join_channel_resp;

typedef struct tag_media_packet_info {
    uint64_t uid;
    uint32_t sid;
    uint32_t seq;
} media_packet_info;

#define resend_information(mbuf) ((uint32_t *) (mbuf->ptr[1] + roundup(mbuf->length, sizeof(uint32_t))))
#define tcp_header_bytes sizeof(tcp_proto_header)
#define udp_header_bytes sizeof(udp_proto_header)
#define F_ENCRYPT  0x0001
#define F_COMPRESS 0x0002

capi void tcp_leave_req(tcp_leave* leave, uint64_t uid, uint32_t sid, uint32_t code);
capi void tcp_list_req(tcp_list* list, uint64_t uid, uint32_t sid);
capi void tcp_proto_ping_req(tcp_ping* ping, uint64_t uid, uint32_t sid);
capi int32_t proto_parsel_header(char* pch, tcp_proto_header* header);

capi void proto_req_join_channel(join_channel* join, uint64_t uid,
                                 uint32_t sid, uint32_t token, uint32_t level);
capi join_channel_resp* proto_join_response_parsel(struct my_buffer* mbuf, uintptr_t address);
capi void join_channel_resp_free(join_channel_resp* resp);

capi void udp_proto_rtt_req(udp_rtt* rtt, uint64_t uid, uint32_t sid, uint32_t token, uint32_t current);
capi uint16_t proto_udp_header_parsel(struct my_buffer* mbuf);
capi void proto_udp_header_reencode(struct my_buffer* mbuf);
capi struct my_buffer* udp_proto_resend_req (uintptr_t type, uint64_t uid, uint32_t sid, uint64_t who, uint32_t seq);
capi struct my_buffer* udp_proto_audio_req(struct list_head* headp, uint32_t bytes,
                                           uint32_t frames, media_packet_info* info, uint8_t token);
capi struct my_buffer* udp_proto_video_req(uint64_t uid, uint32_t sid, media_buffer* media,
                                           char* payload, uint32_t bytes, uint8_t token,
                                           uint32_t seq[2], uint32_t gidx, uint32_t group, uintptr_t reflost);
#endif
