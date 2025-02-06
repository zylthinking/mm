
#include "media_buffer.h"
#include "lock.h"

media_iden_t media_id_uniq()
{
    static int uid = 0;
    uint32_t id = 0;

    do {
        id = (uint32_t) __sync_add_and_fetch(&uid, 1);
        id &= 0xfffff;
    } while (id < 2);

    return make_media_id(0, id);
}

static struct {
    media_iden_t id;
    fourcc** pfcc;
    struct my_buffer* mbuf;
} stream_entries[32];
static lock_t lck = lock_initial;

struct my_buffer* stream_header_get(media_iden_t id, fourcc** pfcc)
{
    struct my_buffer* mbuf = NULL;
    lock(&lck);
    for (int i = 0; i < elements(stream_entries); ++i) {
        if (media_id_eqal(stream_entries[i].id, id) &&
            stream_entries[i].mbuf != NULL &&
            stream_entries[i].pfcc == pfcc)
        {
            mbuf = stream_entries[i].mbuf->mop->clone(stream_entries[i].mbuf);
        }
    }
    unlock(&lck);
    return mbuf;
}

void do_stream_begin(struct my_buffer* mbuf, const char* file, int line)
{
    lock(&lck);
    for (int i = 0; i < elements(stream_entries); ++i) {
        if (stream_entries[i].mbuf == NULL) {
            media_buffer* media = (media_buffer *) mbuf->ptr[0];
            stream_entries[i].id = media->iden;
            stream_entries[i].pfcc = media->pptr_cc;
            stream_entries[i].mbuf = mbuf->mop->clone(mbuf);
            break;
        }
    }
    unlock(&lck);
}

void stream_end(media_iden_t id, fourcc** pfcc)
{
    lock(&lck);
    for (int i = 0; i < elements(stream_entries); ++i) {
        if (media_id_eqal(stream_entries[i].id, id) &&
            stream_entries[i].mbuf != NULL &&
            stream_entries[i].pfcc == pfcc)
        {
            stream_entries[i].mbuf->mop->free(stream_entries[i].mbuf);
            stream_entries[i].mbuf = NULL;
        }
    }
    unlock(&lck);
}
