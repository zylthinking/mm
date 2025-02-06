
#include "session.h"
#include "my_errno.h"
#include "lock.h"
#include "mbuf.h"
#include "media_buffer.h"
#include "sigproc.h"
#include "pts.h"
#include "smooth.h"
#include "ios.h"

static AudioUnit hot_unit = NULL;
#define discard(x) do {hot_unit = x; barrier();} while (0)
#define undiscard() do {barrier(); hot_unit = NULL;} while (0)

static int hardware_delay(int io)
{
    static int delay[3] = {
        3, 3, 24
    };

#if 0
    UInt32 size = sizeof(Float32);
    UInt32 type = kAudioSessionProperty_CurrentHardwareInputLatency;
    if (io == 0) {
        type = kAudioSessionProperty_CurrentHardwareOutputLatency;
    }

    Float32 seconds = (Float32) 0.0;
    UInt32 id = kAudioSessionProperty_CurrentHardwareIOBufferDuration;
    OSStatus status = AudioSessionGetProperty(id, &size, &seconds);
    check_status(status, ((void) 0));
    if (status == 0) {
        delay[2] = seconds * 1000;
    }

    status = AudioSessionGetProperty(type, &size, &seconds);
    check_status(status, ((void) 0));
    if (status == 0) {
        delay[io] = seconds * 1000;
    }
#endif
    return delay[io] + delay[2] + 1;
}

OSStatus output_pull(void*                          inRefCon,
                     AudioUnitRenderActionFlags*    ioActionFlags,
                     const AudioTimeStamp*          inTimeStamp,
                     UInt32                         inBusNumber,
                     UInt32                         inNumberFrames,
                     AudioBufferList*               ioData)
{
    my_assert(ioData && ioData->mNumberBuffers == 1);
    AudioUnit unit = (AudioUnit) inRefCon;

    if (__builtin_expect(hot_unit == unit, 0)) {
        *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
        return 0;
    }

    char* buf = (char *) ioData->mBuffers[0].mData;
    unsigned int bytes = ioData->mBuffers[0].mDataByteSize;

    fetch_pcm fptr = NULL;
    int ref = __sync_add_and_fetch(&session.unit.ref[0], 1);
    if (ref == 2) {
        fptr = session.unit.fptr[0];
    } else {
        my_assert(ref == 1);
    }

    int n = 0;
    if (fptr != NULL) {
        n = fptr(session.unit.ctx[0], session.unit.fmt_out, buf, bytes);
    }

    ref = __sync_sub_and_fetch(&session.unit.ref[0], 1);
    if (ref == 0 && fptr != NULL) {
        fptr(session.unit.ctx[0], NULL, NULL, 0);
        session.unit.ref[0] = 0;
        session.unit.fptr[0] = NULL;
    }

#if 1
    pcm_format* pcm = session.unit.fmt_out->pcm;
    smooth_fill(pcm, buf, n, bytes);
#else
    if (n <= 0) {
        *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
        n = bytes;
    } else if (n < bytes) {
        memset(buf + n, 0, bytes - n);
    }
#endif

    void* proc = session.unit.sigproc;
    if (proc != NULL) {
        sigproc_pcm_notify(proc, buf, bytes, hardware_delay(0));
    }
    return 0;
}

OSStatus input_push(void*                          inRefCon,
                    AudioUnitRenderActionFlags*    ioActionFlags,
                    const AudioTimeStamp*          inTimeStamp,
                    UInt32                         inBusNumber,
                    UInt32                         inNumberFrames,
                    AudioBufferList*               ioData)
{
    my_assert(ioData == NULL);
    AudioUnit unit = (AudioUnit) inRefCon;
    static uint64_t seq = 0;

    if (__builtin_expect(hot_unit == unit, 0)) {
        return 0;
    }

    static AudioBufferList abl = {1, {1,}};
    AudioStreamBasicDescription* fmt = &input_fmt;
    int needed = inNumberFrames * fmt->mBytesPerFrame;
    struct my_buffer* mbuf = mbuf_alloc_2(sizeof(media_buffer) + needed + session.addtional);
    if (mbuf == NULL) {
        return 0;
    }
    mbuf->ptr[1] = mbuf->ptr[0] + sizeof(media_buffer) + session.addtional;
    mbuf->length = needed;

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    memset(media->vp, 0, sizeof(media->vp));
    media->frametype = 0;
    media->angle = 0;
    media->fragment[0] = 0;
    media->fragment[1] = 1;
    media->pptr_cc = &session.unit.fmt_in->cc;
    media->iden = media_id_self;
    media->seq = seq++;

    abl.mBuffers[0].mData = mbuf->ptr[1];
    abl.mBuffers[0].mDataByteSize = needed;

    int n = 0;
    OSStatus status = AudioUnitRender(unit, ioActionFlags, inTimeStamp, 1, inNumberFrames, &abl);
    check_status(status, n = -1);
    if (n == -1) {
        mbuf->mop->free(mbuf);
        return 0;
    }

    void* proc = session.unit.sigproc;
    if (proc != NULL) {
        sigproc_pcm_proc(proc, mbuf, hardware_delay(1));
    }

    fetch_pcm fptr = NULL;
    int ref = __sync_add_and_fetch(&session.unit.ref[1], 1);
    if (ref == 2) {
        fptr = session.unit.fptr[1];
    } else {
        my_assert(ref == 1);
    }

    if (mbuf->length > 0 && fptr != NULL) {
        media->vp[0].ptr = mbuf->ptr[1];
        media->vp[0].stride = (uint32_t) mbuf->length;
        fill_pts(mbuf, NULL);
        fptr(session.unit.ctx[1], session.unit.fmt_in, (char *) mbuf, 0);
    } else {
        mbuf->mop->free(mbuf);
    }

    ref = __sync_sub_and_fetch(&session.unit.ref[1], 1);
    if (ref == 0 && fptr != NULL) {
        fptr(session.unit.ctx[1], NULL, NULL, 0);
        session.unit.ref[1] = 0;
        session.unit.fptr[1] = NULL;
    }
    return 0;
}

int vpio_disable_aec(int enable)
{
    // the code following maybe cause audio unit
    // stopping abnormally.
    return 0;

    errno = EFAILED;
    UInt32 flag = enable;
    OSStatus status = AudioUnitSetProperty
                      (
                          session.unit.unit_io,
                          kAUVoiceIOProperty_BypassVoiceProcessing,
                          kAudioUnitScope_Global,
                          0,
                          &flag,
                          sizeof(UInt32)
                      );
    check_status(status, return -1);
    return 0;
}

static inline int start_remoteio(AudioUnit au)
{
    int n = 0;
    OSStatus status;
    if (lazy_init_remote) {
        status = AudioUnitInitialize(au);
        check_status(status, n = -1);
    }

    if (n == 0) {
        status = AudioOutputUnitStart(au);
        check_status(status, n = -1);
    }

    if (n == -1) {
        if (lazy_init_remote) {
            status = AudioUnitUninitialize(au);
            check_status(status, (void) 0);
        }
        errno = EFAILED;
    }
    return n;
}

static inline void stop_remoteio(AudioUnit au)
{
    OSStatus status = AudioOutputUnitStop(au);
    check_status(status, (void) 0);
    if (lazy_init_remote) {
        status = AudioUnitUninitialize(au);
        check_status(status, (void) 0);
    }
}

static inline int start_composite(AudioUnit au)
{
    int n = 0;
    OSStatus status;
    if (lazy_init_voicep) {
        status = AudioUnitInitialize(au);
        check_status(status, n = -1);
    }

    if (n == 0) {
        status = AudioOutputUnitStart(au);
        check_status(status, n = -1);
    }

    if (n == -1) {
        if (lazy_init_voicep) {
            status = AudioUnitUninitialize(au);
            check_status(status, (void) 0);
        }
        errno = EFAILED;
    }
    return n;

}

static inline void stop_composite(AudioUnit au)
{
    OSStatus status = AudioOutputUnitStop(au);
    check_status(status, (void) 0);

    if (lazy_init_voicep) {
        status = AudioUnitUninitialize(au);
        check_status(status, (void) 0);
    }
}

int output_start(int composite)
{
    int n;
    if (composite) {
        create_unit(session.unit.unit_io, &input_fmt, &output_fmt);
        if (session.unit.unit_io == NULL) {
            errno = EFAILED;
            return -1;
        }

        discard(session.unit.unit_io);
        n = start_composite(session.unit.unit_io);
        if (n == 0) {
            vpio_disable_aec(0);
            stop_remoteio(session.unit.unit_i);
            destroy_audio_unit(session.unit.unit_i);
        } else {
            destroy_audio_unit(session.unit.unit_io);
        }
        undiscard();
    } else {
        create_unit(session.unit.unit_o, NULL, &output_fmt);
        if (session.unit.unit_o == NULL) {
            errno = EFAILED;
            return -1;
        }

        n = start_remoteio(session.unit.unit_o);
        if (n == -1) {
            destroy_audio_unit(session.unit.unit_o);
        }
    }
    return n;
}

void output_stop(int composite)
{
    if (composite) {
        stop_composite(session.unit.unit_io);
        destroy_audio_unit(session.unit.unit_io);
    } else {
        stop_remoteio(session.unit.unit_o);
        destroy_audio_unit(session.unit.unit_o);
    }
}

int input_start(int composite)
{
    int n;
    if (composite) {
        create_unit(session.unit.unit_io, &input_fmt, &output_fmt);
        if (session.unit.unit_io == NULL) {
            errno = EFAILED;
            return -1;
        }

        discard(session.unit.unit_io);
        n = start_composite(session.unit.unit_io);
        if (n == 0) {
            vpio_disable_aec(0);
            stop_remoteio(session.unit.unit_o);
            destroy_audio_unit(session.unit.unit_o);
        } else {
            destroy_audio_unit(session.unit.unit_io);
        }
        undiscard();
    } else {
        create_unit(session.unit.unit_i, &input_fmt, NULL);
        if (session.unit.unit_i == NULL) {
            errno = EFAILED;
            return -1;
        }

        n = start_remoteio(session.unit.unit_i);
        if (n == -1) {
            destroy_audio_unit(session.unit.unit_i);
        }
    }
    return n;
}

void input_stop(int composite)
{
    if (composite) {
        stop_composite(session.unit.unit_io);
        destroy_audio_unit(session.unit.unit_io);
    } else {
        stop_remoteio(session.unit.unit_i);
        destroy_audio_unit(session.unit.unit_i);
    }
}

static int create_unit_vpio(int io)
{
    if (io == 1) {
        output_fmt = input_fmt;
        session.unit.fmt_out = session.unit.fmt_in;
    } else {
        input_fmt = output_fmt;
        session.unit.fmt_in = session.unit.fmt_out;
    }
    create_unit(session.unit.unit_io, &input_fmt, &output_fmt);

    if (session.unit.unit_io == NULL) {
        errno = EFAILED;
        return -1;
    }

    if (!bidirection) {
        UInt32 flag = io;
        OSStatus status = AudioUnitSetProperty(session.unit.unit_io,
                                               kAudioOutputUnitProperty_EnableIO,
                                               kAudioUnitScope_Input,
                                               1,
                                               &flag,
                                               sizeof(UInt32));
        check_status(status, goto LABEL);

        flag = !io;
        status = AudioUnitSetProperty(session.unit.unit_io,
                                      kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output,
                                      0,
                                      &flag,
                                      sizeof(UInt32));
        check_status(status, goto LABEL);
    }

    int n = start_composite(session.unit.unit_io);
    if (n == -1) {
        goto LABEL;
    }
    return 0;
LABEL:
    destroy_audio_unit(session.unit.unit_io);
    return -1;
}

static int reconfig_unit_vpio(int io, int enable)
{
    OSStatus status;
    my_assert(session.unit.unit_io != NULL);
    stop_composite(session.unit.unit_io);

    if (io == 0) {
        if (enable) {
            status = AudioUnitSetProperty(session.unit.unit_io,
                                          kAudioUnitProperty_StreamFormat,
                                          kAudioUnitScope_Input,
                                          0,
                                          &output_fmt,
                                          sizeof(AudioStreamBasicDescription));
            check_status(status, (void) 0);
        }

        status = AudioUnitSetProperty(session.unit.unit_io,
                                      kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output,
                                      0,
                                      &enable,
                                      sizeof(UInt32));
        check_status(status, (void) 0);
    } else {
        if (enable) {
            status = AudioUnitSetProperty(session.unit.unit_io,
                                          kAudioUnitProperty_StreamFormat,
                                          kAudioUnitScope_Output,
                                          1,
                                          &input_fmt,
                                          sizeof(AudioStreamBasicDescription));
            check_status(status, (void) 0);
        }

        status = AudioUnitSetProperty(session.unit.unit_io,
                                      kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Input,
                                      1,
                                      &enable,
                                      sizeof(UInt32));
        check_status(status, (void) 0);
    }

    int n = start_composite(session.unit.unit_io);
    return n;
}

int output_start_vpio(int composite)
{
    int n = 0;
    if (composite == 0) {
        n = create_unit_vpio(0);
    } else if (!bidirection) {
        n = reconfig_unit_vpio(0, 1);
    }

    if (n == 0) {
        vpio_disable_aec(composite == 0 || aec_type == none);
    }

    return n;
}

int output_stop_vpio(int composite)
{
    int n = 0;
    if (composite) {
        if (!bidirection) {
            n = reconfig_unit_vpio(0, 0);
        }

        if (n == -1) {
            destroy_audio_unit(session.unit.unit_io);
        } else {
            vpio_disable_aec(1);
        }
    } else {
        stop_composite(session.unit.unit_io);
        destroy_audio_unit(session.unit.unit_io);
    }

    return n;
}

int input_start_vpio(int composite)
{
    int n = 0;
    if (composite == 0) {
        n = create_unit_vpio(1);
    } else if (!bidirection) {
        n = reconfig_unit_vpio(1, 1);
    }

    if (n == 0) {
        vpio_disable_aec(composite == 0 || aec_type == none);
    }

    return n;
}

int input_stop_vpio(int composite)
{
    int n = 0;
    if (composite) {
        if (!bidirection) {
            n = reconfig_unit_vpio(1, 0);
        }

        if (n == -1) {
            destroy_audio_unit(session.unit.unit_io);
        } else {
            vpio_disable_aec(1);
        }
    } else {
        stop_composite(session.unit.unit_io);
        destroy_audio_unit(session.unit.unit_io);
    }

    return n;
}
