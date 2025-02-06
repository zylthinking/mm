
#ifndef media_buffer_h
#define media_buffer_h

#include "fmt.h"
#include "mydef.h"
#include "my_buffer.h"

typedef struct {
    uint32_t cid;
    uint64_t uid;
} media_iden_t;

#define make_media_id(cid, uid) \
({ \
    media_iden_t iden = {cid, uid}; \
    iden; \
})

#define remote_stream(id) (id.cid != 0)
#define media_id_eqal(id1, id2) (id1.cid == id2.cid && id1.uid == id2.uid)

#define media_stream \
fourcc** pptr_cc; \
media_iden_t iden

typedef struct {
    media_stream;
} stream_t;

typedef struct {
    media_stream;
    video_panel vp[3];
    uint32_t fragment[2];
    uint32_t frametype;
    uint32_t angle;
    uint64_t seq;
    uint64_t pts;
} media_buffer;
#undef media_stream

#define media_id_unkown make_media_id(0, 0)
#define media_id_self make_media_id(0, 1)
capi media_iden_t media_id_uniq();

capi struct my_buffer* stream_header_get(media_iden_t iden, fourcc** pfcc);
capi void do_stream_begin(struct my_buffer* mbuf, const char* file, int line);
capi void stream_end(media_iden_t iden, fourcc** pfcc);

#define stream_begin(mbuf) do_stream_begin(mbuf, __FILE__, __LINE__)

#endif
