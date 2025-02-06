
#ifndef muxer_h
#define muxer_h

#include "mydef.h"
#include "jointor.h"
#include "codec_hook.h"

capi jointor* muxter_create(uint32_t nr, callback_t* format_hook);
capi int muxter_link_upstream(jointor* self, jointor* upstream);
capi void muxter_unlink_upstream(jointor* self, jointor* upstream);

#endif
