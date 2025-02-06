
#include "ff_buffer.h"
#include "mbuf.h"
#include "media_buffer.h"

static struct my_buffer* ff_mbuf_clone(struct my_buffer* mbuf)
{
    ff_payload_t* payload = payload_of(mbuf);
    mbuf = payload->mop->clone(mbuf);
    if (mbuf != NULL) {
        __sync_add_and_fetch(&payload->refs, 1);
    }
    return mbuf;
}

static void ff_mbuf_free(struct my_buffer* mbuf)
{
    ff_payload_t* payload = payload_of(mbuf);
    intptr_t n = __sync_sub_and_fetch(&payload->refs, 1);
    if (n == 0) {
        av_free_packet(&payload->packet);
    }
    payload->mop->free(mbuf);
}

static struct mbuf_operations mop = {
    .clone = ff_mbuf_clone,
    .free = ff_mbuf_free
};

ff_payload_t* payload_of(struct my_buffer* mbuf)
{
    ff_payload_t* payload = (ff_payload_t *) (mbuf->ptr[0] + sizeof(media_buffer));
    return payload;
}

struct my_buffer* ff_buffer_alloc()
{
    struct my_buffer* mbuf = mbuf_alloc_2(sizeof(media_buffer) + sizeof(ff_payload_t));
    if (mbuf == NULL) {
        return NULL;
    }

    mbuf->ptr[1] += mbuf->length;
    mbuf->length = 0;

    ff_payload_t* payload = payload_of(mbuf);
    av_init_packet(&payload->packet);
    payload->packet.data = NULL;
    payload->packet.size = 0;
    payload->refs = 1;

    payload->mop = mbuf->mop;
    mbuf->mop = &mop;
    return mbuf;
}
