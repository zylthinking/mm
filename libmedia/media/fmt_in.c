
#include "fmt_in.h"
#include "media_buffer.h"
#include "mbuf.h"
#include "lock.h"
#include "mem.h"
#include <string.h>

// ---------------------------- raw format defination --------------------------------

static fourcc pcm_cc = {
    codec_pcm, audio_type, 0
};

static pcm_format pcmfmt_8k16b1 = {
    1, 16, 8000
};

static pcm_format pcmfmt_8k16b2 = {
    2, 16, 8000
};

static pcm_format pcmfmt_11k16b1 = {
    1, 16, 11025
};

static pcm_format pcmfmt_11k16b2 = {
    2, 16, 11025
};

static pcm_format pcmfmt_16k16b1 = {
    1, 16, 16000
};

static pcm_format pcmfmt_16k16b2 = {
    2, 16, 16000
};

static pcm_format pcmfmt_22k16b1 = {
    1, 16, 22050
};

static pcm_format pcmfmt_22k16b2 = {
    2, 16, 22050
};

static pcm_format pcmfmt_24k16b1 = {
    1, 16, 24000
};

static pcm_format pcmfmt_24k16b2 = {
    2, 16, 24000
};

static pcm_format pcmfmt_32k16b1 = {
    1, 16, 32000
};

static pcm_format pcmfmt_32k16b2 = {
    2, 16, 32000
};

static pcm_format pcmfmt_44k16b1 = {
    1, 16, 44100
};

static pcm_format pcmfmt_44k16b2 = {
    2, 16, 44100
};

static pcm_format pcmfmt_48k16b1 = {
    1, 16, 48000
};

static pcm_format pcmfmt_48k16b2 = {
    2, 16, 48000
};

#define define_audio_format(codec, type) \
audio_format codec##_##type = { \
    &codec##_cc, &pcmfmt_##type, &codec##_operation, (void **) &codec##_##type##_param, #codec"_"#type \
};

#define define_audio_formats(codec) \
define_audio_format(codec, 8k16b1); \
define_audio_format(codec, 8k16b2); \
define_audio_format(codec, 11k16b1); \
define_audio_format(codec, 11k16b2); \
define_audio_format(codec, 16k16b1); \
define_audio_format(codec, 16k16b2); \
define_audio_format(codec, 22k16b1); \
define_audio_format(codec, 22k16b2); \
define_audio_format(codec, 24k16b1); \
define_audio_format(codec, 24k16b2); \
define_audio_format(codec, 32k16b1); \
define_audio_format(codec, 32k16b2); \
define_audio_format(codec, 44k16b1); \
define_audio_format(codec, 44k16b2); \
define_audio_format(codec, 48k16b1); \
define_audio_format(codec, 48k16b2);

// ---------------------------- SILK --------------------------------
#include "silk.h"

static struct silk_param silk_param_8k16b1 = {
    silk_mpf, 12 * 1000
};

static struct silk_param silk_param_16k16b1 = {
    silk_mpf, 24 * 1024
};

static struct silk_param* silk_8k16b1_param = &silk_param_8k16b1;
static struct silk_param* silk_8k16b2_param = NULL;
static struct silk_param* silk_11k16b1_param = NULL;
static struct silk_param* silk_11k16b2_param = NULL;
static struct silk_param* silk_16k16b1_param = &silk_param_16k16b1;
static struct silk_param* silk_16k16b2_param = NULL;
static struct silk_param* silk_22k16b1_param = NULL;
static struct silk_param* silk_22k16b2_param = NULL;
static struct silk_param* silk_24k16b1_param = NULL;
static struct silk_param* silk_24k16b2_param = NULL;
static struct silk_param* silk_32k16b1_param = NULL;
static struct silk_param* silk_32k16b2_param = NULL;
static struct silk_param* silk_44k16b1_param = NULL;
static struct silk_param* silk_44k16b2_param = NULL;
static struct silk_param* silk_48k16b1_param = NULL;
static struct silk_param* silk_48k16b2_param = NULL;

static fraction silk_ms_from_frames(audio_format* fmt, uint32_t packets, uint32_t bytes0_frame1)
{
    my_assert(bytes0_frame1 == 1);
    struct silk_param* param = *(struct silk_param **) fmt->pparam;
    fraction frac;
    frac.num = param->frame_ms * packets;
    frac.den = 1;
    return frac;
}

static struct audio_format_operation silk_operation = {
    silk_ms_from_frames, silk_muted_frame_get, silk_frame_muted, NULL
};

static fourcc silk_cc = {
    codec_silk, audio_type, 0
};
define_audio_formats(silk);

// ---------------------------- SPEEX --------------------------------

static fourcc speex_cc = {
    codec_speex, audio_type, 0
};

static unsigned char eight = 8;
static void* p8 = &eight;
audio_format speex_8k16b1_mode8 = {
    &speex_cc, &pcmfmt_8k16b1, NULL, &p8, "speex_8k16b1_mode8"
};

// ---------------------------- AAC --------------------------------

static fourcc aac_cc = {
    codec_aac, audio_type, 1
};

static fraction aac_ms_from_frames(audio_format* fmt, uint32_t packets, uint32_t byte0_frame1)
{
    my_assert(byte0_frame1 == 1);
    my_assert2(0, "aac does not implement network feature yet");
    fraction frac;
    return frac;
}

static struct my_buffer* aac_muted_frame_get(audio_format* fmt, uint32_t ms)
{
    return NULL;
}

static uint32_t aac_frame_muted(struct my_buffer* mbuf)
{
    return 0;
}

static struct audio_format_operation aac_operation = {
    aac_ms_from_frames, aac_muted_frame_get, aac_frame_muted, NULL
};

static struct silk_param* aac_8k16b1_param = NULL;
static struct silk_param* aac_8k16b2_param = NULL;
static struct silk_param* aac_11k16b1_param = NULL;
static struct silk_param* aac_11k16b2_param = NULL;
static struct silk_param* aac_16k16b1_param = NULL;
static struct silk_param* aac_16k16b2_param = NULL;
static struct silk_param* aac_22k16b1_param = NULL;
static struct silk_param* aac_22k16b2_param = NULL;
static struct silk_param* aac_24k16b1_param = NULL;
static struct silk_param* aac_24k16b2_param = NULL;
static struct silk_param* aac_32k16b1_param = NULL;
static struct silk_param* aac_32k16b2_param = NULL;
static struct silk_param* aac_44k16b1_param = NULL;
static struct silk_param* aac_44k16b2_param = NULL;
static struct silk_param* aac_48k16b1_param = NULL;
static struct silk_param* aac_48k16b2_param = NULL;
define_audio_formats(aac);

// ---------------------------- AAC PLUS--------------------------------

static fourcc aacplus_cc = {
    codec_aacplus, audio_type, 0
};

static fraction aacplus_ms_from_frames(audio_format* fmt, uint32_t packets, uint32_t byte0_frame1)
{
    my_assert(byte0_frame1 == 1);
    my_assert2(0, "aacplus does not implement network feature yet");
    fraction frac;
    return frac;
}

static struct my_buffer* aacplus_muted_frame_get(audio_format* fmt, uint32_t ms)
{
    return NULL;
}

static uint32_t aacplus_frame_muted(struct my_buffer* mbuf)
{
    return 0;
}

audio_format* aacplus_raw_format(audio_format* fmt)
{
    if (fmt == &aacplus_8k16b1 || fmt == &aacplus_8k16b2) {
        return &pcm_16k16b2;
    }

    if (fmt == &aacplus_11k16b1 || fmt == &aacplus_11k16b2) {
        return &pcm_22k16b2;
    }

    if (fmt == &aacplus_16k16b1 || fmt == &aacplus_16k16b2) {
        return &pcm_32k16b2;
    }

    if (fmt == &aacplus_22k16b1 || fmt == &aacplus_22k16b2) {
        return &pcm_44k16b2;
    }

    if (fmt == &aacplus_24k16b1 || fmt == &aacplus_24k16b2) {
        return &pcm_48k16b2;
    }

    return NULL;
}

static struct audio_format_operation aacplus_operation = {
    aacplus_ms_from_frames, aacplus_muted_frame_get, aacplus_frame_muted, aacplus_raw_format
};

static struct silk_param* aacplus_8k16b1_param = NULL;
static struct silk_param* aacplus_8k16b2_param = NULL;
static struct silk_param* aacplus_11k16b1_param = NULL;
static struct silk_param* aacplus_11k16b2_param = NULL;
static struct silk_param* aacplus_16k16b1_param = NULL;
static struct silk_param* aacplus_16k16b2_param = NULL;
static struct silk_param* aacplus_22k16b1_param = NULL;
static struct silk_param* aacplus_22k16b2_param = NULL;
static struct silk_param* aacplus_24k16b1_param = NULL;
static struct silk_param* aacplus_24k16b2_param = NULL;
static struct silk_param* aacplus_32k16b1_param = NULL;
static struct silk_param* aacplus_32k16b2_param = NULL;
static struct silk_param* aacplus_44k16b1_param = NULL;
static struct silk_param* aacplus_44k16b2_param = NULL;
static struct silk_param* aacplus_48k16b1_param = NULL;
static struct silk_param* aacplus_48k16b2_param = NULL;
define_audio_formats(aacplus);

// ---------------------------- OPUS --------------------------------

static fourcc opus_cc = {
    codec_opus, audio_type, 0
};

static fraction opus_ms_from_frames(audio_format* fmt, uint32_t packets, uint32_t byte0_frame1)
{
    my_assert(byte0_frame1 == 1);
    my_assert2(0, "opus does not implement network feature yet");
    fraction frac;
    return frac;
}

static struct my_buffer* opus_muted_frame_get(audio_format* fmt, uint32_t ms)
{
    return NULL;
}

static uint32_t opus_frame_muted(struct my_buffer* mbuf)
{
    return 0;
}

static struct audio_format_operation opus_operation = {
    opus_ms_from_frames, opus_muted_frame_get, opus_frame_muted, NULL
};

static struct silk_param* opus_8k16b1_param = NULL;
static struct silk_param* opus_8k16b2_param = NULL;
static struct silk_param* opus_11k16b1_param = NULL;
static struct silk_param* opus_11k16b2_param = NULL;
static struct silk_param* opus_16k16b1_param = NULL;
static struct silk_param* opus_16k16b2_param = NULL;
static struct silk_param* opus_22k16b1_param = NULL;
static struct silk_param* opus_22k16b2_param = NULL;
static struct silk_param* opus_24k16b1_param = NULL;
static struct silk_param* opus_24k16b2_param = NULL;
static struct silk_param* opus_32k16b1_param = NULL;
static struct silk_param* opus_32k16b2_param = NULL;
static struct silk_param* opus_44k16b1_param = NULL;
static struct silk_param* opus_44k16b2_param = NULL;
static struct silk_param* opus_48k16b1_param = NULL;
static struct silk_param* opus_48k16b2_param = NULL;
define_audio_formats(opus);

// ---------------------------- PCM --------------------------------

static fraction raw_ms_from_bytes(audio_format* raw, uint32_t bytes, uint32_t byte0_frame1)
{
    my_assert(byte0_frame1 == 0);
    return pcm_ms_from_bytes(raw->pcm, bytes);
}

static struct my_buffer* raw_muted_frame_get(audio_format* fmt, uint32_t ms)
{
    pcm_format* pcm = fmt->pcm;
    fraction frac = pcm_bytes_from_ms(pcm, ms);
    uint32_t bytes = frac.num / frac.den;
    struct my_buffer* mbuf = mbuf_alloc_2(sizeof(media_buffer) + bytes);
    if (mbuf != NULL) {
        mbuf->ptr[1] = mbuf->ptr[0] + sizeof(media_buffer);
        mbuf->length -= sizeof(media_buffer);
        memset(mbuf->ptr[1], 0, mbuf->length);

        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        memset(media->vp, 0, sizeof(media->vp));
        media->frametype = 0;
        media->angle = 0;
        media->fragment[0] = 0;
        media->fragment[1] = 1;
        media->iden = media_id_unkown;
        media->pptr_cc = &fmt->cc;
        media->pts = 0;
        media->seq = 0;
        media->vp[0].ptr = mbuf->ptr[1];
        media->vp[0].stride = (uint32_t) mbuf->length;
    }
    return mbuf;
}

static uint32_t raw_frame_muted(struct my_buffer* mbuf)
{
    static char zero[1024] = {0};
    uint32_t muted = 1;

    uintptr_t bytes = mbuf->length;
    char* pch = mbuf->ptr[1];
    while (bytes > 0) {
        size_t nb = sizeof(zero);
        if (bytes < nb) {
            nb = bytes;
        }

        int n = memcmp(pch, zero, nb);
        if (n != 0) {
            muted = 0;
            break;
        }
        bytes -= nb;
    }
    return muted;
}

static struct audio_format_operation pcm_operation = {
    raw_ms_from_bytes, raw_muted_frame_get, raw_frame_muted, NULL
};

static struct silk_param* pcm_8k16b1_param = NULL;
static struct silk_param* pcm_8k16b2_param = NULL;
static struct silk_param* pcm_11k16b1_param = NULL;
static struct silk_param* pcm_11k16b2_param = NULL;
static struct silk_param* pcm_16k16b1_param = NULL;
static struct silk_param* pcm_16k16b2_param = NULL;
static struct silk_param* pcm_22k16b1_param = NULL;
static struct silk_param* pcm_22k16b2_param = NULL;
static struct silk_param* pcm_24k16b1_param = NULL;
static struct silk_param* pcm_24k16b2_param = NULL;
static struct silk_param* pcm_32k16b1_param = NULL;
static struct silk_param* pcm_32k16b2_param = NULL;
static struct silk_param* pcm_44k16b1_param = NULL;
static struct silk_param* pcm_44k16b2_param = NULL;
static struct silk_param* pcm_48k16b1_param = NULL;
static struct silk_param* pcm_48k16b2_param = NULL;
define_audio_formats(pcm);

audio_format* audio_raw_format(pcm_format* pcm)
{
#define case_pcmfmt(x) \
if (pcm == &pcmfmt_##x) { \
    return &pcm_##x; \
}
    case_pcmfmt(8k16b1);
    case_pcmfmt(8k16b2);
    case_pcmfmt(11k16b1);
    case_pcmfmt(11k16b2);
    case_pcmfmt(16k16b1);
    case_pcmfmt(16k16b2);
    case_pcmfmt(22k16b1);
    case_pcmfmt(22k16b2);
    case_pcmfmt(24k16b1);
    case_pcmfmt(24k16b2);
    case_pcmfmt(32k16b1);
    case_pcmfmt(32k16b2);
    case_pcmfmt(44k16b1);
    case_pcmfmt(44k16b2);
    case_pcmfmt(48k16b1);
    case_pcmfmt(48k16b2);
#undef case_pcmfmt
    return NULL;
}

// ---------------------------- video --------------------------------

#define define_video_param(w, h) \
pic_size size_##w##x##h = {w, h}; \
static video_param_t param_##w##x##h = {10, 100, 34};

#define define_pixel_format(csp, panels, resolution) \
static pixel_format csp##_##resolution = { \
    csp_##csp, panels, &size_##resolution \
}

#define define_video_format1(codec, resolution, csp) \
video_format codec##_##resolution##_##csp = { \
    &codec##_cc, &csp##_##resolution, &param_##resolution, #codec"_"#resolution"_"#csp \
}

#define define_video_format2(codec, resolution, csp) \
video_format codec##_##resolution = { \
    &codec##_cc, &csp##_##resolution, &param_##resolution, #codec"_"#resolution"_"#csp \
}

static uint8_t panels[csp_nr] = {
    [csp_any] = 0,
    [csp_bgra] = 1,
    [csp_rgb] = 1,
    [csp_gbrp] = 3,
    [csp_i420] = 3,
    [csp_nv12] = 2,
    [csp_nv12ref] = 2,
    [csp_nv21] = 2,
    [csp_nv21ref] = 2,
    [csp_i420ref] = 3,
};

#define define_pixel_formats_with_resolution(resolution) \
define_pixel_format(any, 0, resolution); \
define_pixel_format(bgra, 1, resolution); \
define_pixel_format(rgb, 1, resolution); \
define_pixel_format(gbrp, 3, resolution); \
define_pixel_format(i420, 3, resolution); \
define_pixel_format(nv12, 2, resolution); \
define_pixel_format(nv12ref, 2, resolution); \
define_pixel_format(nv21, 2, resolution); \
define_pixel_format(nv21ref, 2, resolution); \
define_pixel_format(i420ref, 3, resolution);

#define define_video_formats_with_resolution(resolution) \
define_video_format1(raw, resolution, bgra); \
define_video_format1(raw, resolution, rgb); \
define_video_format1(raw, resolution, gbrp); \
define_video_format1(raw, resolution, i420); \
define_video_format1(raw, resolution, i420ref); \
define_video_format1(raw, resolution, nv12); \
define_video_format1(raw, resolution, nv12ref); \
define_video_format1(raw, resolution, nv21); \
define_video_format1(raw, resolution, nv21ref); \
define_video_format2(raw, resolution, any); \
define_video_format2(h264, resolution, any); \
define_video_format2(h265, resolution, any);

static fourcc raw_cc = {
    codec_pixel, video_type, 0
};

static fourcc h264_cc = {
    codec_h264, video_type, 1
};

static fourcc h265_cc = {
    codec_h265, video_type, 1
};

#define define_video(w, h) \
define_video_param(w, h); \
define_pixel_formats_with_resolution(w##x##h); \
define_video_formats_with_resolution(w##x##h);

define_video(0, 0);
define_video(640, 480);
define_video(480, 640);
define_video(640, 360);
define_video(480, 360);
define_video(720, 406);
define_video(288, 352);
define_video(352, 288);
define_video(504, 896);
define_video(1080, 720);
define_video(720, 1080);

typedef struct {
    struct list_head entry;
    pic_size size;
    video_param_t param;
    video_format h264;
    video_format h265;
    video_format raw[csp_nr];
    pixel_format pixel[csp_nr];
} video_struct_t;

static rwlock_t rwlk = rw_lock_initial;
static struct list_head video_formats = LIST_HEAD_INIT(video_formats);

video_format* dynamic_video_format_get(uintptr_t codec, uintptr_t csp, uint16_t width, uint16_t height)
{
    video_struct_t* video = NULL;
    struct list_head* ent;
    read_lock(&rwlk);

    for (ent = video_formats.next; ent != &video_formats; ent = ent->next) {
        video = list_entry(ent, video_struct_t, entry);
        if (width == video->size.width && height == video->size.height) {
            read_unlock(&rwlk);
            if (codec == codec_h264) {
                return &video->h264;
            } else if (codec == codec_h265) {
                return &video->h265;
            }
            return &video->raw[csp];
        }
    }

    ent = ent->prev;
    read_unlock(&rwlk);

    video = (video_struct_t *) my_malloc(sizeof(video_struct_t));
    if (video == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    memset(&video->param, 0, sizeof(video_param_t));
    video->size.width = width;
    video->size.height = height;

    video->h264.cc = &h264_cc;
    video->h264.pixel = &video->pixel[csp_any];
    video->h264.caparam = &video->param;
    video->h264.name = "dynamic_h264";

    video->h265.cc = &h265_cc;
    video->h265.pixel = &video->pixel[csp_any];
    video->h265.caparam = &video->param;
    video->h265.name = "dynamic_h265";

    for (int i = 0; i < csp_nr; ++i) {
        video->raw[i].cc = &raw_cc;
        video->raw[i].pixel = &video->pixel[i];
        video->raw[i].caparam = &video->param;
        video->raw[i].name = "dynamic_raw";
    }

    for (int i = 0; i < csp_nr; ++i) {
        video->pixel[i].csp = i;
        video->pixel[i].panels = panels[i];
        video->pixel[i].size = &video->size;
    }

    write_lock(&rwlk);
    for (ent = ent->next; ent != &video_formats; ent = ent->next) {
        video_struct_t* v = list_entry(ent, video_struct_t, entry);
        if (width == v->size.width && height == v->size.height) {
            write_unlock(&rwlk);
            my_free(video);
            if (codec == codec_h264) {
                return &v->h264;
            } else if (codec == codec_h265) {
               return &v->h265;
            }
            return &v->raw[csp];
        }
    }
    list_add_tail(&video->entry, &video_formats);
    write_unlock(&rwlk);

    if (codec == codec_h264) {
        return &video->h264;
    } else if (codec == codec_h265) {
        return &video->h265;
    }
    return &video->raw[csp];
}

video_format* dynamic_video_raw_format(pixel_format* pixel, uint32_t csp)
{
    intptr_t idx = pixel->csp;
    video_struct_t* video = container_of(pixel, video_struct_t, pixel[idx]);
    return &video->raw[csp];
}
