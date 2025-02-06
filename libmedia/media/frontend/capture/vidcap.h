
#ifndef vidcap_h
#define vidcap_h

#include "mydef.h"
#include "jointor.h"
#include "fmt.h"
#include "videoctl.h"
#include "codec_hook.h"

capi jointor* vidcap_open(video_format* fmt, enum camera_position_t campos, callback_t* hook);
capi void vidcap_close(jointor* join);

#endif
