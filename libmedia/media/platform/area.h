
#ifndef area_h
#define area_h

#include "mydef.h"
#include "lock.h"
#include "media_buffer.h"

typedef struct {
    media_iden_t identy;
    intptr_t x, y;
    uintptr_t z, w, h, angle;
    void* egl;

    lock_t lck;
    intptr_t idx;
} area_t;

#define need_implemented
capi intptr_t open_area(area_t* area) need_implemented;
capi void reopen_area(area_t* area, uintptr_t width, uintptr_t height) need_implemented;
capi void do_close_area(area_t* area);
capi intptr_t close_area(area_t* area) need_implemented;
capi void area_invalid(area_t* area, area_t* templ);

#endif