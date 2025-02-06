
#ifndef netjointor_h
#define netjointor_h

#include "mydef.h"
#include "jointor.h"
#include "my_buffer.h"
#include "netdef.h"
#include "media_addr.h"

capi void* media_client_open(void* set, struct tcp_addr* addr, uint32_t ms,
                             notify_fptr fptr, uint32_t dont_play, uint32_t dont_upload, uint32_t login_level);
capi jointor* jointor_get(void* any);
capi int32_t media_client_silence(void* any, uint32_t remote, uint32_t on);
capi int32_t media_client_enable_dtx(void* any, uint32_t on);
capi int32_t media_client_push_audio(void* any, struct my_buffer* mbuf, uint32_t group_size);
capi int32_t media_client_push_video(void* any, struct my_buffer* mbuf);
capi void media_client_close(void* any, int32_t code);

#endif
