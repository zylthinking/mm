
#ifndef writer_h
#define writer_h

#include "mydef.h"
#include "mfio.h"
#include "media_buffer.h"

capi void* writer_open(mfio_writer_t* mfio, const char* filename, media_buffer* audio, media_buffer* video);
capi void* add_stream(mfio_writer_t* mfio, void* writer, media_iden_t iden, uint32_t mask, intptr_t sync);
capi void delete_stream(void* mptr);
capi void writer_close(mfio_writer_t* mfio, void* writer);

#endif
