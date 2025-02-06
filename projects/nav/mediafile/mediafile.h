
#ifndef mediafile_h
#define mediafile_h

#include "mydef.h"
#include "media_buffer.h"
#include <stdint.h>
#include "list_head.h"

#pragma pack(4)
struct file_header {
    char magic[4];
    int32_t be_version;
    int32_t be_bytes;
    int32_t be_file_ms;
    int32_t be_codec;
    int32_t be_frame_ms;
    int32_t be_samrate;
    int32_t be_chans;
    int32_t be_crc;
};
#pragma pack()

capi void* media_file_open(const char* fullpath, int read0_write1);
capi struct file_header* media_file_header(void* ctx);
capi int memdia_file_write(void* ctx, media_buffer* media, const char* buffer, int length);
capi int memdia_file_read(void* any, struct list_head* headp, const fraction* f);
capi void media_file_close(void* ctx);

// this function is not neccessary indeed.
// memdia_file_read return value's sum can do this.
// but I have to play the stupid already existing file recored in android.
capi int media_file_tell(void* any);

#endif
