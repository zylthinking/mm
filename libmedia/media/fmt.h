
#ifndef fmt_h
#define fmt_h

#include <stdint.h>
#include "mydef.h"
#include "fraction.h"

#define unkown_type 0
#define audio_type 1
#define video_type 2

#define codec_pcm 0
#define codec_silk 1
#define codec_speex 2
#define codec_aac 3
#define codec_opus 4
#define codec_aacplus 5

#define codec_pixel 10
#define codec_h264 11
#define codec_h265 12

typedef struct tag_fourcc {
    uint32_t code;
    uint32_t type;
    uint32_t header;
} fourcc;

typedef struct tag_pcm_format {
    uint8_t channel;
    uint8_t sambits;
    uint16_t samrate;
} pcm_format;

struct audio_format_operation;

struct silk_param {
    uint8_t frame_ms;
    uint32_t bitrate;
};

typedef struct tag_audio_format {
    fourcc* cc;
    pcm_format* pcm;
    struct audio_format_operation* ops;
    void** pparam;
    const char* name;
} audio_format;

struct audio_format_operation {
    fraction (*ms_from) (audio_format*, uint32_t, uint32_t bytes0_frame1);
    struct my_buffer* (*muted_frame_get) (audio_format*, uint32_t);
    uint32_t (*frame_muted) (struct my_buffer* mbuf);
    audio_format* (*raw_format) (audio_format* fmt);
};

#define video_type_unkown 0
#define video_type_idr    1
#define video_type_i      2
#define video_type_p      3
#define video_type_b      4
#define video_type_bref   5
#define video_type_sps  (1 << 7)
#define video_type_pps  (1 << 6)
#define video_type_sei  (1 << 5)
#define reference_able(t) (t == video_type_idr || t == video_type_i || t == video_type_p || t == video_type_bref)
#define reference_self(t) (t == video_type_idr || t == video_type_i || t == video_type_sei)
#define reference_others(t) (t == video_type_p || t == video_type_b || t == video_type_bref)

typedef struct {
    uint32_t stride;
    uint32_t height;
    char* ptr;
} video_panel;

typedef struct {
    uint16_t width;
    uint16_t height;
} pic_size;

typedef struct tag_pixel_format {
    uint32_t csp;
    uint32_t panels;
    pic_size* size;
} pixel_format;

typedef struct {
    uintptr_t fps;
    uintptr_t kbps;
    uintptr_t gop;
} video_param_t;

typedef struct {
    fourcc* cc;
    pixel_format* pixel;
    video_param_t* caparam;
    const char* name;
} video_format;

capi audio_format speex_8k16b1_mode8;

#define declare_audio_format(property) \
capi audio_format silk_##property; \
capi audio_format opus_##property; \
capi audio_format aac_##property; \
capi audio_format aacplus_##property; \
capi audio_format pcm_##property;

declare_audio_format(8k16b1);
declare_audio_format(8k16b2);
declare_audio_format(11k16b1);
declare_audio_format(11k16b2);
declare_audio_format(16k16b1);
declare_audio_format(16k16b2);
declare_audio_format(22k16b1);
declare_audio_format(22k16b2);
declare_audio_format(24k16b1);
declare_audio_format(24k16b2);
declare_audio_format(32k16b1);
declare_audio_format(32k16b2);
declare_audio_format(44k16b1);
declare_audio_format(44k16b2);
declare_audio_format(48k16b1);
declare_audio_format(48k16b2);

#define csp_any 0
#define csp_bgra 1
#define csp_rgb 2
#define csp_i420 3
#define csp_gbrp 4
#define csp_nv12 5
#define csp_nv12ref 6
#define csp_nv21 7
#define csp_nv21ref 8
#define csp_i420ref 9
#define csp_nr 10

static __attribute__((unused)) uintptr_t reference_format(video_format* fmt)
{
    if (fmt->pixel->csp == csp_nv12ref ||
        fmt->pixel->csp == csp_nv21ref ||
        fmt->pixel->csp == csp_i420ref)
    {
        return 1;
    }
    return 0;
}

capi const char* format_name(fourcc** fmt);
capi fraction pcm_bytes_from_ms(pcm_format* pcm, uint32_t ms);
capi fraction pcm_ms_from_bytes(pcm_format* pcm, uint32_t bytes);
capi intptr_t media_type_raw(fourcc** cc);
capi audio_format* audio_raw_format(pcm_format* fmt);
capi video_format* video_raw_format(pixel_format* pixel, uint32_t csp);
capi video_format* video_format_get(uintptr_t codecid, uintptr_t csp, uint16_t width, uint16_t height);
capi video_format* video_configuration(video_format* fmt, uintptr_t gop, uintptr_t fps, uintptr_t kbps);
capi fourcc** fourcc_get(int32_t codec, int32_t samrate_width, int32_t chans_height);
capi intptr_t media_same(fourcc** left, fourcc** right);
capi intptr_t panel_width(intptr_t idx, intptr_t width, intptr_t csp);
capi intptr_t panel_height(intptr_t idx, intptr_t height, intptr_t csp);
capi intptr_t pixels_to_bytes(intptr_t idx, intptr_t pixels, intptr_t csp);

#define media_type(fmt) ((fmt)->type)
#define media_has_header(fmt) ((fmt)->header)
#define to_audio_format(cc_pptr) ((audio_format *) cc_pptr)
#define to_video_format(cc_pptr) ((video_format *) cc_pptr)

#endif
