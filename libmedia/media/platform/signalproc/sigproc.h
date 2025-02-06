
#ifndef sigproc_h
#define sigproc_h

#include "mydef.h"
#include "my_buffer.h"
#include "fmt.h"

#define sigproc_webrtc 0
#define sigproc_webrtc_none 1
#define sigproc_webrtc_mb 2
#define sigproc_webrtc_pc 3

#define webrtc_control_aec 0 // [sigproc_webrtc_none, sigproc_webrtc_mb, sigproc_webrtc_pc]
#define webrtc_control_agc 1 // [0, 91]
#define webrtc_control_hpass 2 // [0, 1]
#define webrtc_control_vad 3 // [0, 3]
#define webrtc_control_ns 4 // [0, 3]
#define webrtc_control_reserve 5
#define webrtc_pcm_volume 6

capi void* sigproc_get(int type, audio_format* fmt);
capi void sigproc_put(void* sigp);
capi void sigproc_control(void* sigp, int cmd, void* arg);
capi void sigproc_pcm_notify(void* sigp, char* pcm, uint32_t len, uint32_t delay);
capi void sigproc_pcm_proc(void* sigp, struct my_buffer* mbuf, uint32_t delay);

#endif
