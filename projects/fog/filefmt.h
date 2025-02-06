

#ifndef filefmt_h
#define filefmt_h

#include "mydef.h"
#include "fmt.h"

#include "fctx.h"
#include "ffmpegfile.h"
#define ftype_auto 0

#define filefmt_stream 0
typedef struct {
    struct my_buffer* mbuf;
    fourcc** in_cc;
    fourcc** out_cc;
} filefmt_stream_hook;

capi void* filefmt_open(const char* uri, uint32_t type, callback_t* cb1, file_mode_t* mode);
capi fcontext_t* fcontext_get(void* ctx);
capi intptr_t filefmt_seek(void* ctx, uintptr_t seekto);
capi void filefmt_close(void* ctx);

#endif
