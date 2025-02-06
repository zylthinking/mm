
#include "v.h"
#include "mbuf.h"
#include "media_buffer.h"
#include "pts.h"
#include "notifier.h"
#include "lock.h"
#include "fmt_in.h"
#include "ios.h"

@implementation video_session
- (void) detachLost:(NSArray *) devices
{
    uint32_t lost[2] = {1, 1};
    for (AVCaptureDevice* device in devices) {
        if ((NSNull *) device == [NSNull null]) {
            continue;
        }

        assert(device != nil && device != NULL);
        NSString* uniq = device.uniqueID;
        assert(uniq != nil && uniq != NULL);

        if ([device position] == AVCaptureDevicePositionBack) {
            if (uniq == camera[0].unique) {
                lost[0] = 0;
            }
        } else {
            if (uniq == camera[1].unique) {
                lost[1] = 0;
            }
        }
    }

    if (lost[0] == 1) {
        if (camera[0].input != NULL) {
            if (current == &camera[0]) {
                [session removeInput:camera[0].input];
                current = NULL;
            }
            [camera[0].input release];
            camera[0].input = NULL;
            camera[0].unique = NULL;
        }
    }

    if (lost[1] == 1) {
        if (camera[1].input != NULL) {
            if (current == &camera[1]) {
                [session removeInput:camera[1].input];
                current = NULL;
            }
            [camera[1].input release];
            camera[1].input = NULL;
            camera[1].unique = NULL;
        }
    }
}

- (void) tryUse:(AVCaptureDevice *) dev camera:(camera_t *) cam uniq:(NSString *) uniq
{
    AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:dev error:nil];
    if (input != nil) {
        [input retain];
        cam->input = input;
        cam->unique = uniq;
    }
}

- (void) addedOrLost:(enum camera_position_t) campos
{
    NSArray* devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
    [self detachLost:devices];

    camera_t* oldcamera = current;
    if (oldcamera != NULL) {
        if (campos == unkown) {
            if (oldcamera == &camera[0]) {
                campos = back;
            } else {
                campos = front;
            }
        }

        if (oldcamera->input == NULL) {
            oldcamera = NULL;
        } else if ((oldcamera == &camera[0] && campos == front) ||
                   (oldcamera == &camera[1] && campos == back))
        {
            [session removeInput:current->input];
            current = NULL;
            oldcamera = NULL;
        }
    }

    for (AVCaptureDevice* device in devices) {
        if ((NSNull *) device == [NSNull null]) {
            continue;
        }

        my_assert(device != nil && device != NULL);
        NSString* uniq = device.uniqueID;
        my_assert(uniq != nil && uniq != NULL);
        AVCaptureDevicePosition pos = device.position;

        if (camera[0].input == NULL && pos == AVCaptureDevicePositionBack) {
            [self tryUse:device camera:&camera[0] uniq:uniq];
        } else if (camera[1].input == NULL && pos == AVCaptureDevicePositionFront) {
            [self tryUse:device camera:&camera[1] uniq:uniq];
        }

        if (camera[0].input != NULL && camera[1].input != NULL) {
            break;
        }
    }

    if (campos == unkown) {
        return;
    }

    if (campos == back && camera[0].input != NULL) {
        current = &camera[0];
    }

    if (current == NULL && camera[1].input != NULL) {
        current = &camera[1];
    }

    if (current == oldcamera) {
        return;
    }
    [session addInput:current->input];
}

- (int) useCamera:(enum camera_position_t) campos
{
    if (campos == back) {
        if (current == &camera[0]) {
            return 0;
        }

        if (camera[1].input == NULL) {
            return -1;
        }
    }

    if (campos == front) {
        if (current == &camera[1]) {
            return 0;
        }

        if (camera[0].input == NULL) {
            return -1;
        }
    }

    if (campos == back) {
        if (current != NULL) {
            [session removeInput:current->input];
        }
        current = &camera[0];
    } else {
        if (current != NULL) {
            [session removeInput:current->input];
        }
        current = &camera[1];
    }
    [session addInput:current->input];
    return 0;
}

- (BOOL) videoSessionInit:(enum camera_position_t) campos
{
    [self addedOrLost:campos];
    if (camera[0].input == NULL && camera[1].input == NULL) {
        return FALSE;
    }

    [session beginConfiguration];
    AVCaptureVideoDataOutput* videoOut = [[AVCaptureVideoDataOutput alloc] init];
    if (videoOut == nil) {
        [session commitConfiguration];
        return FALSE;
    }
    videoOut.alwaysDiscardsLateVideoFrames = YES;

    dispatch_queue_t q = dispatch_queue_create("video", DISPATCH_QUEUE_SERIAL);
    if (q == NULL) {
        [videoOut release];
        [session commitConfiguration];
        return FALSE;
    }

    [videoOut setSampleBufferDelegate:self queue:q];
    dispatch_release(q);

    if ([session canAddOutput:videoOut]) {
        [session addOutput:videoOut];
    } else {
        [videoOut release];
        [session commitConfiguration];
        return FALSE;
    }

    conn = [videoOut connectionWithMediaType:AVMediaTypeVideo];
    if (conn == nil) {
        [session removeOutput:videoOut];
        [videoOut release];
        [session commitConfiguration];
        return FALSE;
    }
    [conn retain];

    [videoOut release];
    [session commitConfiguration];
    return TRUE;
}

static NSString* preset_from_format(camera_t* cam, video_format* fmt, uint32_t* angle)
{
    NSString* preset = NULL;
    if (fmt->pixel->size->width == 480 && fmt->pixel->size->height == 640) {
        preset = AVCaptureSessionPreset640x480;
        *angle = 0;
    } else if (fmt->pixel->size->width == 640 && fmt->pixel->size->height == 480) {
        preset = AVCaptureSessionPreset640x480;
        *angle = 1;
    } else if (fmt->pixel->size->width == 288 && fmt->pixel->size->height == 352) {
        preset = AVCaptureSessionPreset352x288;
        *angle = 0;
    } else if (fmt->pixel->size->width == 352 && fmt->pixel->size->height == 288) {
        preset = AVCaptureSessionPreset352x288;
        *angle = 1;
    } else if (fmt->pixel->size->width == 720 && fmt->pixel->size->height == 1280) {
        preset = AVCaptureSessionPreset1280x720;
        *angle = 0;
    } else if (fmt->pixel->size->width == 1280 && fmt->pixel->size->height == 720) {
        preset = AVCaptureSessionPreset1280x720;
        *angle = 1;
    } else if (fmt->pixel->size->width == 1080 && fmt->pixel->size->height == 1920) {
        preset = AVCaptureSessionPreset1920x1080;
        *angle = 0;
    } else if (fmt->pixel->size->width == 1920 && fmt->pixel->size->height == 1080) {
        preset = AVCaptureSessionPreset1920x1080;
        *angle = 1;
    } else if (fmt->pixel->size->width == 540 && fmt->pixel->size->height == 960) {
        preset = AVCaptureSessionPresetiFrame960x540;
        *angle = 0;
    } else if (fmt->pixel->size->width == 960 && fmt->pixel->size->height == 540) {
        preset = AVCaptureSessionPresetiFrame960x540;
        *angle = 1;
    } else {
        mark("resolution does not support");
    }
    return preset;
}

- (intptr_t) configure:(video_format *)fmt
{
    if (fmt->pixel->csp != csp_nv12ref && fmt->pixel->csp != csp_nv12) {
        errno = EINVAL;
        return -1;
    }

    NSString* preset = preset_from_format(current, fmt, &angle);
    if (preset == NULL) {
        errno = EINVAL;
        return -1;
    }
    reftype = reference_format(fmt);

    NSString* key = (NSString *) kCVPixelBufferPixelFormatTypeKey;
    NSNumber* value = [NSNumber numberWithUnsignedInt: kCVPixelFormatType_420YpCbCr8BiPlanarFullRange];
    if (value == nil) {
        return -1;
    }

    NSDictionary* settings = [NSDictionary dictionaryWithObject:value forKey:key];
    if (settings == nil) {
        return -1;
    }

    [session beginConfiguration];
    AVCaptureVideoDataOutput* output = (AVCaptureVideoDataOutput *) conn.output;
    output.videoSettings = settings;

    if (angle == 0) {
        conn.videoOrientation = AVCaptureVideoOrientationPortrait;
        camera[0].angle[0] = 1;
        camera[0].angle[1] = 0;
        camera[1].angle[0] = 0;
        camera[1].angle[1] = 1;
    } else if (0) {
        camera[0].angle[0] = 2;
        camera[0].angle[1] = 0;
        camera[1].angle[0] = 0;
        camera[1].angle[1] = 0;
        conn.videoOrientation = AVCaptureVideoOrientationLandscapeLeft;
        angle = 0;
    } else {
        camera[0].angle[0] = 0;
        camera[0].angle[1] = 0;
        camera[1].angle[0] = 0;
        camera[1].angle[1] = 2;
        conn.videoOrientation = AVCaptureVideoOrientationLandscapeRight;
        angle = 0;
    }

    // [current->input device].activeVideoMinFrameDuration = CMTimeMake(1, (int32_t) fmt->caparam->fps);
    // [current->input device].activeVideoMaxFrameDuration = CMTimeMake(1, (int32_t) fmt->caparam->fps);
    if (conn.supportsVideoMinFrameDuration) {
        conn.videoMinFrameDuration = CMTimeMake(1, (int32_t) fmt->caparam->fps);
    }

    if (conn.supportsVideoMaxFrameDuration) {
        conn.videoMaxFrameDuration = CMTimeMake(1, (int32_t) fmt->caparam->fps);
    }

    session.sessionPreset = preset;
    [session commitConfiguration];

    format = fmt;
    return 0;
}

- (video_session *) init:(video_push_t) func arg:(void *) ctx pos:(enum camera_position_t) campos
{
    self = [super init];
    if (self == nil) {
        return nil;
    }

    camera[0].unique = NULL;
    camera[0].input = NULL;
    camera[1].unique = NULL;
    camera[1].input = NULL;
    current = NULL;
    session = [[AVCaptureSession alloc] init];
    conn = nil;
    format = NULL;
    if (session == nil) {
        [self dealloc];
        self = nil;
    }

    if (![self videoSessionInit:campos]) {
        [self dealloc];
        self = nil;
    } else {
        uid = media_id_self;
        seq = 0;
        push = func;
        any = ctx;
    }
    return self;
}

- (int32_t) start
{
    if (!session.isRunning) {
        [session startRunning];
    }
    return 0;
}

- (void) stop
{
    if (session.isRunning) {
        [session stopRunning];
    }
}

- (struct my_buffer *) video_buffer_get1:(CVPixelBufferRef) pixelbuf
{
    size_t bytes = CVPixelBufferGetDataSize(pixelbuf);
    struct my_buffer* mbuf = mbuf_alloc_2((uint32_t) (sizeof(media_buffer) + bytes));
    if (mbuf == NULL) {
        return NULL;
    }

    CVReturn ret = CVPixelBufferLockBaseAddress(pixelbuf, kCVPixelBufferLock_ReadOnly);
    if (kCVReturnSuccess != ret) {
        mbuf->mop->free(mbuf);
        return NULL;
    }

    mbuf->ptr[1] = mbuf->ptr[0] + sizeof(media_buffer);
    mbuf->length -= sizeof(media_buffer);
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    memset(media->vp, 0, sizeof(media->vp));
    media->frametype = 0;
    media->angle = (angle + (uint32_t) current->angle[0]) | current->mirror;
    media->fragment[0] = 0;
    media->fragment[1] = 1;
    media->iden = uid;
    media->seq = (++seq);
    media->pptr_cc = &format->cc;

    bytes = 0;
    size_t nr_panels = CVPixelBufferGetPlaneCount(pixelbuf);

    for (size_t i = 0; i < nr_panels; ++i) {
        media->vp[i].stride = (uint32_t) CVPixelBufferGetBytesPerRowOfPlane(pixelbuf, i);
        media->vp[i].height = (uint32_t) CVPixelBufferGetHeightOfPlane(pixelbuf, i);
        media->vp[i].ptr = mbuf->ptr[1] + bytes;

        uintptr_t length = media->vp[i].stride * media->vp[i].height;
        bytes += length;

        void* addr = CVPixelBufferGetBaseAddressOfPlane(pixelbuf, i);
        memcpy(media->vp[i].ptr, addr, length);
    }

    ret = CVPixelBufferUnlockBaseAddress(pixelbuf, kCVPixelBufferLock_ReadOnly);
    my_assert(kCVReturnSuccess == ret);
    my_assert(mbuf->length >= bytes);
    mbuf->length = bytes;
    return mbuf;
}

- (struct my_buffer *) video_buffer_get2:(CVPixelBufferRef) pixelbuf
{
    struct my_buffer* mbuf = buffer_attach(pixelbuf, 1);
    if (mbuf != NULL) {
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        media->frametype = 0;
        media->angle = (angle + (uint32_t) (current->angle[0])) | current->mirror;
        media->fragment[0] = 0;
        media->fragment[1] = 1;
        media->iden = uid;
        media->seq = (++seq);
        media->pptr_cc = &format->cc;
    }
    return mbuf;
}

- (void) captureOutput:(AVCaptureOutput *)captureOutput didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                                                               fromConnection:(AVCaptureConnection *)connection
{
    (void) captureOutput;
    (void) connection;

    CVPixelBufferRef pixelbuf = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (pixelbuf == NULL) {
        return;
    }

    struct my_buffer* mbuf = NULL;
    if (reftype == 0) {
        mbuf = [self video_buffer_get1:pixelbuf];
    } else {
        mbuf = [self video_buffer_get2:pixelbuf];
    }

    if (mbuf != NULL) {
        fill_pts(NULL, mbuf);

        video_push_t video_push = NULL;
        uint32_t n = __sync_add_and_fetch(&refs, 1);
        if (n == 2) {
            video_push = push;
        } else {
            my_assert(n == 1);
        }

        if (video_push != NULL) {
            video_push(any, mbuf);
        } else {
            mbuf->mop->free(mbuf);
        }

        n = __sync_sub_and_fetch(&refs, 1);
        if (n == 0 && video_push != NULL) {
            video_push(any, NULL);
        }
    }
}

- (void) dealloc
{
    if (conn != nil) {
        [conn release];
    }

    if (camera[0].input != NULL) {
        [camera[0].input release];
    }

    if (camera[1].input != NULL) {
        [camera[1].input release];
    }

    if (session != nil) {
        [session release];
    }
    [super dealloc];
}

@end

static video_session* vs = NULL;
static lock_t lck = lock_initial;

void* video_create(enum camera_position_t campos, video_push_t func, void* ctx, video_format* fmt)
{
    errno = EEXIST;
    lock(&lck);
    if (vs != NULL) {
        unlock(&lck);
        return NULL;
    }

    errno = ENOMEM;
    video_session* ptr = [[video_session alloc] init:func arg:ctx pos:campos];
    if (ptr == nil) {
        unlock(&lck);
        return NULL;
    }
    vs = ptr;
    vs->camera[0].mirror = 0x00;
    vs->camera[1].mirror = 0x80;

    errno = EFAILED;
    intptr_t n = [vs configure:fmt];
    if (n != -1) {
        notifer_video_register(vs);
        vs->refs = 1;
        [vs start];
    } else {
        [vs release];
        vs = NULL;
    }
    unlock(&lck);

    return vs;
}

void video_destroy(void* ptr)
{
    my_assert(ptr != NULL);
    lock(&lck);
    if (vs != (video_session *) ptr) {
        unlock(&lck);
        return;
    }

    notifer_video_unregister(ptr);
    uint32_t refs = __sync_sub_and_fetch(&vs->refs, 1);

    video_push_t video_push = vs->push;
    void* any = vs->any;

    [vs stop];
    [vs release];
    vs = NULL;
    unlock(&lck);

    if (refs == 0) {
        video_push(any, NULL);
    }
}

int use_camera(enum camera_position_t campos)
{
    lock(&lck);
    if (vs == NULL) {
        unlock(&lck);
        return 0;
    }
    vs->camera[1].angle[0] = vs->camera[1].angle[1];
    int n = [vs useCamera:campos];
    unlock(&lck);
    return n;
}
