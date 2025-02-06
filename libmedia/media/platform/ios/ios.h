
#ifndef ios_h
#define ios_h

#include "mbuf.h"
#include "media_buffer.h"
#include "mydef.h"
#include <CoreVideo/CVPixelBuffer.h>
#include <Coremedia/CMSampleBuffer.h>

#define check_status(s, exp) \
do { \
    int res = (int) (s); \
    if(res != noErr) { \
        const char* p = (const char*) &res;\
        logmsg("%s: ios error code = %d(%c%c%c%c) at line %d\n", \
               __FUNCTION__, res, p[3], p[2], p[1], p[0], __LINE__); \
        exp; \
    } \
} while (0)

capi double ios_version();

typedef struct {
    CVPixelBufferRef pixelbuf;
    struct mbuf_operations* mop;
    intptr_t ref;
    intptr_t rdonly;
} video_buffer_t;

static __attribute__((unused)) struct my_buffer* mbuf_clone_hack(struct my_buffer* mbuf)
{
    video_buffer_t* buffer = (video_buffer_t *) (mbuf->ptr[0] + sizeof(media_buffer));
    struct my_buffer* mbuf2 = buffer->mop->clone(mbuf);
    if (mbuf2 != NULL) {
        __sync_add_and_fetch(&buffer->ref, 1);
    }
    return mbuf2;
}

static __attribute__((unused)) void mbuf_free_hack(struct my_buffer* mbuf)
{
    video_buffer_t* buffer = (video_buffer_t *) (mbuf->ptr[0] + sizeof(media_buffer));
    intptr_t n = __sync_sub_and_fetch(&buffer->ref, 1);
    if (n == 0) {
        CVPixelBufferUnlockBaseAddress(buffer->pixelbuf, buffer->rdonly);
        CVPixelBufferRelease(buffer->pixelbuf);
    }
    mbuf->mop = buffer->mop;
    mbuf->mop->free(mbuf);
}

static __attribute__((unused)) struct my_buffer* buffer_attach(CVPixelBufferRef pixelbuf, intptr_t rdonly)
{
    rdonly = rdonly ? kCVPixelBufferLock_ReadOnly : 0;
    CVReturn ret = CVPixelBufferLockBaseAddress(pixelbuf, rdonly);
    if (kCVReturnSuccess != ret) {
        return NULL;
    }

    struct my_buffer* mbuf = mbuf_alloc_2((uint32_t) (sizeof(media_buffer) + sizeof(video_buffer_t)));
    if (mbuf == NULL) {
        CVPixelBufferUnlockBaseAddress(pixelbuf, rdonly);
        return NULL;
    }

    video_buffer_t* buffer = (video_buffer_t *) (mbuf->ptr[0] + sizeof(media_buffer));
    buffer->ref = 1;
    buffer->rdonly = rdonly;
    buffer->mop = mbuf->mop;
    buffer->pixelbuf = pixelbuf;
    CVPixelBufferRetain(pixelbuf);

    mbuf->ptr[1] = (char *) buffer;
    mbuf->length = 0;

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    memset(media->vp, 0, sizeof(media->vp));

    if (!CVPixelBufferIsPlanar(pixelbuf)) {
        media->vp[0].stride = (uint32_t) CVPixelBufferGetBytesPerRow(pixelbuf);
        media->vp[0].height = (uint32_t) CVPixelBufferGetHeight(pixelbuf);
        media->vp[0].ptr = (char *) CVPixelBufferGetBaseAddress(pixelbuf);
    } else {
        size_t nr_panels = CVPixelBufferGetPlaneCount(pixelbuf);
        for (size_t i = 0; i < nr_panels; ++i) {
            media->vp[i].stride = (uint32_t) CVPixelBufferGetBytesPerRowOfPlane(pixelbuf, i);
            media->vp[i].height = (uint32_t) CVPixelBufferGetHeightOfPlane(pixelbuf, i);
            media->vp[i].ptr = (char *) CVPixelBufferGetBaseAddressOfPlane(pixelbuf, i);
        }
    }

    static struct mbuf_operations mop = {
        .clone = mbuf_clone_hack,
        .free = mbuf_free_hack
    };
    mbuf->mop = &mop;
    return mbuf;
}

typedef struct {
    CMBlockBufferRef block;
    struct mbuf_operations* mop;
    intptr_t ref;
} video_buffer2_t;

static __attribute__((unused)) struct my_buffer* mbuf_clone_hack2(struct my_buffer* mbuf)
{
    video_buffer2_t* buffer = (video_buffer2_t *) (mbuf->ptr[0] + sizeof(media_buffer));
    struct my_buffer* mbuf2 = buffer->mop->clone(mbuf);
    if (mbuf2 != NULL) {
        __sync_add_and_fetch(&buffer->ref, 1);
    }
    return mbuf2;
}

static __attribute__((unused)) void mbuf_free_hack2(struct my_buffer* mbuf)
{
    video_buffer2_t* buffer = (video_buffer2_t *) (mbuf->ptr[0] + sizeof(media_buffer));
    intptr_t n = __sync_sub_and_fetch(&buffer->ref, 1);
    if (n == 0) {
        if (buffer->block != NULL) {
            CFRelease(buffer->block);
        }
    }
    mbuf->mop = buffer->mop;
    mbuf->mop->free(mbuf);
}

static __attribute__((unused)) struct my_buffer* block_attach(CMBlockBufferRef block)
{
    size_t bytes, total;
    char* pch = NULL;
    OSStatus status = CMBlockBufferGetDataPointer(block, 0, &bytes, &total, &pch);
    if (status != kCMBlockBufferNoErr) {
        return NULL;
    }

    if (bytes == total) {
        bytes = 0;
    }

    struct my_buffer* mbuf = mbuf_alloc_2((uint32_t) (sizeof(media_buffer) + sizeof(video_buffer2_t) + bytes));
    if (mbuf == NULL) {
        return NULL;
    }

    video_buffer2_t* buffer = (video_buffer2_t *) (mbuf->ptr[0] + sizeof(media_buffer));
    buffer->ref = 1;
    buffer->mop = mbuf->mop;
    mbuf->ptr[1] = (char *) buffer;
    mbuf->length = 0;

    if (bytes == 0) {
        buffer->block = block;
        CFRetain(block);
    } else {
        buffer->block = NULL;
        pch = mbuf->ptr[1] + sizeof(video_buffer2_t);
        status = CMBlockBufferAccessDataBytes(block, 0, total, pch, &pch);
        if (status != kCMBlockBufferNoErr) {
            mbuf->mop->free(mbuf);
            return NULL;
        }
        my_assert(pch == mbuf->ptr[1] + sizeof(video_buffer2_t));
    }

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    memset(media->vp, 0, sizeof(media->vp));
    media->vp[0].stride = (uint32_t) total;
    media->vp[0].height = 1;
    media->vp[0].ptr = pch;

    static struct mbuf_operations mop = {
        .clone = mbuf_clone_hack2,
        .free = mbuf_free_hack2
    };
    mbuf->mop = &mop;
    return mbuf;
}

#endif
