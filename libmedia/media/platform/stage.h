
#ifndef stage_h
#define stage_h

#include "mydef.h"
#include "media_buffer.h"
#include "my_buffer.h"
#include "list_head.h"
#include "area.h"
#include "fmt.h"
#include "lock.h"

typedef struct {
    area_t area;
    video_format* fmt;
    uintptr_t w, h, type;

    struct list_head parent_entry;
    struct list_head sibling_entry;
    struct my_buffer* mbuf;
} stage_t;

capi stage_t* stage_get(media_buffer* media);
capi void wash_stage(stage_t* stage);
capi void stage_put(stage_t* stage);
capi intptr_t rend_to_stage(stage_t* stage, struct my_buffer* stream, intptr_t sync);

#endif
