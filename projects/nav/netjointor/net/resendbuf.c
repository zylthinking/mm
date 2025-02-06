
#include "resendbuf.h"
#include "mem.h"
#include "lock.h"
#include <string.h>

static void resend_buffer_clear(resend_buffer* cache, uint32_t seq1, uint32_t seq2)
{
    for (uint32_t i = seq1; i < seq2; ++i) {
        uint32_t indx = i % cache->capacity;
        struct my_buffer* buf = cache->cache[indx];

        if (buf != NULL) {
            cache->cache[indx] = NULL;
            buf->mop->free(buf);
        }
    }
}

intptr_t resend_buffer_init(resend_buffer* cache, uint32_t capacity)
{
    uint32_t bytes = sizeof(struct my_buffer *) * capacity;
    cache->cache = (struct my_buffer **) my_malloc(bytes);
    if (cache->cache == NULL) {
        return -1;
    }

    cache->capacity = capacity;
    cache->seq[0] = cache->seq[1] = 0;
    cache->lck = lock_val;
    memset(cache->cache, 0, bytes);
    return 0;
}

void resend_buffer_push(resend_buffer* cache, struct my_buffer* mbuf, uint32_t seq)
{
    lock(&cache->lck);
    // seq wrapping will not take into account
    my_assert(seq >= cache->seq[1]);
    my_assert(cache->seq[1] >= cache->seq[0]);

    int32_t n = (int32_t) (seq - cache->seq[0] + 1) - (int32_t) cache->capacity;
    int32_t nr = my_min(n, (int32_t) cache->capacity);
    if (nr > 0) {
        resend_buffer_clear(cache, cache->seq[0], cache->seq[0] + nr);
        cache->seq[0] += n;
    }

    uint32_t indx = seq % cache->capacity;
    my_assert(cache->cache[indx] == NULL);
    cache->cache[indx] = mbuf;
    cache->seq[1] = seq + 1;
    unlock(&cache->lck);
}

void resend_buffer_pull(resend_buffer* cache, uint32_t from, uint32_t to, struct list_head* headp)
{
    my_assert(from < to);
    lock(&cache->lck);
    my_assert(cache->seq[1] >= cache->seq[0]);

    if (from < cache->seq[0]) {
        from = cache->seq[0];
    }

    if (to > cache->seq[1]) {
        to = cache->seq[1];
    }

    for (uint32_t n = from; n < to; ++n) {
        uint32_t i = n % cache->capacity;
        struct my_buffer* mbuf = cache->cache[i];
        if (mbuf != NULL) {
            struct my_buffer* mbuf2 = mbuf->mop->clone(mbuf);
            if (mbuf2 != NULL) {
                mbuf = mbuf2;
            } else {
                cache->cache[i] = NULL;
            }
            list_add_tail(&mbuf->head, headp);
        }
    }
    unlock(&cache->lck);
}

// this function will be called exclusively
void resend_buffer_reset(resend_buffer* cache)
{
    if (cache->cache != NULL) {
        resend_buffer_clear(cache, cache->seq[0], cache->seq[1]);
        my_free(cache->cache);
    }

    cache->cache = NULL;
    cache->seq[0] = cache->seq[1] = 0;
    cache->capacity = 0;
    cache->lck = lock_val;
}
