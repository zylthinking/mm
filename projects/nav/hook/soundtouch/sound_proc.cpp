
#include "sound_proc.h"
#include "SoundTouch.h"
#include "codec_hook.h"
#include "fmt.h"
#include "mbuf.h"
#include "media_buffer.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>

using namespace soundtouch;

class effect_selector {
public:
    static int pitch_random[4];
    static int pitch_old[1];
    static int pitch_male[1];
    static int pitch_female[2];
    static int pitch_child[3];
    static int pitch_subtle[1];

    int speech;
    int pitch;
    int timbre;
    int type;

    effect_selector()
    {
        srand48((unsigned int) time(NULL));
        speech = 0;
        pitch = 0;
        timbre = 0;
    }

    int random_select_pitch(int type)
    {
        int old_pitch = pitch;
        int n = (int) lrand48();
        if (type == (intptr_t) type_random) {
            pitch = pitch_random[n % elements(pitch_random)];
        } else if (type == (intptr_t) type_old) {
            pitch = pitch_old[n % elements(pitch_old)];
        } else if (type == (intptr_t) type_male) {
            pitch = pitch_male[n % elements(pitch_male)];
        } else if (type == (intptr_t) type_female) {
            pitch = pitch_female[n % elements(pitch_female)];
        } else if (type == (intptr_t) type_child) {
            pitch = pitch_child[n % elements(pitch_child)];
        } else if (type == (intptr_t) type_subtle) {
            pitch = pitch_subtle[n % elements(pitch_subtle)];
        } else {
            pitch = 0;
        }
        this->type = type;

        if (old_pitch != pitch) {
            return 1;
        }
        return 0;
    }
};

int effect_selector::pitch_random[] = {-4, -3, 3, 4};
int effect_selector::pitch_old[] = {-5};
int effect_selector::pitch_male[] = {-4};
int effect_selector::pitch_female[] = {5, 6};
int effect_selector::pitch_child[] = {8, 9, 10};
int effect_selector::pitch_subtle[] = {-4};

class audio_effector {
public:
    SoundTouch touch;
    effect_selector selector;
    pcm_format* pcm;
    media_buffer mediabuf;

    audio_effector()
    {
        pcm = NULL;
        mediabuf.seq = (uint32_t) -1;
        touch.setSetting(SETTING_SEQUENCE_MS, 40);
        touch.setSetting(SETTING_SEEKWINDOW_MS, 15);
        touch.setSetting(SETTING_OVERLAP_MS, 8);
        touch.setTempoChange(0);
    }

    void new_type(int type)
    {
        int n = selector.random_select_pitch(type);
        if (n == 1) {
            touch.setPitchSemiTones((float) selector.pitch / 4);
        }
    }

    void new_pcm_fmt(pcm_format* fmt)
    {
        if (pcm == fmt) {
            return;
        }
        pcm = fmt;
        touch.setSampleRate(pcm->samrate);
        touch.setChannels(pcm->channel);
    }

    void update_mediabuf(int samples)
    {
        mediabuf.seq += 1;
        mediabuf.pts += samples / pcm->samrate;
    }

    struct my_buffer* samples_proc(struct my_buffer* in)
    {
        int samples = 0;
        short* buf = NULL;
        if (__builtin_expect(in != NULL, 1)) {
            buf = (short *) in->ptr[1];
            samples = (int) (in->length / (2 * pcm->channel));
            touch.putSamples(buf, samples);

            if (mediabuf.seq == (uint32_t) -1) {
                mediabuf = *(media_buffer *) in->ptr[0];
                my_assert(mediabuf.seq != (uint32_t) -1);
            }
        } else {
            touch.flush();
        }

        int samples_get = touch.numSamples();
        int bytes = samples_get * 2 * pcm->channel;
        if (samples_get > samples) {
            int extra_bytes = (int) sizeof(media_buffer);
            
            struct my_buffer* mbuf = mbuf_alloc_2(bytes + extra_bytes);
            if (mbuf != NULL) {
                mbuf->ptr[1] += extra_bytes;
                mbuf->length -= extra_bytes;

                if (in != NULL) {
                    in->mop->free(in);
                }

                samples = samples_get;
                in = mbuf;
                buf = (short *) in->ptr[1];
            }
        } else if (in != NULL) {
            samples = samples_get;
            if (bytes == 0) {
                in->mop->free(in);
                in = NULL;
            } else {
                in->length = bytes;
            }
        }

        if (__builtin_expect(in != NULL, 1)) {
            samples_get = touch.receiveSamples(buf, samples);
            in->length = samples_get * 2 * pcm->channel;
            memcpy(in->ptr[0], &mediabuf, sizeof(mediabuf));
            update_mediabuf(samples_get);
        } else if (samples_get > 0) {
            logmsg("Oops: memory failed for the last flush\n");
        }
        return in;
    }
};

static audio_effector effector;
void effect_settype(int type)
{
    effector.new_type(type);
}

intptr_t soundtouch_proc(void* uptr, intptr_t identy, void* any)
{
    if (effector.selector.type == (intptr_t) type_none) {
        return 0;
    }

    codectx_t* ctx = (codectx_t *) any;
    audio_format* infmt = to_audio_format(ctx->left);
    audio_format* outfmt = to_audio_format(ctx->right);
    if (infmt->cc->code == outfmt->cc->code) {
        return 0;
    }

    if (infmt->cc->code == codec_pcm || outfmt->cc->code == codec_pcm) {
        effector.new_pcm_fmt(infmt->pcm);
        effector.mediabuf.pptr_cc = ctx->left;
        ctx->mbuf = effector.samples_proc(ctx->mbuf);
    }
    return 0;
}
