
#ifndef audioctl_h
#define audioctl_h

#include "mydef.h"
enum aec_t {none, ios, webrtc_none, webrtc_mb, webrtc_pc};

#ifdef __APPLE__
typedef struct {
    int category;
    int speaker;
    int headset;
    int microphone;
    int interrupted;
} audio_state;

#define in_killed ((void *) 0)
#define out_killed ((void *) 1)
#define not_killed ((void *) 2)

capi int lock_audio_dev();
capi int switch_to_speaker(int one_or_zero, int retry_if_failed);
capi int switch_to_category(int category, int retry_if_failed);

#define need_implemented
capi void audio_notify(void* any) need_implemented;
#endif

capi int audio_aec_ctl(enum aec_t aec, uint32_t vol1, uint32_t vol0);
capi int audio_enable_vad(uint32_t level);
#endif
