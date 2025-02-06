
#ifndef render_jointor_h
#define render_jointor_h

#include "mydef.h"
#include "fmt.h"
#include "jointor.h"

capi jointor* render_create(audio_format* fmt);
capi int render_link_upstream(jointor* self, jointor* upstream);
capi void render_unlink_upstream(jointor* self, jointor* upstream);

#endif
