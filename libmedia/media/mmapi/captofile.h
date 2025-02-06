
#ifndef captofile_h
#define captofile_h

#include "mydef.h"
#include "fmt.h"
#include "my_buffer.h"
#include "audioctl.h"
#include "videoctl.h"
#include "codec_hook.h"

typedef struct {
    fourcc** cc;
    union {
        enum aec_t aec;
        enum camera_position_t campos;
    };
} target_param;

capi void* hook_control(callback_t* hook, int32_t a0v1, void* ptr, intptr_t sync);
capi void* capture_add_target(callback_t* cb, target_param* param);
capi void capture_delete_target(void*);

#endif
