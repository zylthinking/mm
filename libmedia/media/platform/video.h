
#ifndef video_h
#define video_h

#include "mydef.h"
#include "my_buffer.h"
#include "videoctl.h"
#include "fmt.h"

typedef void (*video_push_t) (void* ctx, struct my_buffer* mbuf);
capi void* video_create(enum camera_position_t campos, video_push_t func, void* ctx, video_format* fmt);
capi void video_destroy(void* ptr);

#endif