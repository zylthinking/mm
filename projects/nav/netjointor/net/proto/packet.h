

#ifndef packet_h
#define packet_h

#include "list_head.h"
#include "proto.h"
#include "fdset_in.h"

typedef struct tag_tcp_ping_request {
    struct my_buffer mbuf;
    tcp_ping proto;
    uint32_t idle;
} tcp_ping_request;

typedef struct tag_tcp_list_request {
    struct my_buffer mbuf;
    tcp_list proto;
    uint32_t idle;
} tcp_list_request;

typedef struct tag_tcp_leave_request {
    struct my_buffer mbuf;
    tcp_leave proto;
} tcp_leave_request;

typedef struct tag_tcp_login_request {
    struct my_buffer mbuf;
    join_channel proto;
} tcp_login_request;

typedef struct tag_udp_rtt_request {
    struct my_buffer mbuf;
    udp_rtt proto;
    uint32_t idle;
} udp_rtt_request;

capi void tcp_list_init(tcp_list_request* req, uint64_t uid, uint32_t sid);
capi void tcp_leave_init(tcp_leave_request* req, uint64_t uid, uint32_t sid, uint32_t reason);
capi void tcp_ping_init(tcp_ping_request* req, uint64_t uid, uint32_t sid);

capi int32_t packet_idle(void* req);
capi void tcp_login_init(tcp_login_request* req, uint64_t uid, uint32_t sid, uint32_t token, uint32_t level);

capi void udp_rtt_init(udp_rtt_request* req, uint64_t uid, uint32_t sid, uint32_t token);
capi struct my_buffer* udp_resend_req(uintptr_t type, uint64_t uid, uint32_t sid, uint64_t who, uint32_t seq);

capi uint32_t token_get_tcp();
capi uint32_t token_get_udp();
capi struct my_buffer* udp_audio_packet(struct list_head* headp, uint32_t total, uint32_t size,
                                        media_packet_info* info, uint8_t token, uint32_t enable_dtx);
capi struct my_buffer* udp_video_packet(struct my_buffer* mbuf, uint64_t uid,
                                        uint32_t sid, uint8_t token, uint32_t seq[3], uintptr_t reflost);
#endif
