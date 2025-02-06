
#ifndef ff_buffer_h
#define ff_buffer_h

#include "mydef.h"
#include "libavcodec/avcodec.h"
#include "my_buffer.h"

typedef struct {
    intptr_t refs;
    AVPacket packet;
    struct mbuf_operations* mop;
} ff_payload_t;

capi struct my_buffer* ff_buffer_alloc();
capi ff_payload_t* payload_of(struct my_buffer* mbuf);

#endif
