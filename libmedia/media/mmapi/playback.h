
#ifndef playback_h
#define playback_h

#include "mydef.h"
#include "jointor.h"
#include "fmt.h"

capi void* playback_open(audio_format* audio_format, jointor* upstream);
capi void playback_close(void* mptr, jointor* upstream);
capi void* playback_hook_add(callback_t* hook);
capi void playback_hook_delete(void* mptr);

#endif
