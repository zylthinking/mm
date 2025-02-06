

#include "fileit.h"
#include "ffmpegfile.h"

extern filefmt_t m3u8_fmt;
extern filefmt_t mp4_fmt;
extern filefmt_t rtmp_fmt;

static filefmt_t* files[] = {
    &m3u8_fmt,
    &mp4_fmt,
    &rtmp_fmt
};

filefmt_t* fiflefmt_get(intptr_t magic)
{
    for (int i = 0; i < elements(files); ++i) {
        if (files[i]->magic == magic) {
            return files[i];
        }
    }
    return NULL;
}