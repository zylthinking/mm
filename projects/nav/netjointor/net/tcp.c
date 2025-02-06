
#include "tcp.h"
#include "mbuf.h"
#include "my_handle.h"
#include "my_errno.h"
#include "lock.h"
#include "now.h"
#include "fdset_in.h"
#include "rc4.h"
#include "proto/proto.h"
#include "udp.h"
#include <pthread.h>
#include <string.h>

#ifndef _MSC_VER
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define ping_failed 6300
#define encrypt_tcp(keyp, type, proto) \
do { \
    type* ptr = proto; \
    ptr->header.flag = htons(F_ENCRYPT); \
    char* buf = ((char *) ptr) + tcp_header_bytes; \
    rc4(keyp, buf, sizeof(type) - tcp_header_bytes); \
} while (0)

enum read_step {invalid, header, content};

typedef struct {
    uintptr_t step;
    struct my_buffer* mbuf;

    uintptr_t reflost;
    // seq[0]: packet seq
    // seq[1]: previous referenceable seq
    uint32_t seq[3];
} vcb_t;

typedef struct {
    struct list_head media;
    uint32_t media_nr;
    uint32_t seq;
} acb_t;

typedef struct {
    lock_t lck;

    void* fset;
    my_handle* handle;
    notify_fptr fptr;

    uint64_t uid;
    struct tcp_addr* addr[2];
    struct list_head upload;

    vcb_t vcb;
    acb_t acb;

    uint8_t token[2];
    uint8_t logout;
    uint32_t dont_upload;
    uint32_t enable_dtx;
    uint32_t dont_play;

    uint8_t encrypt;
    rc4_stat crypt[3];
    uint32_t login_level;
    tcp_login_request login;

    tcp_ping_request ping;
    tcp_list_request list;
    tcp_leave_request leave;
    uint32_t* ping_id;
    uint32_t hp;
    uint32_t timer;

    my_handle* udp_handle;
    uint32_t udp_ip;
    uint16_t* udp_port;

    enum read_step step;
    tcp_proto_header header;
} tcp_context;

#include "conn.h"
static int reconnect(my_handle* ctx_handle, uint32_t ms);

static void connect_udp_if_needed(tcp_context* ctx, struct fd_struct* fds)
{
    if (ctx->udp_ip == (uint32_t) -1) {
        return;
    }

    lock(&ctx->lck);
    if (ctx->udp_handle != NULL) {
        void* ptr = handle_get(ctx->udp_handle);
        if (ptr != NULL) {
            handle_put(ctx->udp_handle);
            unlock(&ctx->lck);
            return;
        }

        handle_release(ctx->udp_handle);
        ctx->udp_handle = NULL;
    }
    unlock(&ctx->lck);

    struct tcp_addr* addr = ctx->addr[0];
    my_handle* handle = udp_new(ctx->fset, ctx->udp_ip, ctx->udp_port, addr->sid,
                                ctx->uid, addr->token, ctx->dont_play, &ctx->crypt[2], ctx->fptr);
    if (handle == NULL) {
        return;
    }

    lock(&ctx->lck);
    ctx->udp_handle = handle;
    unlock(&ctx->lck);
}

static int32_t join_response_arrive(tcp_context* ctx,
                                    struct fd_struct* fds, struct my_buffer* packet)
{
    uint64_t n = (ctx->login_level == 2);
    join_channel_resp* resp = proto_join_response_parsel(packet, (uintptr_t) n);
    if (resp == NULL) {
        return -1;
    }

    if (resp->result != PROTO_RES_SUCCESS) {
        join_channel_resp_free(resp);
        return -1;
    }

    lock(&ctx->lck);
    if (ctx->fptr.fptr != NULL) {
        ctrace(ctx->fptr.fptr((void *) ctx->fptr.ctx, notify_login, &n));
    }
    unlock(&ctx->lck);

    ctx->logout = 1;
    ctx->udp_ip = resp->ip;
    ctx->udp_port = resp->port;

    for (intptr_t i = 1; (ctx->udp_port != NULL && i < ctx->udp_port[0] + 1); ++i) {
        logmsg_d("udp ip: 0x%x, port: 0x%x in be\n", ctx->udp_ip, ctx->udp_port[i]);
    }
    join_channel_resp_free(resp);
    return 0;
}

static void do_notify_user_stat(tcp_context* ctx, usr_stat_t* stat)
{
    if (ctx->uid == stat->uid) {
        return;
    }

    lock(&ctx->lck);
    if (ctx->fptr.fptr != NULL) {
        stat->uid = ntohll(stat->uid);
        logmsg_d("uid %lld, stat %lld, reason %lld\n", stat->uid, stat->stat, stat->reason);
        ctrace(ctx->fptr.fptr((void *) ctx->fptr.ctx, notify_user_stat, stat));
    }
    unlock(&ctx->lck);
}

static int32_t get_user_list(tcp_context* ctx,
                             struct fd_struct* fds, struct my_buffer* packet)
{
    if (packet->length < 4) {
        return 0;
    }

    char* pch = packet->ptr[1];
    int64_t nr = (int64_t) ntohl(*(int32_t *) pch);
    pch += sizeof(int32_t);

    lock(&ctx->lck);
    if (ctx->fptr.fptr != NULL) {
        ctrace(ctx->fptr.fptr((void *) ctx->fptr.ctx, notify_user_count, &nr));
    }
    unlock(&ctx->lck);

    usr_stat_t stat;
    stat.reason = 0;
    for (int32_t i = 0; i < nr; ++i) {
        // use memcpy to avoid arm align issues
        memcpy(&stat.uid, pch, sizeof(uint64_t));
        pch += sizeof(uint64_t);

        stat.stat = (uint64_t) (int8_t) pch[0];
        // ignore another int8_t field(can_talk)
        // this line breaks the arm align requirment.
        pch += sizeof(int8_t) * 2;
        do_notify_user_stat(ctx, &stat);
    }

    nr = (int64_t) ntohl(*(int32_t *) pch);
    pch += sizeof(int32_t);

    for (int32_t i = 0; i < nr; ++i) {
        uint16_t bytes = ntohs(*(uint16_t *) pch);
        my_assert(bytes == 3);
        ctx->encrypt &= pch[4];
        pch += bytes + sizeof(uint16_t);
    }
    return 0;
}

static int32_t ustat_notify(tcp_context* ctx,
                            struct fd_struct* fds, struct my_buffer* packet)
{
    if (packet->length != 9 && packet->length != 17) {
        return 0;
    }

    int64_t uid = *(int64_t *) (packet->ptr[1]);
    int64_t stat = *(int8_t *) (packet->ptr[1] + 8);

    uint32_t reason = 0;
    if (packet->length == 17) {
        reason = *(int32_t *) (packet->ptr[1] + 13);
        reason = ntohl(reason);
    }

    usr_stat_t args;
    args.uid = uid;
    args.stat = stat;
    args.reason = reason;
    do_notify_user_stat(ctx, &args);
    return 0;
}

static void shutdown_udp(tcp_context* ctx)
{
    lock(&ctx->lck);
    if (ctx->udp_handle != NULL) {
        handle_dettach(ctx->udp_handle);
        ctx->udp_handle = NULL;
    }
    unlock(&ctx->lck);
}

static int32_t packet_arrive(tcp_context* ctx,
            struct fd_struct* fds, struct my_buffer* packet, uint32_t code)
{
    ctx->hp = ((uint32_t) -1);
    if (ctx->udp_handle != NULL && udp_no_response(ctx->udp_handle, now())) {
        logmsg("reconnect udp\n");
        shutdown_udp(ctx);
    }
    int32_t n = 0;

    switch (code) {
        case P_SESS_RESP_JOIN_CHANNEL:
            n = join_response_arrive(ctx, fds, packet);
            if (n == 0 && fds->bye == NULL) {
                fds->bye = &ctx->leave.mbuf;
                lock(&ctx->lck);
                list_add_tail(&ctx->list.mbuf.head, &ctx->upload);
                fds->mask |= EPOLLOUT;
                unlock(&ctx->lck);

                // this function only failed when the connection
                // has broken, which the failure does not matter.
                int err = fdset_sched(ctx->handle, 10, ctx->ping_id);
                if (err == 0) {
                    ctx->ping_id = NULL;
                }
            }
            break;
        case P_SESS_REPORT_USER_STATE:
            n = ustat_notify(ctx, fds, packet);
            break;
        case P_SESS_RESP_USER_LIST:
            n = get_user_list(ctx, fds, packet);
            break;
        default:
            logmsg_d("get unknown pcode %d\n", code);
        case P_SESS_RESP_HEARTBEAT:
        case P_SESS_BROADCAST_MSG:
            break;
    }

    if (packet != NULL) {
        packet->mop->free(packet);
    }
    return n;
}

static void tcp_break_link(tcp_context* ctx)
{
    my_handle* handle = (my_handle *) __sync_lock_test_and_set(&ctx->handle, NULL);
    if (handle != NULL) {
        handle_dettach(handle);
    }
}

static int32_t tcp_read(struct fd_struct* fds, struct my_buffer* mbuf, int err, struct sockaddr_in* in)
{
    (void) (in);
    my_handle* handle = (my_handle *) fds->priv;
    tcp_context* ctx = (tcp_context *) handle_get(handle);
    if (ctx == NULL) {
        if (mbuf != NULL) {
            mbuf->mop->free(mbuf);
        }
        return -1;
    }

    if (err != 0) {
        if (mbuf != NULL) {
            mbuf->mop->free(mbuf);
        }
        tcp_break_link(ctx);
        handle_put(handle);
        return -1;
    }

    if (ctx->step == header) {
        int32_t n = proto_parsel_header(mbuf->ptr[1], &ctx->header);
        mbuf->mop->free(mbuf);
        if (n == -1) {
            tcp_break_link(ctx);
            handle_put(handle);
            return -1;
        }
        ctx->step = content;

        handle_put(handle);
        return ctx->header.bytes;
    }

    if (ctx->header.flag & F_ENCRYPT) {
        ctx->crypt[1] = ctx->crypt[2];
        rc4(&ctx->crypt[1], mbuf->ptr[1], (int) mbuf->length);
    }

    int n = packet_arrive(ctx, fds, mbuf, ctx->header.pcode);
    if (n == -1) {
        tcp_break_link(ctx);
        handle_put(handle);
        return -1;
    }

    ctx->step = header;
    handle_put(handle);
    return (int32_t) sizeof(tcp_proto_header);
}

static struct my_buffer* tcp_write(struct fd_struct* fds, struct sockaddr_in** in, int* err)
{
    (void) (in);
    my_handle* handle = (my_handle *) fds->priv;
    tcp_context* ctx = (tcp_context *) handle_get(handle);
    if (ctx == NULL) {
        return NULL;
    }

    if (err != NULL) {
        tcp_break_link(ctx);
        handle_put(handle);
        return NULL;
    }

    struct my_buffer* mbuf = NULL;
    lock(&ctx->lck);
    struct list_head* ent = ctx->upload.next;
    if (!list_empty(ent)) {
        list_del(ent);
        mbuf = list_entry(ent, struct my_buffer, head);
    } else {
        fds->mask &= (~EPOLLOUT);
    }
    unlock(&ctx->lck);

    handle_put(handle);
    return mbuf;
}

static void join_channel_init(tcp_context* ctx)
{
    struct tcp_addr* addr = ctx->addr[0];
    ctx->crypt[0] = ctx->crypt[2];
    tcp_login_init(&ctx->login, addr->uid, addr->sid, addr->token, ctx->login_level);
    encrypt_tcp(&ctx->crypt[0], join_channel, &ctx->login.proto);
}

void tcp_notify(struct fd_struct* fds, uint32_t* id)
{
    my_handle* handle = (my_handle *) fds->priv;
    tcp_context* ctx = (tcp_context *) handle_get(handle);
    if (ctx == NULL) {
        return;
    }

    my_assert(ctx->ping_id == NULL);
    ctx->ping_id = id;
    ctx->timer = now();
    if (ctx->timer > ctx->hp) {
        tcp_break_link(ctx);
        handle_put(handle);
        return;
    }

    int32_t idle = packet_idle(&ctx->ping.mbuf);
    if (idle == 1) {
        if (ctx->hp == (uint32_t) -1) {
            ctx->hp = ctx->timer + ping_failed;
        }
        lock(&ctx->lck);
        list_add_tail(&ctx->ping.mbuf.head, &ctx->upload);
        fds->mask |= EPOLLOUT;
        unlock(&ctx->lck);
    } else {
        logmsg_d("%u: previous ping still not fired, current hp = %u\n", ctx->timer, ctx->hp);
    }

    if (ctx->login_level == 1) {
        ctx->login_level = 2;
        join_channel_init(ctx);
        lock(&ctx->lck);
        list_add_tail(&ctx->login.mbuf.head, &ctx->upload);
        fds->mask |= EPOLLOUT;
        unlock(&ctx->lck);
    }

    uint32_t ms = 1000;
    if (ctx->udp_handle == NULL || ctx->login_level != 2) {
        ms = 100;
    }

    int err = fdset_sched(ctx->handle, ms, ctx->ping_id);
    if (err == 0) {
        ctx->ping_id = NULL;
    } else {
        logmsg("fdset_sched(tcp_ping) failed, errno = %d\n", errno);
    }
    connect_udp_if_needed(ctx, fds);
    handle_put(handle);
}

static void reset_connection(tcp_context* ctx)
{
    free_buffer(&ctx->upload);
    ctx->hp = ((uint32_t) -1);
    ctx->timer = ((uint32_t) -1);

    shutdown_udp(ctx);
    if (ctx->udp_port != NULL) {
        my_free(ctx->udp_port);
        ctx->udp_port = NULL;
    }

    ctx->udp_ip = (uint32_t) -1;
    lock(&ctx->lck);
    if (ctx->fptr.fptr != NULL && ctx->logout == 1) {
        ctrace(ctx->fptr.fptr(ctx->fptr.ctx, notify_disconnected, NULL));
    }
    unlock(&ctx->lck);
    ctx->logout = 0;
    ctx->step = invalid;
}

static void tcp_context_free(void* addr)
{
    tcp_context* ctx = (tcp_context *) addr;
    tcp_break_link(ctx);

    my_assert(ctx->fptr.fptr == NULL);
    reset_connection(ctx);
    free_buffer(&ctx->acb.media);

    if (ctx->vcb.mbuf != NULL) {
        ctx->vcb.mbuf->mop->free(ctx->vcb.mbuf);
    }

    if (ctx->addr[0] != NULL) {
        tcp_addr_free(ctx->addr[0]);
    }

    if (ctx->ping_id != NULL) {
        sched_handle_free(ctx->ping_id);
    }

    if (ctx->udp_port != NULL) {
        // it's better the one release is who alloc
        // while, keep it bad ....
        my_free(ctx->udp_port);
    }

    handle_release((my_handle *) ctx->fset);
    my_free(ctx);
}

static void* reconn_thread(void* any)
{
    mark("thread start");
    my_handle* handle = (my_handle *) any;
    while (1) {
        int n = reconnect(handle, 10000);
        if (n == 0) {
            break;
        }

        if (errno == EBADF) {
            break;
        }
        logmsg("reconnect failed with errno %d\n", errno);
        usleep((useconds_t) (1000 * 1000));
    }
    handle_release(handle);
    return NULL;
}

static void start_reconnect(my_handle* handle)
{
    pthread_t tid;
    while (1) {
        tcp_context* ctx = (tcp_context *) handle_get(handle);
        if (ctx == NULL) {
            errno = EBADF;
            break;
        }

        if (ctx->ping_id == NULL) {
            ctx->ping_id = sched_handle_alloc(0);
            if (ctx->ping_id == NULL) {
                handle_put(handle);
                usleep(1000 * 20);
                continue;
            }
        }

        handle_clone(handle);
        int n = pthread_create(&tid, NULL, reconn_thread, handle);
        handle_put(handle);

        if (n == 0) {
            pthread_detach(tid);
            break;
        }
        handle_release(handle);
        usleep(1000 * 20);
    }
}

static void tcp_detach(struct fd_struct* fds)
{
    my_handle* handle = (my_handle *) fds->priv;

    my_close(fds->fd);
    my_free(fds);

    tcp_context* ctx = (tcp_context *) handle_get(handle);
    if (ctx != NULL) {
        uint32_t current = now();
        if (ctx->timer != (uint32_t) -1 &&
            current > ctx->timer + ping_failed)
        {
            logmsg("ping failed: %d\n", current - ctx->timer);
        }

        // assure ctx->handle to be NULL
        tcp_break_link(ctx);
        reset_connection(ctx);
        start_reconnect(handle);
        handle_put(handle);
    }
    handle_release(handle);
}

static struct fds_ops tcp_link_ops = {
    tcp_read,
    NULL,
    tcp_write,
    tcp_detach,
    tcp_notify,
    NULL
};

static void setup_fds(tcp_context* ctx, struct fd_struct* fds, my_handle* handle, struct my_buffer** mbufp)
{
    join_channel_init(ctx);
    list_add_tail(&ctx->login.mbuf.head, &ctx->upload);

    fds->tcp0_udp1 = 0;
    fds->mask = EPOLLIN | EPOLLOUT;
    fds->bytes = (uint32_t) sizeof(tcp_proto_header);
    fds->fop = &tcp_link_ops;
    fds->priv = (void *) ctx->handle;
    handle_clone(ctx->handle);

    ctx->step = header;
    ctx->handle = handle;
#if 0
    lock(&ctx->lck);
    if (ctx->fptr.fptr != NULL) {
        ctrace(ctx->fptr.fptr(ctx->fptr.ctx, notify_connected, NULL));
    }
    unlock(&ctx->lck);
#endif

    if (mbufp != NULL) {
        mbufp[0] = tcp_write(fds, NULL, NULL);
    }
}

static int reconnect(my_handle* ctx_handle, uint32_t ms)
{
    tcp_context* ctx = (tcp_context *) handle_get(ctx_handle);
    if (ctx == NULL) {
        errno = EBADF;
        return -1;
    }

    errno = EFAILED;
    ctx->handle = ctx_handle;
    // we have to execute setup_fds in network thread,
    // it is the only one safe place to be sure no reconnecting for the newly
    // connected connection
    int n = connect_media_server(ctx, ms, setup_fds);
    if (n == 0) {
        // handle_put may free ctx before setup_fds exits
        // have to wait to avoid a segment fault
        while ((volatile my_handle *) ctx->handle == ctx_handle) usleep(1000);
    } else {
        ctx->handle = NULL;
    }
    handle_put(ctx_handle);
    return n;
}

void* tcp_new(tcp_new_args* arg)
{
    errno = ENOMEM;
    tcp_context* ctx = (tcp_context *) my_malloc(sizeof(tcp_context));
    if (ctx == NULL) {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));

    my_handle* handle = handle_attach(ctx, tcp_context_free);
    if (handle == NULL) {
        my_free(ctx);
        return NULL;
    }

    INIT_LIST_HEAD(&ctx->upload);
    struct tcp_addr* addr = arg->addr;
    rc4_init(&ctx->crypt[2], (const char *) addr->keyp, addr->key_bytes);

    rc4_stat stat = ctx->crypt[2];
    tcp_ping_init(&ctx->ping, addr->uid, addr->sid);
    encrypt_tcp(&stat, tcp_ping, &ctx->ping.proto);
    ctx->hp = ((uint32_t) -1);
    ctx->timer = ((uint32_t) -1);

    stat = ctx->crypt[2];
    tcp_list_init(&ctx->list, addr->uid, addr->sid);
    encrypt_tcp(&stat, tcp_list, &ctx->list.proto);

    stat = ctx->crypt[2];
    tcp_leave_init(&ctx->leave, addr->uid, addr->sid, 0);
    encrypt_tcp(&stat, tcp_leave, &ctx->leave.proto);
    // disable leave when reconnect
    ctx->leave.mbuf.length = 0;

    INIT_LIST_HEAD(&ctx->acb.media);
    ctx->acb.media_nr = 0;
    ctx->acb.seq = 1200;

    ctx->vcb.step = 0;
    ctx->vcb.mbuf = NULL;
    ctx->vcb.reflost = 1;
    ctx->vcb.seq[0] = 1200;
    ctx->vcb.seq[1] = ctx->vcb.seq[2] = 0;

    ctx->dont_upload = arg->dont_upload;
    ctx->dont_play = arg->dont_play;
    ctx->enable_dtx = 0;
    ctx->login_level = arg->login_level ? 2 : 0;
    ctx->step = invalid;
    ctx->logout = 0;
    ctx->token[0] = ctx->token[1] = (uint8_t) token_get_tcp();
    ctx->encrypt = 0x01;

    ctx->fset = arg->set;
    handle_clone((my_handle *) ctx->fset);

    ctx->lck = lock_val;
    ctx->uid = addr->uid;
    ctx->addr[0] = addr;
    ctx->addr[1] = NULL;
    ctx->fptr = arg->fptr;
    ctx->udp_handle = NULL;
    ctx->udp_ip = (uint32_t) -1;
    ctx->udp_port = NULL;
    ctx->ping_id = sched_handle_alloc(0);

    int n = -1;
    if (ctx->ping_id != NULL) {
        n = reconnect(handle, arg->ms);
    }

    if (-1 == n) {
        // avoid call fptr
        ctx->fptr.fptr = NULL;
        // avoid free ctx->addr[0]
        ctx->addr[0] = NULL;
        handle_dettach(handle);
        return NULL;
    }
    return handle;
}

void tcp_delete(void* any, int32_t code)
{
    tcp_context* ctx = (tcp_context *) handle_get((my_handle *) any);
    // the only one may call handle_dettach for (my_handle *) any is here
    // because tcp_new returns (void *) my_handle, e.g. caller won't know
    // it is a my_handle, so, handle_clone will not be called on it.
    // so we can assert here.
    my_assert(ctx != NULL);
    code = htonl(code);
    tcp_leave_init(&ctx->leave, ctx->uid, ctx->addr[0]->sid, (uint32_t) code);
    rc4_stat stat = ctx->crypt[2];
    encrypt_tcp(&stat, tcp_leave, &ctx->leave.proto);
    ctx->leave.mbuf.length = sizeof(ctx->leave.proto);

    // we require lock here to assure
    // no callback after returning from this function
    lock(&ctx->lck);
    if (ctx->udp_handle != NULL) {
        udp_clear_nofify_fptr(ctx->udp_handle);
    }
    ctx->fptr.fptr = NULL;
    unlock(&ctx->lck);

    handle_put((my_handle *) any);
    handle_dettach((my_handle *) any);
}

static void push_media_packet(struct my_buffer* mbuf, uint32_t mtype, tcp_context* ctx)
{
    struct list_head head;
    list_add_tail(&head, &mbuf->head);

    while (!list_empty(&head)) {
        struct list_head* ent = head.next;
        list_del(ent);
        mbuf = list_entry(ent, struct my_buffer, head);

        my_handle* handle = NULL;
        void* ptr = NULL;

        lock(&ctx->lck);
        if (ctx->udp_handle != NULL) {
            handle = ctx->udp_handle;
            handle_clone(handle);
            ptr = udp_lock_handle(handle);
        }
        unlock(&ctx->lck);

        if (ptr != NULL) {
            if (audio_type == mtype) {
                udp_push_audio(ptr, mbuf, ctx->encrypt);
            } else {
                if (0 == udp_push_video(ptr, ctx->vcb.mbuf, mbuf, ctx->encrypt, ctx->vcb.step == 1)) {
                    ctx->vcb.step = 2;
                }
            }
            udp_unlock_handle(handle);
            handle_release(handle);
        } else {
            mbuf->mop->free(mbuf);
        }
    }
}

static struct my_buffer* audio_packet_make(tcp_context* ctx, struct my_buffer* mbuf, uint32_t nr)
{
    ctx->acb.media_nr += 1;
    list_add_tail(&mbuf->head, &ctx->acb.media);
    if (ctx->acb.media_nr < nr) {
        return NULL;
    }

    struct list_head head;
    list_add(&head, &ctx->acb.media);
    list_del_init(&ctx->acb.media);

    media_packet_info info;
    info.uid = ctx->uid;
    info.sid = ctx->addr[0]->sid;
    info.seq = ctx->acb.seq;

    mbuf = udp_audio_packet(&head, ctx->acb.media_nr, nr, &info, ctx->token[0], ctx->enable_dtx);
    ctx->acb.seq = info.seq;
    ctx->acb.media_nr = ctx->acb.media_nr % nr;
    return mbuf;
}

static struct my_buffer* video_packet_make(tcp_context* ctx, struct my_buffer* mbuf)
{
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    uint32_t type = media->frametype;
    if ((ctx->vcb.mbuf == NULL) ||
        (type != video_type_idr && type != video_type_i && ctx->vcb.reflost == 1))
    {
        mbuf->mop->free(mbuf);
        return NULL;
    }

    struct my_buffer* mbuf2 = NULL;
    if (ctx->vcb.step == 0) {
        ctx->token[1] = (uint8_t) token_get_tcp();
        mbuf2 = udp_video_packet(ctx->vcb.mbuf, ctx->uid, ctx->addr[0]->sid, ctx->token[1], ctx->vcb.seq, 0);
        if (mbuf2 == NULL) {
            goto LABEL;
        }
        // sps frame should be small enough
        my_assert(list_empty(&mbuf2->head));
        ctx->vcb.mbuf = mbuf2;
        ctx->vcb.step = 1;
    }

    mbuf2 = udp_video_packet(mbuf, ctx->uid, ctx->addr[0]->sid, ctx->token[1], ctx->vcb.seq, ctx->vcb.reflost);
    if (mbuf2 == NULL) {
        goto LABEL;
    }

LABEL:
    mbuf->mop->free(mbuf);
    if (mbuf2 == NULL) {
        if (reference_able(type)) {
            ctx->vcb.reflost = 1;
        }
    } else if (type == video_type_idr) {
         ctx->vcb.reflost = 0;
    }
    return mbuf2;
}

int32_t media_push(void* any, struct my_buffer* mbuf, uint32_t nr)
{
    errno = EBADF;
    tcp_context* ctx = (tcp_context *) handle_get((my_handle *) any);
    if (ctx == NULL) {
        return -1;
    }

    if (__builtin_expect(ctx->login_level == 0, 0)) {
        ctx->login_level = 1;
    }

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    uint32_t mtype = media_type(media->pptr_cc[0]);
    if (video_type == mtype) {
        if (media->frametype == video_type_sps) {
            if (ctx->vcb.mbuf == NULL) {
                my_assert(ctx->vcb.reflost == 1);
            } else {
                ctx->vcb.mbuf->mop->free(ctx->vcb.mbuf);
                ctx->vcb.reflost = 1;
            }
            ctx->vcb.step = 0;
            ctx->vcb.mbuf = mbuf;
            mbuf = NULL;
        } else if(ctx->login_level == 2) {
            mbuf = video_packet_make(ctx, mbuf);
        } else {
            mbuf->mop->free(mbuf);
            mbuf = NULL;
        }
    } else {
        if (ctx->dont_upload == 0) {
            mbuf = audio_packet_make(ctx, mbuf, nr);
        } else {
            mbuf->mop->free(mbuf);
            mbuf = NULL;
        }
    }

    if (__builtin_expect(mbuf != NULL, 1)) {
        push_media_packet(mbuf, mtype, ctx);
    }
    handle_put((my_handle *) any);
    return 0;
}

int32_t media_pull_stream(void* id, struct list_head* headp, const fraction* f)
{
    return udp_pull_audio(id, headp, f);
}

void media_close_stream(void* id)
{
    udp_close_audio((my_handle *) id);
}

int32_t media_silence(void* any, uint32_t remote, uint32_t on)
{
    errno = EBADF;
    tcp_context* ctx = (tcp_context *) handle_get((my_handle *) any);
    if (ctx == NULL) {
        return -1;
    }

    if (remote == 0) {
        ctx->dont_play = !!on;
        lock(&ctx->lck);
        if (ctx->udp_handle != NULL) {
            udp_drop_stream(ctx->udp_handle, ctx->dont_play);
        }
        unlock(&ctx->lck);
    } else {
        ctx->dont_upload = !!on;
    }

    handle_put((my_handle *) any);
    return 0;
}

int32_t media_enable_dtx(void* any, uint32_t on)
{
    errno = EBADF;
    tcp_context* ctx = (tcp_context *) handle_get((my_handle *) any);
    if (ctx == NULL) {
        return -1;
    }

    ctx->enable_dtx = !!on;
    handle_put((my_handle *) any);
    return 0;
}
