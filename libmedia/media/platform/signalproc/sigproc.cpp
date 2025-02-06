
#include "sigproc.h"
#include "now.h"
#include "mem.h"
#include "filt.h"
#include "sigproc.h"
#include "audio_processing.h"
#include "module_common_types.h"
#include <new>
using namespace webrtc;

typedef struct {
    AudioFrame frame;
    struct my_buffer* mbuf;
    audio_format* fmt;

    uint32_t bytes_used;
    uint32_t bytes_10ms;
} pcm_frame;

class webrtc_sigproc {
public:
    uint32_t type;
    AudioProcessing* apm;
    pcm_frame in, out;
    uint32_t delay;
    Filter* filter[2];

    uint32_t agc;
    uint32_t hpass;
    uint32_t ns;
    uint32_t vad;
    int32_t volume[2];

    webrtc_sigproc()
    {
        filter[0] = NULL;
        filter[1] = new (std::nothrow) Filter(LPF, 51, 8.0, 3.0);
        apm = AudioProcessing::Create(0);
        if (apm == NULL || filter[1] == NULL || filter[1]->get_error_flag()) {
            my_assert(0);
        }
        volume[0] = volume[1] = 65535;
    }
};

static void webrtc_control(webrtc_sigproc* proc, uint32_t cmd, void* args)
{
    AudioProcessing* apm = proc->apm;
    uint32_t op = *(uint32_t *) args;

    switch (cmd) {
        case webrtc_control_aec:
            if (op == sigproc_webrtc_none) {
                apm->echo_control_mobile()->Enable(false);
                apm->echo_cancellation()->Enable(false);
                proc->type = op;
                logmsg("close aec\n");
            } else if (op == sigproc_webrtc_mb) {
                apm->echo_cancellation()->Enable(!op);
                apm->echo_control_mobile()->Enable(op);
                proc->type = op;
                logmsg("change to mobile\n");
            } else if (op == sigproc_webrtc_pc) {
                apm->echo_control_mobile()->Enable(!op);
                apm->echo_cancellation()->Enable(op);
                proc->type = op;
                logmsg("change to pc\n");
            }
            break;

        case webrtc_control_agc:
            if (op > 91) {
                op = 91;
            }

            if (proc->agc != op) {
                proc->agc = op;
                if (op > 0) {
                    apm->gain_control()->set_compression_gain_db(op - 1);
                }
                apm->gain_control()->Enable(!!op);
            }
            break;

        case webrtc_control_hpass:
            op = !!op;
            if (proc->hpass != op) {
                proc->hpass = op;
                proc->apm->high_pass_filter()->Enable(op);
            }
            break;

        case webrtc_control_vad:
            if (op > VoiceDetection::kHighLikelihood) {
                op = VoiceDetection::kHighLikelihood;
            }

            if (proc->vad != op) {
                proc->vad = op;
                uint32_t level = ((uint32_t) VoiceDetection::kHighLikelihood) - op;
                apm->voice_detection()->set_likelihood((VoiceDetection::Likelihood) level);
                apm->voice_detection()->Enable(!!op);
            }
            break;

        case webrtc_control_ns:
            if (op > NoiseSuppression::kVeryHigh) {
                op = NoiseSuppression::kVeryHigh;
            }

            if (proc->ns != op) {
                proc->ns = op;
                apm->noise_suppression()->set_level((NoiseSuppression::Level) op);
                apm->noise_suppression()->Enable(!!op);
            }
            break;

        case webrtc_control_reserve:
            *(uint32_t *) args = proc->in.bytes_10ms;
            break;

        case webrtc_pcm_volume:
        {
            int32_t* intp = (int32_t *) args;
            if (intp[0] != -1) {
                proc->volume[0] = intp[0];
                trace_change(proc->volume[0], NULL);
            }

            if (intp[1] != -1) {
                proc->volume[1] = intp[1];
                trace_change(proc->volume[1], NULL);
            }

            break;
        }

        default:
            break;
    }
}

void* sigproc_get(int type, audio_format* fmt)
{
    static webrtc_sigproc obj;
    webrtc_sigproc* proc = &obj;
    AudioProcessing* apm = proc->apm;
    if (type != sigproc_webrtc) {
        errno = EINVAL;
        return NULL;
    }

    apm->set_num_channels(fmt->pcm->channel, fmt->pcm->channel);
    apm->set_num_reverse_channels(fmt->pcm->channel);
    apm->set_sample_rate_hz(fmt->pcm->samrate);

    apm->gain_control()->set_mode(GainControl::kAdaptiveDigital);
    apm->gain_control()->enable_limiter(true);
    apm->gain_control()->set_compression_gain_db(0);
    apm->gain_control()->set_target_level_dbfs(3);

    apm->voice_detection()->set_likelihood(VoiceDetection::kHighLikelihood);
    apm->voice_detection()->set_frame_size_ms(10);
    apm->noise_suppression()->set_level(NoiseSuppression::kVeryHigh);

    apm->echo_control_mobile()->enable_comfort_noise(false);
    apm->echo_control_mobile()->set_routing_mode(EchoControlMobile::kLoudSpeakerphone);
    apm->echo_cancellation()->set_suppression_level(EchoCancellation::kHighSuppression);

    proc->in.mbuf = NULL;
    proc->in.fmt = fmt;
    proc->in.bytes_10ms = fmt->pcm->samrate * fmt->pcm->sambits * fmt->pcm->channel / 800;
    proc->in.bytes_used = 0;
    proc->in.frame.sample_rate_hz_ = fmt->pcm->samrate;
    proc->in.frame.num_channels_ = fmt->pcm->channel;
    proc->in.frame.samples_per_channel_ = proc->in.bytes_10ms * 8 / fmt->pcm->sambits;
    proc->in.frame.speech_type_ = AudioFrame::kNormalSpeech;
    proc->in.frame.vad_activity_ = AudioFrame::kVadUnknown;

    proc->out.mbuf = NULL;
    proc->out.fmt = fmt;
    proc->out.bytes_10ms = fmt->pcm->samrate * fmt->pcm->sambits * fmt->pcm->channel / 800;
    proc->out.bytes_used = 0;
    proc->out.frame.sample_rate_hz_ = fmt->pcm->samrate;
    proc->out.frame.num_channels_ = fmt->pcm->channel;
    proc->out.frame.samples_per_channel_ = proc->in.bytes_10ms * 8 / fmt->pcm->sambits;
    proc->out.frame.speech_type_ = AudioFrame::kNormalSpeech;

    proc->type = sigproc_webrtc_none;
    proc->delay = 0;
    proc->agc = 0;
    proc->hpass = 0;
    proc->ns = 0;
    proc->vad = 0;
    return (void *) proc;
}

void sigproc_put(void* sigp)
{
    uint32_t type = *(uint32_t *) sigp;
    if (type == sigproc_webrtc_mb || type == sigproc_webrtc_pc || type == sigproc_webrtc_none) {
        webrtc_sigproc* proc = (webrtc_sigproc *) sigp;
        uint32_t val = sigproc_webrtc_none;
        webrtc_control(proc, webrtc_control_aec, &val);

        val = 0;
        webrtc_control(proc, webrtc_control_agc, &val);
        webrtc_control(proc, webrtc_control_hpass, &val);
        webrtc_control(proc, webrtc_control_vad, &val);
        webrtc_control(proc, webrtc_control_ns, &val);
    } else {
        my_assert(0);
    }
}

void sigproc_control(void* sigp, int cmd, void* args)
{
    uint32_t type = *(uint32_t *) sigp;
    if (type == sigproc_webrtc_mb || type == sigproc_webrtc_pc || type == sigproc_webrtc_none) {
        webrtc_sigproc* proc = (webrtc_sigproc *) sigp;
        webrtc_control(proc, cmd, args);
    } else {
        my_assert(0);
    }
}

static __attribute__((unused)) void low_pass(Filter* filter, char* buf, int bytes)
{
    int16_t* sp = (int16_t *) buf;
    bytes /= 2;
    for (int i = 0; i < bytes; ++i) {
        sp[i] = (int16_t) filter->do_sample((double) sp[i]);
    }
}

static void adjust_volume_out(audio_format* fmt, char* buf, int bytes, int32_t volume)
{
    uint32_t size = fmt->pcm->sambits / 8;
    if (__builtin_expect((size == 2), 1)) {
        for (int i = 0; i < bytes; i += 2) {
            int16_t* sp = (int16_t *) &buf[i];
            int32_t vol = (((int32_t) sp[0]) * 65535) / volume;
            sp[0] = (int16_t) vol;
        }
    } else {
        my_assert(0);
    }
}

void sigproc_pcm_notify(void* sigp, char* pcm, uint32_t len, uint32_t delay)
{
    uint32_t type = *(uint32_t *) sigp;
    if (type != sigproc_webrtc_mb && type != sigproc_webrtc_pc && type != sigproc_webrtc_none) {
        my_assert(0);
        return;
    }

    webrtc_sigproc* proc = (webrtc_sigproc *) sigp;
    if (proc->volume[0] != 65535) {
        adjust_volume_out(proc->out.fmt, pcm, len, proc->volume[0]);
    }

    proc->delay = delay;
    AudioProcessing* apm = proc->apm;
    char* buf = (char *) proc->out.frame.data_;

    while (len > 0) {
        uint32_t bytes = proc->out.bytes_10ms - proc->out.bytes_used;
        if (bytes > len) {
            bytes = len;
        }

        memcpy(&buf[proc->out.bytes_used], pcm, bytes);
        proc->out.bytes_used += bytes;
        if (proc->out.bytes_used != proc->out.bytes_10ms) {
            break;
        }

        int n = apm->AnalyzeReverseStream(&proc->out.frame);
        my_assert(n == 0);
        proc->out.bytes_used = 0;
        len -= bytes;
        pcm += bytes;
    }
}

static void adjust_volume_in(audio_format* fmt, char* buf, uint32_t bytes, int32_t volume)
{
    uint32_t size = fmt->pcm->sambits / 8;
    if (__builtin_expect((size == 2), 1)) {
        for (int i = 0; i < bytes; i += 2) {
            int16_t* sp = (int16_t *) &buf[i];
            int32_t vol = sp[0] * volume / 65535;
            sp[0] = (int16_t) vol;
        }
    } else {
        my_assert(0);
    }
}

static int32_t check_amplitude(audio_format* fmt, char* buf, uint32_t bytes)
{
    int16_t amplitude[2] = {0};
    uint32_t size = fmt->pcm->sambits / 8;
    if (__builtin_expect((size == 2), 1)) {
        for (int i = 0; i < bytes; i += 2) {
            int16_t* sp = (int16_t *) &buf[i];
            if (amplitude[0] < sp[0]) {
                amplitude[0] = sp[0];
            }

            if (amplitude[1] > sp[0]) {
                amplitude[1] = sp[0];
            }

            if (amplitude[0] > amplitude[1] + 8000) {
                return 0;
            }
        }
    } else {
        my_assert(0);
    }
    return 1;
}

void sigproc_pcm_proc(void* sigp, struct my_buffer* mbuf, uint32_t delay)
{
    uint32_t type = *(uint32_t *) sigp;
    if (type != sigproc_webrtc_mb && type != sigproc_webrtc_pc && type != sigproc_webrtc_none) {
        my_assert(0);
        return;
    }

    webrtc_sigproc* proc = (webrtc_sigproc *) sigp;
    delay += proc->delay;
    trace_change(delay, NULL);

    AudioProcessing* apm = proc->apm;
    char* buf = (char *) proc->in.frame.data_;
    uint32_t len = (uint32_t) mbuf->length;
    char* pch = mbuf->ptr[1];
    mbuf->ptr[1] = pch - proc->in.bytes_10ms;
    char* base = mbuf->ptr[1];

    while (len > 0) {
        uint32_t bytes = proc->in.bytes_10ms - proc->in.bytes_used;
        if (bytes > len) {
            bytes = len;
        }
        len -= bytes;
        memcpy(&buf[proc->in.bytes_used], pch, bytes);
        proc->in.frame.vad_activity_ = AudioFrame::kVadUnknown;

        proc->in.bytes_used += bytes;
        if (proc->in.bytes_used != proc->in.bytes_10ms) {
            break;
        }
        proc->in.bytes_used = 0;

        if (proc->volume[1] != 65535) {
            adjust_volume_in(proc->in.fmt, buf, proc->in.bytes_10ms, proc->volume[1]);
        }

        apm->set_stream_delay_ms(delay);
        apm->ProcessStream(&proc->in.frame);
        if (proc->in.frame.vad_activity_ == AudioFrame::kVadPassive &&
            check_amplitude(proc->in.fmt, buf, proc->in.bytes_10ms))
        {
            memset(base, 0, proc->in.bytes_10ms);
        } else {
            memcpy(base, buf, proc->in.bytes_10ms);
        }

        pch += bytes;
        base += proc->in.bytes_10ms;
    }
    mbuf->length = (uint32_t) (base - mbuf->ptr[1]);
}
