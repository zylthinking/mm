
#ifndef tcp_h
#define tcp_h

#include "mydef.h"
#include "my_buffer.h"
#include "proto/packet.h"
#include "netdef.h"
#include "fraction.h"
#include "media_addr.h"

typedef struct {
    void* set;
    struct tcp_addr* addr;
    uint32_t ms;
    notify_fptr fptr;
    uint32_t dont_play;
    uint32_t dont_upload;
    uint32_t login_level;
} tcp_new_args;

capi void* tcp_new(tcp_new_args* arg);
capi int32_t media_push(void* any, struct my_buffer* mbuf, uint32_t nr);

capi int32_t media_pull_stream(void* id, struct list_head* headp, const fraction* f);
capi void media_close_stream(void* id);

capi int32_t media_silence(void* any, uint32_t remote, uint32_t on);
capi int32_t media_enable_dtx(void* any, uint32_t on);
capi void tcp_delete(void* any, int32_t code);

#endif
