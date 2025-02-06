
#include "video.h"
#include "fmt.h"
#include "media_buffer.h"
#include "videoctl.h"
#import <AVFoundation/AVFoundation.h>

typedef struct {
    NSString* unique;
    AVCaptureDeviceInput* input;
    uintptr_t angle[2];
    uint8_t mirror;
} camera_t;

@interface video_session : NSObject<AVCaptureVideoDataOutputSampleBufferDelegate> {
@public
    AVCaptureSession* session;
    AVCaptureConnection* conn;
    camera_t camera[2];
    camera_t* current;

    media_iden_t uid;
    uint32_t seq;
    video_push_t push;
    void* any;
    uint32_t refs;
    uint32_t angle;

    video_format* format;
    uintptr_t reftype;
}

- (void) addedOrLost:(enum camera_position_t) campos;

@end

