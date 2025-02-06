
#ifndef mfio_h
#define mfio_h

#include "mydef.h"
#include "media_buffer.h"

typedef struct {
    void* (*open)(const char* file, media_buffer* audio, media_buffer* video);
    void* (*add_stream)(void* writer);
    void (*delete_stream)(void* strm);
    intptr_t (*write)(void* strm, struct my_buffer* mbuf);
    void (*close)(void* writer);
} mfio_writer_t;

__attribute__((unused)) static int write_n(int fd, void* buf, size_t bytes)
{
    char* pch = (char *) buf;

    do {
        ssize_t n = write(fd, pch, bytes);
        if (n == -1) {
            if (errno != EINTR) {
                break;
            }
            n = 0;
        }
        pch += n;
        bytes -= n;
    } while (bytes > 0);

    return ((int) pch - (int) buf);
}

__attribute__((unused)) static int write_np(int fd, void* buf, size_t bytes, uint32_t pos)
{
    char* pch = (char *) buf;

    do {
        ssize_t n = pwrite(fd, pch, bytes, pos);
        if (n == -1) {
            if (errno != EINTR) {
                break;
            }
            n = 0;
        }

        pch += n;
        bytes -= n;
        pos += n;
    } while (bytes > 0);

    return ((int) pch - (int) buf);
}

extern mfio_writer_t my_mfio_writer;

#endif

