
#ifndef fmt_in_h
#define fmt_in_h

#include "mydef.h"
#include "fmt.h"

#define declare_video_format(resolution) \
capi pic_size size_##resolution; \
capi video_format raw_##resolution; \
capi video_format raw_##resolution##_bgra; \
capi video_format raw_##resolution##_rgba; \
capi video_format raw_##resolution##_rgb; \
capi video_format raw_##resolution##_rgb565; \
capi video_format raw_##resolution##_gbrp; \
capi video_format raw_##resolution##_i420; \
capi video_format raw_##resolution##_nv12; \
capi video_format raw_##resolution##_nv12ref; \
capi video_format raw_##resolution##_nv21; \
capi video_format raw_##resolution##_nv21ref; \
capi video_format raw_##resolution##_i420ref; \
capi video_format h264_##resolution; \
capi video_format h265_##resolution;

declare_video_format(0x0);
declare_video_format(720x406);
declare_video_format(640x480);
declare_video_format(640x360);
declare_video_format(480x640);
declare_video_format(480x360);
declare_video_format(288x352);
declare_video_format(352x288);
declare_video_format(504x896);
declare_video_format(1080x720);
declare_video_format(720x1080);

capi video_format* dynamic_video_raw_format(pixel_format* pixel, uint32_t csp);
capi video_format* dynamic_video_format_get(uintptr_t codec, uintptr_t csp, uint16_t width, uint16_t height);

#endif
