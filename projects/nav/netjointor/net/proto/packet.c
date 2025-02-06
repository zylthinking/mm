
#include "packet.h"
#include "now.h"
#include "mbuf.h"
#include "media_buffer.h"
#include "my_errno.h"

uint32_t token_get_tcp()
{
    static int32_t token = 0;
    uint32_t n = 0;
    do {
        n = (uint32_t) __sync_add_and_fetch(&token, 1);
    } while (n == 0);
    return n;
}

uint32_t token_get_udp()
{
    static int32_t token = 0;
    return (uint32_t) __sync_add_and_fetch(&token, 1);
}


int32_t packet_idle(void* req)
{
    struct my_buffer* mbuf = (struct my_buffer *) req;

    char* pch = mbuf->ptr[1] + mbuf->length;
    uint32_t* idle = (uint32_t *) roundup((intptr_t) pch, 4);

    int32_t n = (int32_t) __sync_lock_test_and_set(idle, 0);
    return n;
}

static void idle_free(struct my_buffer* mbuf)
{
    mbuf->length += (uintptr_t) (mbuf->ptr[1] - mbuf->ptr[0]);
    mbuf->ptr[1] = mbuf->ptr[0];
    int32_t* idle = (int32_t *) (mbuf->ptr[1] + mbuf->length);
    *idle = 1;
}

static void mbuf_no_free(struct my_buffer* mbuf)
{
    mbuf->length += (uintptr_t) (mbuf->ptr[1] - mbuf->ptr[0]);
    mbuf->ptr[1] = mbuf->ptr[0];
}

static struct my_buffer* mbuf_no_clone(struct my_buffer* mbuf)
{
    (void) mbuf;
    my_assert(0);
    return NULL;
}

static struct mbuf_operations mop1 = {
    mbuf_no_clone, idle_free
};

static struct mbuf_operations mop2 = {
    mbuf_no_clone, mbuf_no_free
};

static void stock_proto_init(struct my_buffer* mbuf,
                             void* proto, size_t bytes, struct mbuf_operations* mop)
{
    INIT_LIST_HEAD(&mbuf->head);
    mbuf->mem_head = NULL;
    mbuf->length = (uint32_t) bytes;
    mbuf->ptr[0] = mbuf->ptr[1] = (char *) proto;
    mbuf->mop = mop;
}

//==================================================================

void tcp_ping_init(tcp_ping_request* req, uint64_t uid, uint32_t sid)
{
    stock_proto_init(&req->mbuf, &req->proto, sizeof(req->proto), &mop1);

    char* pch = req->mbuf.ptr[1] + req->mbuf.length;
    pch = (char *) roundup((intptr_t) pch, 4);
    my_assert(pch == (char *) &req->idle);
    req->idle = 1;

    tcp_proto_ping_req(&req->proto, uid, sid);
}

void tcp_list_init(tcp_list_request* req, uint64_t uid, uint32_t sid)
{
    stock_proto_init(&req->mbuf, &req->proto, sizeof(req->proto), &mop1);

    char* pch = req->mbuf.ptr[1] + req->mbuf.length;
    pch = (char *) roundup((intptr_t) pch, 4);
    my_assert(pch == (char *) &req->idle);
    req->idle = 1;

    tcp_list_req(&req->proto, uid, sid);
}

void tcp_leave_init(tcp_leave_request* req, uint64_t uid, uint32_t sid, uint32_t reason)
{
    stock_proto_init(&req->mbuf, &req->proto, sizeof(req->proto), &mop2);
    tcp_leave_req(&req->proto, uid, sid, reason);
}

void tcp_login_init(tcp_login_request* req, uint64_t uid,
                    uint32_t sid, uint32_t token, uint32_t level)
{
    stock_proto_init(&req->mbuf, &req->proto, sizeof(req->proto), &mop2);
    proto_req_join_channel(&req->proto, uid, sid, token, level);
}

//======================================================================

void udp_rtt_init(udp_rtt_request* req, uint64_t uid, uint32_t sid, uint32_t token)
{
    uint32_t current = now();
    if (sid != 0 || uid != 0 || token != 0) {
        stock_proto_init(&req->mbuf, &req->proto, sizeof(req->proto), &mop1);

        char* pch = req->mbuf.ptr[1] + req->mbuf.length;
        pch = (char *) roundup((intptr_t) pch, 4);
        my_assert(pch == (char *) &req->idle);
        req->idle = 1;

        udp_proto_rtt_req(&req->proto, uid, sid, token, current);
    } else {
        req->proto.tms = current;
    }
}

struct my_buffer* udp_resend_req(uintptr_t type, uint64_t uid, uint32_t sid, uint64_t who, uint32_t seq)
{
    errno = ENOMEM;
    return udp_proto_resend_req(type, uid, sid, who, seq);
}

static uint32_t group_mbuf(struct list_head* head1, struct list_head* head2, uint32_t size)
{
    uint32_t bytes = 0;

    while (size > 0) {
        struct list_head* ent = head2->next;
        list_del(ent);

        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        bytes += mbuf->length;

        list_add_tail(ent, head1);
        --size;
    }

    return bytes;
}

struct my_buffer* udp_audio_packet(struct list_head* headp, uint32_t total, uint32_t size,
                                   media_packet_info* info, uint8_t token, uint32_t enable_dtx)
{
    struct list_head head1, head2;
    INIT_LIST_HEAD(&head1);
    INIT_LIST_HEAD(&head2);
    struct my_buffer* mbuf = NULL;

    while (total >= size) {
        uint32_t bytes = group_mbuf(&head1, headp, size);
        total -= size;
        mbuf = udp_proto_audio_req(&head1, bytes, size, info, token);
        if (mbuf == NULL) {
            free_buffer(&head1);
            continue;
        }

        uint32_t* intp = (uint32_t *) (mbuf->ptr[1] + mbuf->length);
        if (enable_dtx && intp[3] == 0) {
            mbuf->mop->free(mbuf);
            mbuf = NULL;
            continue;
        }

        info->seq++;
        list_add_tail(&mbuf->head, &head2);
    }

    if (!list_empty(&head2)) {
        mbuf = list_entry(head2.next, struct my_buffer, head);
        list_del(&head2);
    }
    return mbuf;
}

struct my_buffer* udp_video_packet(struct my_buffer* mbuf, uint64_t uid, uint32_t sid, uint8_t token, uint32_t seq[3], uintptr_t reflost)
{
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    uint32_t length = (uint32_t) media->vp[0].stride;
    uint32_t nr = (length + mtu - 1) / mtu;
    uint32_t bytes = (length + nr - 1) / nr;
    char* pch = media->vp[0].ptr;

    struct list_head head;
    INIT_LIST_HEAD(&head);
    uint32_t i = 0;

    for (; i < nr; ++i) {
        uint32_t nb = my_min(bytes, length);
        mbuf = udp_proto_video_req(uid, sid, media, pch, nb, token, seq, i, nr, reflost);
        if (mbuf == NULL) {
            break;
        }

        pch += nb;
        length -= nb;
        list_add_tail(&mbuf->head, &head);
    }

    if (i != nr) {
        free_buffer(&head);
        mbuf = NULL;
    } else {
        if (reference_able(media->frametype)) {
            seq[1] = seq[0];
            if (video_type_idr == media->frametype) {
                seq[2] = seq[0];
            }
        }
        seq[0] += nr;

        mbuf = list_entry(head.next, struct my_buffer, head);
        list_del(&head);
    }

    return mbuf;
}
