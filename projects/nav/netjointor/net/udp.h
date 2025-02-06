

#ifndef udp_h
#define udp_h

#include "mydef.h"
#include "my_handle.h"
#include "my_buffer.h"
#include "rc4.h"
#include "fraction.h"
#include "netdef.h"

capi my_handle* udp_new(void* set, uint32_t ip, uint16_t* port, uint32_t sid, uint64_t uid,
                        uint32_t token, uint32_t dont_play, rc4_stat* rc4, notify_fptr fptr);
capi void udp_clear_nofify_fptr(my_handle* handle);
capi void udp_drop_stream(my_handle* handle, uint32_t drop);
capi uint32_t udp_no_response();

capi void* udp_lock_handle(my_handle* handle);
capi void udp_unlock_handle(my_handle* handle);
capi void udp_push_audio(void* something, struct my_buffer* mbuf, uint32_t encrypt);
capi int32_t udp_push_video(void* something, struct my_buffer* sps, struct my_buffer* mbuf, uint32_t encrypt, uint32_t renew);

capi int32_t udp_pull_audio(void* id, struct list_head* headp, const fraction* desire);
capi void udp_close_audio(void* id);

#endif
