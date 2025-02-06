
#ifndef mfio_mp4_h
#define mfio_mp4_h

#include "mfio.h"
#include "media_buffer.h"

extern mfio_writer_t mp4_mfio_writer;
typedef struct my_buffer* (*pixel_translate_t) (struct my_buffer*);
capi void mp4_video_config(void* mptr, intptr_t optimization, pixel_translate_t callback);

#endif
