
#include "fmt_in.h"
#include "media_buffer.h"
#include "mbuf.h"
#include <string.h>

#define case_audio(codec) \
if (samrate == 8000) { \
    if (chans == 1) { \
        cc_pptr = &codec##_8k16b1.cc; \
    } else if (chans == 2) { \
        cc_pptr = &codec##_8k16b2.cc; \
    } \
} else if (samrate == 11025) { \
    if (chans == 1) { \
        cc_pptr = &codec##_11k16b1.cc; \
    } else if (chans == 2) { \
        cc_pptr = &codec##_11k16b2.cc; \
    } \
} else if (samrate == 16000) { \
    if (chans == 1) { \
        cc_pptr = &codec##_16k16b1.cc; \
    } else if (chans == 2) { \
        cc_pptr = &codec##_16k16b2.cc; \
    } \
} else if (samrate == 22050) { \
    if (chans == 1) { \
        cc_pptr = &codec##_22k16b1.cc; \
    } else if (chans == 2) { \
        cc_pptr = &codec##_22k16b2.cc; \
    } \
} else if (samrate == 24000) { \
    if (chans == 1) { \
        cc_pptr = &codec##_24k16b1.cc; \
    } else if (chans == 2) { \
        cc_pptr = &codec##_24k16b2.cc; \
    } \
} else if (samrate == 32000) { \
    if (chans == 1) { \
        cc_pptr = &codec##_32k16b1.cc; \
    } else if (chans == 2) { \
        cc_pptr = &codec##_32k16b2.cc; \
    } \
} else if (samrate == 44100) { \
    if (chans == 1) { \
        cc_pptr = &codec##_44k16b1.cc; \
    } else if (chans == 2) { \
        cc_pptr = &codec##_44k16b2.cc; \
    } \
} else if (samrate == 48000) { \
    if (chans == 1) { \
        cc_pptr = &codec##_48k16b1.cc; \
    } else if (chans == 2) { \
        cc_pptr = &codec##_48k16b2.cc; \
    } \
}

#define case_video(codecid) \
if (width == 640 && height == 480) { \
    cc_pptr = &codecid##_640x480.cc; \
} else if (width == 480 && height == 640) { \
    cc_pptr = &codecid##_480x640.cc; \
} else if (width == 288 && height == 352) { \
    cc_pptr = &codecid##_288x352.cc; \
} else if (width == 352 && height == 288) { \
    cc_pptr = &codecid##_352x288.cc; \
} else if (width == 640 && height == 360) { \
    cc_pptr = &codecid##_640x360.cc; \
} else if (width == 720 && height == 406) { \
    cc_pptr = &codecid##_720x406.cc; \
} else if (width == 480 && height == 360) { \
    cc_pptr = &codecid##_480x360.cc; \
} else if (width == 504 && height == 896) { \
    cc_pptr = &codecid##_504x896.cc; \
} else if (width == 1080 && height == 720) { \
    cc_pptr = &codecid##_1080x720.cc; \
} else if (width == 720 && height == 1080) { \
    cc_pptr = &codecid##_720x1080.cc; \
} else { \
    video_format* video = dynamic_video_format_get(codec_##codecid, csp_any, width, height); \
    my_assert(video != NULL); \
    cc_pptr = &video->cc; \
}

fourcc** fourcc_get(int32_t codec, int32_t samrate_width, int32_t chans_height)
{
    fourcc** cc_pptr = NULL;
    int32_t samrate = samrate_width;
    int32_t width = samrate_width;
    int32_t chans = chans_height;
    int height = chans_height;

    if (codec == codec_pcm) {
        case_audio(pcm);
    } else if (codec == codec_silk) {
        case_audio(silk);
    } else if (codec == codec_aac) {
        case_audio(aac);
    } else if (codec == codec_aacplus) {
        case_audio(aacplus);
    } else if (codec == codec_opus) {
        case_audio(opus);
    } else if (codec == codec_h264) {
        case_video(h264);
    } else if (codec == codec_h265) {
        case_video(h265);
    }

    if (cc_pptr == NULL) {
        logmsg("%d %d %d does not support yet\n", codec, samrate_width, chans_height);
    }
    return cc_pptr;
}

#undef case_video
#undef case_audio

#define case_resolution(scp, resolution) \
switch(scp) { \
    case csp_bgra: return &raw_##resolution##_bgra; \
    case csp_gbrp: return &raw_##resolution##_gbrp; \
    case csp_rgb: return &raw_##resolution##_rgb; \
    case csp_i420: return &raw_##resolution##_i420; \
    case csp_nv12: return &raw_##resolution##_nv12; \
    case csp_nv12ref: return &raw_##resolution##_nv12ref; \
    case csp_nv21: return &raw_##resolution##_nv21; \
    case csp_nv21ref: return &raw_##resolution##_nv21ref; \
    case csp_i420ref: return &raw_##resolution##_i420ref; \
    case csp_any: return &raw_##resolution; \
}

intptr_t media_type_raw(fourcc** cc)
{
    if (cc[0]->type == audio_type) {
        audio_format* fmt1 = to_audio_format(cc);
        audio_format* fmt2 = audio_raw_format(fmt1->pcm);
        return (fmt1 == fmt2);
    }
    return (cc[0]->code == codec_pixel);
}

video_format* video_raw_format(pixel_format* pixel, uint32_t csp)
{
    if (pixel->size == &size_0x0) {
        case_resolution(csp, 0x0);
    } else if (pixel->size == &size_640x480) {
        case_resolution(csp, 640x480);
    } else if (pixel->size == &size_480x640) {
        case_resolution(csp, 480x640);
    } else if (pixel->size == &size_640x360) {
        case_resolution(csp, 640x360);
    } else if (pixel->size == &size_288x352) {
        case_resolution(csp, 288x352);
    } else if (pixel->size == &size_352x288) {
        case_resolution(csp, 352x288);
    } else if (pixel->size == &size_720x406) {
        case_resolution(csp, 720x406);
    } else if (pixel->size == &size_480x360) {
        case_resolution(csp, 480x360);
    } else if (pixel->size == &size_504x896) {
        case_resolution(csp, 504x896);
    } else if (pixel->size == &size_1080x720) {
        case_resolution(csp, 1080x720);
    }
    return dynamic_video_raw_format(pixel, csp);
}

#define case_codec(codec, args...) \
if (width == 0 && height == 0) return &codec##_0x0; \
if (width == 640 && height == 480) return &codec##_640x480##args; \
if (width == 480 && height == 640) return &codec##_480x640##args; \
if (width == 640 && height == 360) return &codec##_640x360##args; \
if (width == 480 && height == 360) return &codec##_480x360##args; \
if (width == 720 && height == 406) return &codec##_720x406##args; \
if (width == 288 && height == 352) return &codec##_288x352##args; \
if (width == 352 && height == 288) return &codec##_352x288##args; \
if (width == 504 && height == 896) return &codec##_504x896##args; \
if (width == 1080 && height == 720) return &codec##_1080x720##args; \
if (width == 720 && height == 1080) return &codec##_1080x720##args; \

video_format* video_format_get(uintptr_t codecid, uintptr_t csp, uint16_t width, uint16_t height)
{
    my_assert(csp < csp_nr);
    if (codecid == codec_h264) {
        my_assert(csp == csp_any);
        case_codec(h264);
    } else if (codecid == codec_h265) {
        my_assert(csp == csp_any);
        case_codec(h265);
    } else if (codecid == codec_pixel) {
        if (csp == csp_any) {
            case_codec(raw);
        } else if (csp == csp_bgra) {
            case_codec(raw, _bgra);
        } else if (csp == csp_rgb) {
            case_codec(raw, _rgb);
        } else if (csp == csp_i420) {
            case_codec(raw, _i420);
        } else if (csp == csp_gbrp) {
            case_codec(raw, _gbrp);
        } else if (csp == csp_nv12) {
            case_codec(raw, _nv12);
        } else if (csp == csp_nv12ref) {
            case_codec(raw, _nv12ref);
        } else if (csp == csp_nv21) {
            case_codec(raw, _nv21);
        } else if (csp == csp_nv21ref) {
            case_codec(raw, _nv21ref);
        } else if (csp == csp_i420ref) {
            case_codec(raw, _i420ref);
        }
    } else {
        my_assert2(0, "codecid %d not support yet", codecid);
        return NULL;
    }
    return dynamic_video_format_get(codecid, csp, width, height);
}
#undef case_codec

video_format* video_configuration(video_format* fmt, uintptr_t gop, uintptr_t fps, uintptr_t kbps)
{
    fmt->caparam->fps = fps;
    fmt->caparam->gop = gop;
    fmt->caparam->kbps = kbps;
    return fmt;
}

fraction pcm_ms_from_bytes(pcm_format* pcm, uint32_t bytes)
{
    fraction frac;
    frac.num = bytes * 8000;
    frac.den = pcm->channel * pcm->sambits * pcm->samrate;
    if (frac.den <= 0xffff && frac.num <= 0xffff) {
        return frac;
    }

    uint32_t n = bgcd(frac.num, frac.den);
    frac.num /= n;
    frac.den /= n;
    return frac;
}

fraction pcm_bytes_from_ms(pcm_format* pcm, uint32_t ms)
{
    fraction frac;
    frac.num = pcm->samrate * pcm->sambits * pcm->channel * ms;
    frac.den = 8000;
    if (frac.num <= 0xffff) {
        return frac;
    }

    uint32_t n = bgcd(frac.num, frac.den);
    frac.num /= n;
    frac.den /= n;
    return frac;
}

static inline intptr_t same_size(video_format* left, video_format* right)
{
    if (right->pixel->size == &size_0x0) {
        return 1;
    }
    return (left->pixel->size == right->pixel->size);
}

intptr_t media_same(fourcc** left, fourcc** right)
{
    if (left == right) {
        return 1;
    }

    if (*left != *right || media_type(*left) == audio_type) {
        return 0;
    }

    video_format* fmt_left = to_video_format(left);
    video_format* fmt_right = to_video_format(right);
    if (!same_size(fmt_left, fmt_right)) {
        return 0;
    }

    intptr_t n = (fmt_right->pixel->csp == csp_any);
    return n;
}

intptr_t panel_width(intptr_t idx, intptr_t width, intptr_t csp)
{
    switch (csp) {
        case csp_bgra:
        case csp_gbrp:
        {
            return width;
        }

        case csp_nv12:
        case csp_nv12ref:
        case csp_nv21:
        case csp_nv21ref:
        case csp_i420:
        case csp_i420ref:
        {
            if (idx == 0) {
                return width;
            }
            return width / 2;
        }
    }
    return -1;
}

intptr_t panel_height(intptr_t idx, intptr_t height, intptr_t csp)
{
    switch (csp) {
        case csp_bgra:
        case csp_gbrp:
        {
            return height;
        }

        case csp_nv12:
        case csp_nv12ref:
        case csp_nv21:
        case csp_nv21ref:
        case csp_i420:
        case csp_i420ref:
        {
            if (idx == 0) {
                return height;
            }
            return height / 2;
        }
    }
    return -1;
}

intptr_t pixels_to_bytes(intptr_t idx, intptr_t pixels, intptr_t csp)
{
    switch (csp) {
        case csp_bgra:
        {
            return pixels * 4;
        }

        case csp_i420:
        case csp_gbrp:
        {
            return pixels;
        }

        case csp_nv12:
        case csp_nv12ref:
        case csp_nv21:
        case csp_nv21ref:
        case csp_i420ref:
        {
            if (idx == 0) {
                return pixels;
            }
            return pixels * 2;
        }
    }
    return -1;
}

const char* format_name(fourcc** fmt)
{
    if (audio_type == media_type(*fmt)) {
        return to_audio_format(fmt)->name;
    }
    return to_video_format(fmt)->name;
}
