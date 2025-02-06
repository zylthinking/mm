

#ifndef mmv_h
#define mmv_h

#include "mydef.h"
#include "efftype.h"
#include "audioctl.h"

capi void* mmvoice_capture_to_file(const char* fullpath, intptr_t effect, enum aec_t aec);
capi int32_t amplitude(void* any);
capi void mmvoice_capture_stop(void* any);

typedef struct {
    int total;
    int consumed;
    unsigned ms_total;
    int err;
} playpos;

capi playpos* mmvoice_play_file(const char* fullpath);
capi void mmvoice_stop_play(playpos* pos);

#endif
