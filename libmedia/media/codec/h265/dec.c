
#define FFMPEG_CODEC_ID AV_CODEC_ID_H265
#include "ffmpeg.h"

media_process_unit ffmpeg_h265_dec = {
    &h265_0x0.cc,
    &raw_0x0.cc,
    &ffmpeg_ops,
    10,
    mpu_decoder,
    "ffmpeg_h265_dec"
};
