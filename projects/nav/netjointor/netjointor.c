
#include "netjointor.h"
#include "file_jointor.h"
#include "mem.h"
#include "net/tcp.h"

typedef struct {
    media_struct_t media;

    union {
        tcp_new_args* args;
        void* tcp_handle;
    } u;

    jointor* jointor;
    notify_fptr fptr;
    int32_t refs, code;
} context;

static void* net_open(void* any)
{
    context* nfc = (context *) any;
    nfc->u.tcp_handle = tcp_new(nfc->u.args);
    nfc->code = 0;
    if (nfc->u.tcp_handle == NULL) {
        return NULL;
    }
    nfc->media.any = no_stream;
    __sync_add_and_fetch(&nfc->refs, 1);
    return nfc;
}

static void net_close(void* id)
{
    context* nfc = (context *) id;
    tcp_delete(nfc->u.tcp_handle, nfc->code);
    if (0 == __sync_sub_and_fetch(&nfc->refs, 1)) {
        my_free(nfc);
    }
}

static media_operation_t netops = {
    net_open,
    media_pull_stream,
    net_close
};

// tcp_delete assure net_notify will not hit after it's returing
static int net_notify(void* ctx, uint32_t notify, void* any)
{
    context* nfc = (context *) ctx;
    if (notify_new_stream != notify) {
        nfc->fptr.fptr(nfc->fptr.ctx, notify, any);
        return 0;
    }

    if (nfc->jointor == NULL) {
        return -1;
    }
    return file_jointor_notify_stream(nfc->jointor, any, media_close_stream);
}

void* media_client_open(void* set, struct tcp_addr* addr,
                        uint32_t ms, notify_fptr fptr, uint32_t dont_play,
                        uint32_t dont_upload, uint32_t login_level)
{
    context* nfc = (context *) my_malloc(sizeof(context));
    if (nfc == NULL) {
        return NULL;
    }

    tcp_new_args args;
    args.set = set;
    args.addr = addr;
    args.ms = ms;
    args.fptr.fptr = net_notify;
    args.fptr.ctx = nfc;
    args.dont_play = dont_play;
    args.dont_upload = dont_upload;
    args.login_level = login_level;

    nfc->u.args = &args;
    nfc->fptr = fptr;
    nfc->jointor = NULL;
    nfc->media.ops = &netops;
    nfc->refs = 1;

    jointor* jointor = file_jointor_open(&nfc->media);
    if (jointor == NULL) {
        my_free(nfc);
        nfc = NULL;
    } else {
        nfc->jointor = jointor;
    }
    return nfc;
}

jointor* jointor_get(void* any)
{
    context* nfc = (context *) any;
    int n = nfc->jointor->ops->jointor_get(nfc->jointor);
    my_assert(n > 0);
    return nfc->jointor;
}

void media_client_close(void* any, int32_t code)
{
    context* nfc = (context *) any;
    nfc->code = code;
    file_jointor_close(nfc->jointor);
    if (0 == __sync_sub_and_fetch(&nfc->refs, 1)) {
        my_free(nfc);
    }
}

int32_t media_client_push_audio(void* any, struct my_buffer* mbuf, uint32_t group_size)
{
    context* nfc = (context *) any;
    return media_push(nfc->u.tcp_handle, mbuf, group_size);
}

int32_t media_client_silence(void* any, uint32_t remote, uint32_t on)
{
    context* nfc = (context *) any;
    return media_silence(nfc->u.tcp_handle, remote, on);
}

int32_t media_client_enable_dtx(void* any, uint32_t on)
{
    context* nfc = (context *) any;
    return media_enable_dtx(nfc->u.tcp_handle, on);
}

int32_t media_client_push_video(void* any, struct my_buffer* mbuf)
{
    context* nfc = (context *) any;
    return media_push(nfc->u.tcp_handle, mbuf, 1);
}

