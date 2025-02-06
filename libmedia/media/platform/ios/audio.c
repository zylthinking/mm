
#include "audio.h"
#include "lock.h"
#include "session.h"
#include "my_errno.h"
#include "sigproc.h"
#include "now.h"
#include <pthread.h>

struct audio_session session = {
    .lck = {0},
    .init_step = 0,
    .unit = {.aec = none},
    .public = NULL,
    .private = NULL,
    .category_free = 1,
    .loop = NULL,
    .state = {audio_env_unkown, audio_env_unkown},
    .req = NULL,
    .addtional = 0,
};

static inline int set_session_category(UInt32 category)
{
    OSStatus status = AudioSessionSetProperty(
        kAudioSessionProperty_AudioCategory, sizeof(category), &category);
    logmsg("set_session_category %d ret %d\n", category, status);

    if (status != 0) {
        return -1;
    }
    return 0;
}

static inline int do_switch_to_speaker(UInt32 val)
{
    OSStatus status = AudioSessionSetProperty(
        kAudioSessionProperty_OverrideCategoryDefaultToSpeaker, sizeof(val), &val);
    logmsg("do_switch_to_speaker %d ret %d\n", val, status);

    if (status != 0) {
        return -1;
    }
    return 0;
}

static void perform(void* any)
{
    audio_state* current = (audio_state *) any;
    audio_state* next = current + 1;

    if (0 == memcmp(next, current, sizeof(audio_state))) {
        return;
    }
    audio_state saved = *current;

    if (current->headset != next->headset) {
        current->headset = next->headset;
    }

    if (current->microphone != next->microphone) {
        current->microphone = next->microphone;
    }

    // kAudioSessionProperty_OverrideCategoryDefaultToSpeaker will not work
    // alone before a force switch category
    if ((current->category != next->category || current->speaker != next->speaker) &&
        0 == set_session_category(next->category))
    {
        current->category = next->category;
        if (current->category != kAudioSessionCategory_PlayAndRecord) {
            // 1 or 0 ?
            // I wrote some commence in libyy that when plug-out headset,
            // and set the session category into mediaplayback, then I always
            // failed to set the outroute to reciver, and sound comes out from
            // speaker. that indicate the 1 should be the right value.
            // And obviously, ipad had no reciver at all.
            //
            // however, the document says:
            //    kAudioSessionProperty_OverrideCategoryDefaultToSpeaker
            //    Specifies whether or not to route audio to the speaker (instead of to the receiver)
            //    when no other audio route, such as a headset, is connected.
            //    A read/write UInt32 value. By default, the value of this property is FALSE (0)
            //
            // And another document says the ability to adjust the output route only avaliable
            // in PlayAndRecord, which means the default value should be reciver, e.g. 0.
            // Because if we lost the ability, the none output route should reset to the default
            // or there will be a path to bypass the ability limit.
            //
            // Need test on iphone, ipad and itouch, currently, I trust in the document.
            current->speaker = 0;
        }
    }

    if (current->speaker != next->speaker) {
        int n = do_switch_to_speaker(next->speaker);
        if (n == 0) {
            current->speaker = next->speaker;
        }
    }

    if (0 != memcmp(&saved, current, sizeof(audio_state))) {
        ctrace(audio_notify(current));
    }
}

static void on_interrupt(void* ctx, UInt32 reason)
{
    audio_state* current = &session.state[0];
    audio_state* next = current + 1;
    audio_state saved = *current;

    if (reason == kAudioSessionBeginInterruption) {
        next->interrupted = 1;
        if (current->interrupted == 0) {
            current->interrupted = 1;
            audio_control(paused, play | record, none);
        }
    } else if (current->interrupted == 1) {
        next->interrupted = 0;
        AudioSessionSetActive(TRUE);
        int n = audio_control(resume, play, none);
        if (0 == n) {
            current->interrupted = 0;
        }
    }

    if (0 != memcmp(&saved, current, sizeof(audio_state))) {
        ctrace(audio_notify(current));
    }
}

static void handler(void* any)
{
    audio_state* current = &session.state[0];
    audio_state* next = current + 1;
    struct request* req = session.req;
    session.req = NULL;

    if (req->type == route) {
        if (req->value == current->speaker) {
            next->speaker = req->value;
            req->value = 0;
        } else if (req->value == next->speaker) {
            // we have been in retry mode
            next->speaker = req->value;
            perform(session.state);
            if (next->speaker != current->speaker && !req->retry_if_failed) {
                // this user don't wan't retry, so tell him the result
                // howerver, don't reset next->speaker because the previous caller
                // wants it.
                req->value = -1;
            } else {
                req->value = 0;
            }
        } else {
            // next->speaker maybe equal with current->speaker, or not equal
            // which tell us it is in retry mode
            // if this call succeed, we hornor this later call
            // or else if the later call does not retry, we still
            // hornor the previous retry.
            int saved_speaker = next->speaker;

            next->speaker = req->value;
            perform(session.state);
            if (next->speaker != current->speaker) {
                req->value = -1;
                if (!req->retry_if_failed) {
                    next->speaker = saved_speaker;
                }
            } else {
                req->value = 0;
            }
        }
        req->flag = 0;
        return;
    }

    if (req->type == cat_by_user) {
        my_assert(req->value != kAudioSessionCategory_MediaPlayback);
        int saved_category_free = session.category_free;
        int saved_category = next->category;
        session.category_free = (req->value == kAudioSessionCategory_PlayAndRecord);
        next->category = req->value;
        perform(session.state);

        req->value = -1;
        if (current->category == next->category) {
            req->value = 0;
        } else if (!req->retry_if_failed) {
            session.category_free = saved_category_free;
            next->category = saved_category;
        }
        req->flag = 0;
        return;
    }

    if (req->type == cat_internal) {
        my_assert(req->value == kAudioSessionCategory_MediaPlayback ||
               req->value == kAudioSessionCategory_PlayAndRecord);
        req->value = -1;
        if (session.category_free == 1) {
            int saved_category = next->category;
            next->category = req->value;
            perform(session.state);

            if (current->category == next->category) {
                req->value = 0;
            } else if (!req->retry_if_failed) {
                next->category = saved_category;
            }
        }
        req->flag = 0;
        return;
    }

    if (req->type == lock_dev) {
        req->value = -1;
        if (current->interrupted == 1) {
            on_interrupt(NULL, kAudioSessionEndInterruption);
        }

        if (current->interrupted == 0) {
            req->value = 0;
        }

        req->flag = 0;
        return;
    }
}

static void env_changed(CFRunLoopSourceRef event)
{
    CFRunLoopSourceSignal(event);
    CFRunLoopWakeUp(session.loop);
}

int lock_audio_dev()
{
    if (session.init_step != -1) {
        errno = ENOENT;
        return -1;
    }

    struct request req;
    req.type = lock_dev;
    req.value = 1;
    req.retry_if_failed = 1;
    req.flag = 1;
    int n = __sync_bool_compare_and_swap(&session.req, NULL, &req);
    if (n == 0) {
        errno = EAGAIN;
        return -1;
    }

    env_changed(session.public);
    while (req.flag == 1) {
        sched_yield();
    }
    return req.value;
}

int switch_to_speaker(int one_or_zero, int retry_if_failed)
{
    if (session.init_step == 0) {
        relock(&session.lck);
        if (session.init_step == 0) {
            audio_state* next = &session.state[1];
            next->speaker = one_or_zero;
            unlock(&session.lck);
            return 0;
        }
        unlock(&session.lck);
    }

    struct request req;
    req.type = route;
    req.value = one_or_zero;
    req.retry_if_failed = retry_if_failed;
    req.flag = 1;
    int n = __sync_bool_compare_and_swap(&session.req, NULL, &req);
    if (n == 0) {
        errno = EAGAIN;
        return -1;
    }

    env_changed(session.public);
    while (req.flag == 1) {
        sched_yield();
    }
    return req.value;
}

static int do_switch_to_category(int type, int category, int retry_if_failed)
{
    if (session.init_step < 2) {
        relock(&session.lck);
        if (session.init_step < 2) {
            audio_state* next = &session.state[1];
            next->category = category;
            unlock(&session.lck);
            return 0;
        }
        unlock(&session.lck);
    }

    struct request req;
    req.type = type;
    req.value = category;
    req.retry_if_failed = retry_if_failed;
    req.flag = 1;
    int n = __sync_bool_compare_and_swap(&session.req, NULL, &req);
    if (n == 0) {
        errno = EAGAIN;
        return -1;
    }

    env_changed(session.public);
    while (req.flag == 1) {
        sched_yield();
    }
    return req.value;
}

int switch_to_category(int category, int retry_if_failed)
{
    return do_switch_to_category(cat_by_user, category, retry_if_failed);
}

void toggle_category()
{
    int n = do_switch_to_category(cat_internal, kAudioSessionCategory_MediaPlayback, 0);
    if (n == 0) {
        do_switch_to_category(cat_internal, kAudioSessionCategory_PlayAndRecord, 1);
    }
}

static void* session_event_dispatch(void* arg)
{
    volatile intptr_t* np = (volatile intptr_t *) arg;
    session.loop = CFRunLoopGetCurrent();
    if (session.loop == NULL) {
        *np = 0;
        return NULL;
    }

    if (session.public != NULL) {
        CFRelease(session.public);
    }

    CFRunLoopSourceContext ctx0 = {0};
    ctx0.info = session.req;
    ctx0.perform = handler;
    session.public = CFRunLoopSourceCreate(NULL, 0, &ctx0);
    if (session.public == NULL) {
        *np = 0;
        return NULL;
    }

    CFRunLoopSourceContext ctx1 = {0};
    ctx1.info = &session.state[0];
    ctx1.perform = perform;
    session.private = CFRunLoopSourceCreate(NULL, 1, &ctx1);
    if (session.private == NULL) {
        *np = 0;
        return NULL;
    }

    CFRunLoopAddSource(session.loop, session.private, kCFRunLoopDefaultMode);
    CFRunLoopAddSource(session.loop, session.public, kCFRunLoopDefaultMode);

    *np = 0;
    CFRunLoopRun();
    return NULL;
}

static int start_dispatcher()
{
    pthread_t tid;
    volatile intptr_t n0 = 1;

    int n = pthread_create(&tid, NULL, session_event_dispatch, (void *) &n0);
    if (n != 0) {
        return -1;
    }
    pthread_detach(tid);

    while (n0 == 1) {
        sched_yield();
    }

    if (session.private == NULL) {
        return -1;
    }
    return 0;
}

static int detect_headset()
{
    CFStringRef route;
    UInt32 size = sizeof(CFStringRef);
    int n = -1;
    OSStatus status = AudioSessionGetProperty(kAudioSessionProperty_AudioRoute, &size, &route);
    if (status == 0) {
        Boolean b1 = CFStringHasPrefix(route, CFSTR("Headset"));
        Boolean b2 = CFStringHasPrefix(route, CFSTR("Headphone"));
        CFRelease(route);
        n = (b1 || b2);
    }
    return n;
}

static int env_initialize()
{
    int b, n = 0;
    UInt32 microphone, size = sizeof(UInt32);
    OSStatus status = AudioSessionGetProperty(
                kAudioSessionProperty_AudioInputAvailable, &size, &microphone);
    if (status != 0) {
        return -1;
    }

    n = detect_headset();
    if (n == -1) {
        return -1;
    }

    b = __sync_bool_compare_and_swap(&session.state[1].microphone, -1, (int) microphone);
    b |= __sync_bool_compare_and_swap(&session.state[1].headset, -1, n);
    if (b) {
        env_changed(session.private);
    }
    return 0;
}

static void env_changed_notify(void* context,
                AudioSessionPropertyID inID, UInt32 inDataSize, const void* inData)
{
    audio_state* next = &session.state[1];

    if (inID == kAudioSessionProperty_AudioInputAvailable) {
        next->microphone = (int) (*((UInt32 *) inData));
    } else if (inID == kAudioSessionProperty_AudioRouteChange) {
        int n = detect_headset();
        if (n != -1) {
            next->headset = n;
        } else {
            CFDictionaryRef ref = (CFDictionaryRef) inData;
            CFStringRef str_reason = CFSTR(kAudioSession_AudioRouteChangeKey_Reason);
            CFNumberRef reason = (CFNumberRef) CFDictionaryGetValue(ref, str_reason);
            SInt32 reasonVal;
            CFNumberGetValue(reason, kCFNumberSInt32Type, &reasonVal);

            if (reasonVal == kAudioSessionRouteChangeReason_OldDeviceUnavailable) {
                next->headset = 0;
            } else if (reasonVal == kAudioSessionRouteChangeReason_NewDeviceAvailable) {
                next->headset = 1;
            }
        }
    }
    env_changed(session.private);
}

static int audio_initialize_internal()
{
    if (session.init_step == 0) {
        if (-1 == start_dispatcher()) {
            return -1;
        }
        session.init_step = 1;
    }

    if (session.init_step == 1) {
        int n = (int) AudioSessionInitialize(
                    session.loop, NULL, on_interrupt, NULL);
        if (n != 0 && n != (int) 'init') {
            return -1;
        }
        session.init_step = 2;
    }

    if (session.init_step == 2) {
        audio_state* next = &session.state[1];
        int category = kAudioSessionCategory_PlayAndRecord;
        if (next->category != -1) {
            category = next->category;
        }

        if (-1 == switch_to_category(category, 0)) {
            return -1;
        }
        session.init_step = 3;
    }

    if (session.init_step == 3) {
        audio_state* next = &session.state[1];
        if (-1 == switch_to_speaker(next->speaker == -1 ? 1 : next->speaker, 0)) {
            return -1;
        }
        session.init_step = 4;
    }

    if (session.init_step == 4) {
        int n = (int) AudioSessionAddPropertyListener(
                    kAudioSessionProperty_AudioInputAvailable,
                    env_changed_notify, NULL);
        if (n != 0) {
            return -1;
        }
        session.init_step = 5;
    }

    if (session.init_step == 5) {
        int n = (int) AudioSessionAddPropertyListener(
                    kAudioSessionProperty_AudioRouteChange,
                    env_changed_notify, NULL);
        if (n != 0) {
            return -1;
        }
        session.init_step = 6;
    }

    if (session.init_step == 6) {
        int n = env_initialize();
        if (n != 0) {
            return -1;
        }
        session.init_step = 7;
    }

    session.init_step = -1;
    return 0;
}

#if (defined(__GNUC__) || defined(__clang__))
__attribute__((constructor))
#endif
static void audio_initialize()
{
    relock(&session.lck);
    int n = audio_initialize_internal();
    unlock(&session.lck);

    if (n == -1) {
        logmsg("audio_initialize failed\n");
        exit(-1);
    }
}
#if defined(_MSC_VER)
#pragma data_seg(".CRT$XIU")
static intptr_t ptr = audio_initialize;
#pragma data_seg()
#endif

static void to_audio_descriptor(audio_format* output, AudioStreamBasicDescription* desc)
{
    desc->mSampleRate        = output->pcm->samrate;
    desc->mFormatID          = kAudioFormatLinearPCM;
    desc->mFormatFlags       = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    desc->mFramesPerPacket   = 1;
    desc->mChannelsPerFrame  = output->pcm->channel;
    desc->mBitsPerChannel    = output->pcm->sambits;
    desc->mBytesPerFrame     = desc->mBitsPerChannel * desc->mChannelsPerFrame / 8;
    desc->mBytesPerPacket    = desc->mBytesPerFrame * desc->mFramesPerPacket;
    desc->mReserved = 0;
}

int audio_start_output(fetch_pcm fptr, audio_format* fmt, void* ctx)
{
    if (fptr == NULL) {
        errno = EINVAL;
        return -1;
    }

    lock(&session.lck);
    to_audio_descriptor(fmt, &output_fmt);
    session.unit.fmt_out = fmt;

    if (session.unit.fptr[0] != NULL) {
        errno = EEXIST;
        if (output_stat == stopped) {
            errno = EAGAIN;
        }
        unlock(&session.lck);
        return -1;
    }

    session.unit.ctx[0] = ctx;
    session.unit.fptr[0] = fptr;

    // we need wmb to give output_pull@unit.c a word
    // that if it add the ref to 2, then it won't get an old fptr
    // output_pull does not need a rmb because the __sync_xxx has
    // do that for us.
    wmb();

    session.unit.ref[0] = 1;
    unlock(&session.lck);

    int n = audio_control(runing, play, none);
    if (n == -1) {
        session.unit.ref[0] = 0;
        session.unit.fptr[0] = NULL;
    }
    return n;
}

void audio_stop_output()
{
    audio_control(stopped, play, none);
    int ref = __sync_sub_and_fetch(&session.unit.ref[0], 1);
    if (ref == 0) {
        session.unit.fptr[0](session.unit.ctx[0], NULL, NULL, 0);
        session.unit.fptr[0] = NULL;
    }
}

static void initialize_aec(void* sigproc, uint32_t aec_method, uint32_t webrtc, int32_t vad)
{
    uint32_t val = 1;
    sigproc_control(sigproc, webrtc_control_aec, &aec_method);
    if (webrtc == 1) {
        sigproc_control(sigproc, webrtc_control_hpass, &val);
        sigproc_control(sigproc, webrtc_control_agc, &val);

        val = 3;
        sigproc_control(sigproc, webrtc_control_ns, &val);
    }

    val = vad;
    sigproc_control(sigproc, webrtc_control_vad, &val);
    sigproc_control(sigproc, webrtc_control_reserve, &session.addtional);
}

static int32_t webrtc_aec_type(enum aec_t aec)
{
    if (aec == webrtc_none) {
        return sigproc_webrtc_none;
    }

    if (aec == webrtc_mb) {
        return sigproc_webrtc_mb;
    }

    return sigproc_webrtc_pc;
}

static int webrtc_aec_control(enum aec_t aec, uint32_t vol1, uint32_t vol0)
{
    int volume[2] = {-1, -1};
    if (vol0 != (uint32_t) -1) {
        volume[0] = (vol0 & 0x7fffffff);
    }

    if (vol1 != (uint32_t) -1) {
        volume[1] = (vol1 & 0xffff);
    }

    void* sigproc = session.unit.sigproc;
    my_assert(sigproc != NULL);
    uint32_t type = webrtc_aec_type(aec);
    sigproc_control(sigproc, webrtc_control_aec, &type);
    sigproc_control(sigproc, webrtc_pcm_volume, volume);
    return 0;
}

int audio_aec_ctl(enum aec_t aec, uint32_t vol1, uint32_t vol0)
{
    lock(&session.lck);
    if (input_stat != runing) {
        unlock(&session.lck);
        errno = ENOENT;
        return -1;
    }

    if (session.unit.aec == aec) {
        unlock(&session.lck);
        return 0;
    }

    int n = -1;
    errno = EPERM;
    if (aec == webrtc_mb || aec == webrtc_none || aec == webrtc_pc) {
        if (session.unit.aec != ios) {
            n = webrtc_aec_control(aec, vol1, vol0);
        }
    } else if (aec == ios) {
        if (force_vpio && session.unit.aec == none) {
            n = vpio_disable_aec(0);
        }
    } else {
        if (session.unit.aec == ios) {
            if (force_vpio) {
                n = vpio_disable_aec(1);
            } else {
                // or else stop_remoteid will be called
                // instead of stop_vpio when stopping
            }
        } else {
            n = webrtc_aec_control(webrtc_none, vol1, vol0);
        }
    }

    if (n == 0) {
        session.unit.aec = aec;
    }
    unlock(&session.lck);
    return n;
}

int audio_enable_vad(uint32_t level)
{
    lock(&session.lck);
    if (input_stat != runing) {
        unlock(&session.lck);
        errno = ENOENT;
        return -1;
    }

    void* sigproc = session.unit.sigproc;
    my_assert(sigproc != NULL);
    sigproc_control(sigproc, webrtc_control_vad, &level);
    unlock(&session.lck);
    return 0;
}

audio_format* audio_start_input(fetch_pcm fptr, audio_format* fmt, void* ctx, enum aec_t aec)
{
    errno = EINVAL;
    if (fptr == NULL) {
        return NULL;
    }

    if (force_vpio) {
        if (aec != ios && aec != none) {
            return NULL;
        }
    }

    enum aec_t sigproc_aec = aec;
    uint32_t webrtc = (aec != ios && aec != none);
    if (webrtc == 0) {
        sigproc_aec = webrtc_none;
    }

    lock(&session.lck);
    to_audio_descriptor(fmt, &input_fmt);
    session.unit.fmt_in = fmt;

    void* sigproc = sigproc_get(sigproc_webrtc, session.unit.fmt_in);
    if (sigproc == NULL) {
        unlock(&session.lck);
        errno = EFAILED;
        return NULL;
    }

    if (session.unit.fptr[1] != NULL) {
        sigproc_put(sigproc);
        errno = EEXIST;
        if (input_stat == stopped) {
            errno = EAGAIN;
        }
        unlock(&session.lck);
        return NULL;
    }

    initialize_aec(sigproc, webrtc_aec_type(sigproc_aec), webrtc, 0);
    session.unit.sigproc = sigproc;

    session.unit.ctx[1] = ctx;
    session.unit.fptr[1] = fptr;

    // see audio_start_output for an explaination
    wmb();

    session.unit.ref[1] = 1;
    unlock(&session.lck);

    int n = audio_control(runing, record, aec);
    if (n == -1) {
        session.unit.ref[1] = 0;
        session.unit.fptr[1] = NULL;
        return NULL;
    }
    return fmt;
}

void audio_stop_input()
{
    audio_control(stopped, record, none);
    if (session.unit.sigproc != NULL) {
        sigproc_put(session.unit.sigproc);
        session.unit.sigproc = NULL;
        session.addtional = 0;
    }

    int ref = __sync_sub_and_fetch(&session.unit.ref[1], 1);
    if (ref == 0) {
        session.unit.fptr[1](session.unit.ctx[1], NULL, NULL, 0);
        session.unit.fptr[1] = NULL;
    }
}

__attribute__((weak)) void audio_notify(void* any)
{
    (void) any;
    return;
}
