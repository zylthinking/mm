
#ifndef sortcache_h
#define sortcache_h

#include "mydef.h"
#include "fraction.h"
#include "my_buffer.h"
#include "my_handle.h"
#include "media_buffer.h"

typedef struct {
    uint32_t gopidx;
    union {
        struct {
            uint16_t lost;
            uint16_t delay;
        } s;
        uint32_t expire;
    };
} jitter_info_t;
#define offset_field(media) media->vp[0].height
#define reflost_flag 0xff

typedef struct {
    uint32_t aud0;
    uint32_t recv;
    uint32_t lost;
    uint32_t late;
    uint32_t dup;
    uint32_t drop;
} cache_probe;

typedef struct push_reval {
    int32_t ret;
    uint32_t seq;
} push_retval;

capi my_handle* cache_create(uint32_t latch, media_buffer* media, uint32_t audio_frames);
capi push_retval* cache_push(my_handle* handle, struct my_buffer* mbuf, uint32_t rtt);
capi void cache_set_resend_count(my_handle* handle, uint32_t nr);
capi cache_probe* cache_probe_get(my_handle* handle);
capi int32_t cache_pull(my_handle* handle, struct list_head* headp, const fraction* f);
capi void cache_shutdown(my_handle* handle);

#endif
