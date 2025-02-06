
#include "h264.h"
#define FFMPEG_CODEC_ID AV_CODEC_ID_H264
#include "ffmpeg.h"

media_process_unit ffmpeg_h264_dec = {
    &h264_0x0.cc,
    &raw_0x0.cc,
    &ffmpeg_ops,
    10,
    mpu_decoder,
    "ffmpeg_h264_dec"
};
