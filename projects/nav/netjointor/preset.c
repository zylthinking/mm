
#include "preset.h"
#include "fmt_in.h"
#include "proto.h"
#include "media_buffer.h"

void frame_information_print(struct my_buffer* mbuf)
{
#if 0
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    intptr_t n = media->frametype;
    if (0 != (n & 0xf0)) {
        n = 6;
    }

    static intptr_t nr1 = 0;
    static intptr_t nr2 = 0;
    static intptr_t nr3 = 1;
    if (n == 1 && nr1 > 0) {
        if (nr3 > 1) {
            nr2 += nr1;
            logmsg("seq: %llu, packet: %d, average = %d\n", media->seq, (int) nr1, (int) (nr2 / (nr3 - 1)));
        }
        nr1 = 0;
        nr3++;
    }

#if 0
    static const char* frame_name[] = {
        "video_type_unkown",
        "video_type_idr",
        "video_type_i",
        "video_type_p",
        "video_type_b",
        "video_type_bref",
        "video_type_sps"
    };
    logmsg("seq: %llu, pts: %llu, frame_type = %s, bytes = %u\n",
           media->seq, media->pts, frame_name[n], (uint32_t) mbuf->length);
#endif

    if (n != 0) {
        nr1 += (mbuf->length + mtu - 1) / mtu;
    }
#endif
}

__attribute__((constructor))
static void preset()
{
    h264_288x352.caparam->fps = 10;
    h264_288x352.caparam->kbps = 100;
    h264_288x352.caparam->gop = 10;

    h264_352x288.caparam->fps = 10;
    h264_352x288.caparam->kbps = 100;
    h264_352x288.caparam->gop = 10;

    h264_640x480.caparam->fps = 10;
    h264_640x480.caparam->kbps = 250;
    h264_640x480.caparam->gop = 10;

    h264_480x640.caparam->fps = 10;
    h264_480x640.caparam->kbps = 250;
    h264_480x640.caparam->gop = 10;

    h264_1080x720.caparam->fps = 10;
    h264_1080x720.caparam->kbps = 800;
    h264_1080x720.caparam->gop = 10;

    h264_720x1080.caparam->fps = 10;
    h264_720x1080.caparam->kbps = 800;
    h264_720x1080.caparam->gop = 10;
}
