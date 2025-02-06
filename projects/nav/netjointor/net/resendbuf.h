
#ifndef resendbuf_h
#define resendbuf_h

#include "list_head.h"
#include "my_buffer.h"
#include "mydef.h"
#include "lock.h"

typedef struct {
    lock_t lck;
    struct my_buffer** cache;
    uint32_t seq[2];
    uint32_t capacity;
} resend_buffer;

#define resend_need_init(resend) ((resend)->capacity == 0)
capi intptr_t resend_buffer_init(resend_buffer* cache, uint32_t capacity);
capi void resend_buffer_push_audio(resend_buffer* cache, struct my_buffer* mbuf);
capi void resend_buffer_push(resend_buffer* cache, struct my_buffer* mbuf, uint32_t group);
capi void resend_buffer_pull(resend_buffer* cache, uint32_t from, uint32_t to, struct list_head* headp);
capi void resend_buffer_reset(resend_buffer* cache);

#endif
