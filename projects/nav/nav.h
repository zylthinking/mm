
#ifndef nav_h
#define nav_h

#include "mydef.h"
#include "netdef.h"
#include "efftype.h"
#include "audioctl.h"
#include "videoctl.h"
#include "fmt.h"

#define audio_capture_start 0
#define audio_capture_stop 1
#define audio_capture_pause 2
#define audio_play_pause 3
#define audio_choose_aec 4
#define audio_enable_dtx 5
#define video_capture_start 6
#define video_capture_stop 7
#define video_use_camera 8

typedef struct {
    enum aec_t aec;
    intptr_t effect;
    audio_format* fmt;
} audio_param;

typedef struct {
    enum camera_position_t campos;
    intptr_t feedback;
    intptr_t width, height;
    intptr_t gop, fps, kbps;
} video_param;

typedef struct {
    uint64_t uid;
    const uint8_t* info;
    uint32_t bytes;
    uint32_t isp;
    uint32_t ms_wait;
    notify_fptr fptr;
    uint32_t dont_play;
    uint32_t dont_upload;
    uint32_t login_level;
} nav_open_args;

capi void* nav_open(nav_open_args* args);
capi void nav_close(void* any, uint32_t code);
capi int32_t nav_ioctl(void* any, int32_t cmd, void* param);

#endif
