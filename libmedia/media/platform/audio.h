
#ifndef audio_h
#define audio_h

#include "mydef.h"
#include "audioctl.h"
#include "fmt.h"

typedef int (*fetch_pcm) (void*, audio_format* fmt, char*, unsigned int);
capi int audio_start_output(fetch_pcm fptr, audio_format* fmt, void* ctx);
capi void audio_stop_output();

capi audio_format* audio_start_input(fetch_pcm fptr, audio_format* fmt, void* ctx, enum aec_t aec);
capi void audio_stop_input();

#endif
