

#ifndef session_h
#define session_h

#include "mydef.h"
#include "AudioUnit/AudioUnit.h"
#include "AudioToolbox/AudioSession.h"
#include "audio.h"
#include "lock.h"

#define stopped     0
#define runing      1
#define paused      2
#define resume      3
#define record      1
#define play        2

struct request {
    enum {
        route, cat_by_user, cat_internal, lock_dev
    } type;
    int value;
    int retry_if_failed;
    volatile intptr_t flag;
};

struct unit_struct {
    int lck;
    fetch_pcm fptr[2];
    void* ctx[2];
    int ref[2];

    AudioComponentInstance unit_i;
    AudioComponentInstance unit_o;
    AudioComponentInstance unit_io;
    AudioStreamBasicDescription fmt_i, fmt_o;
    audio_format *fmt_in, *fmt_out;
    int state_i, state_o;
    enum aec_t aec;
    void* sigproc;
};

struct audio_session {
    lock_t lck;
    int init_step;
    audio_state state[2];
    struct unit_struct unit;
    uint32_t addtional;

    struct request* req;
    int category_free;

    CFRunLoopSourceRef private;
    CFRunLoopSourceRef public;
    CFRunLoopRef loop;
};

#define lazy_init_remote 0
#define lazy_init_voicep 0
#define toggle_cate 0

#define force_vpio 0
#define bidirection (1 && force_vpio)

#if force_vpio
#undef lazy_init_voicep
#define lazy_init_voicep 1
#endif

#define audio_env_unkown { \
    .category = -1, \
    .speaker = -1, \
    .headset = -1, \
    .microphone = -1, \
    .interrupted = 0 \
}

extern struct audio_session session;
#define input_stat session.unit.state_i
#define output_stat session.unit.state_o
#define aec_type session.unit.aec
#define input_fmt session.unit.fmt_i
#define output_fmt session.unit.fmt_o

capi void toggle_category();
capi int vpio_disable_aec(int enable);

capi int audio_control(int cmd, int record_or_play, enum aec_t aec);
capi int output_start(int composite);
capi void output_stop(int composite);
capi int input_start(int composite);
capi void input_stop(int composite);

capi int output_start_vpio(int composite);
capi int output_stop_vpio(int composite);
capi int input_start_vpio(int composite);
capi int input_stop_vpio(int composite);

capi AudioComponentInstance create_unit_internal(AudioStreamBasicDescription* in_fmt, AudioStreamBasicDescription* out_fmt);
capi void destroy_audio_unit_internal(AudioComponentInstance audioUnit);
capi int hardware_latency();

#define destroy_audio_unit(x) \
do { \
    destroy_audio_unit_internal(x); \
    x = NULL; \
} while (0)

#if 0
    #define create_unit(unit, x, y) \
    do { \
        if (unit != NULL) { \
            destroy_audio_unit(unit); \
        } \
        unit = create_unit_internal(x, y); \
    } while (0)
#else
    #define create_unit(unit, x, y) \
    do { \
        if (unit == NULL) { \
            unit = create_unit_internal(x, y); \
        } \
    } while (0)
#endif

#endif
