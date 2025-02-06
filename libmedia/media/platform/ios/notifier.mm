
#import "v.h"
#include "notifier.h"
#include "lock.h"
#include "audioctl.h"
#include "ios.h"
#include "enter.h"
#include "my_errno.h"
#import <Foundation/NSNotification.h>
#import <UIKit/UIApplication.h>

@interface notifier : NSObject {
@public
    video_session* vs;
}

- (notifier *) init:(video_session *) vs;
- (void) onNotify:(NSNotification *) notification;
@end

static notifier* notifier_create()
{
    notifier* noti = [[notifier alloc] init:NULL];
    if (noti == nil) {
        logmsg("killed because of memory\n");
        exit(-1);
    }

    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [center addObserver: noti
               selector: @selector(onNotify:)
                   name: UIApplicationDidBecomeActiveNotification
                 object: nil];

    [center addObserver: noti
               selector: @selector(onNotify:)
                   name: UIApplicationDidEnterBackgroundNotification
                 object: nil];

    return noti;
}

static notifier* noti = notifier_create();
extern entry_point h264_decoder_point;

@implementation notifier

- (notifier *) init:(video_session *) session
{
    if (self = [super init]) {
        self->vs = session;
    }
    return self;
}

- (void) onNotify:(NSNotification *) notification
{
    if (notification.name == UIApplicationDidBecomeActiveNotification) {
        lock_audio_dev();
        enter_block(&h264_decoder_point, 0);
        return;
    }

    if (notification.name == UIApplicationDidEnterBackgroundNotification) {
        enter_block(&h264_decoder_point, 1);
        if (vs != NULL) {

        }
    }

    if (notification.name == AVCaptureDeviceWasConnectedNotification ||
        notification.name == AVCaptureDeviceWasDisconnectedNotification)
    {
        my_assert(vs != NULL);
        [vs addedOrLost:unkown];
        return;
    }
}

@end

void notifer_video_register(void* vs)
{
    my_assert(noti->vs == NULL);
    noti->vs = (video_session *) vs;

    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [center addObserver: noti
               selector: @selector(onNotify:)
                   name: AVCaptureSessionInterruptionEndedNotification
                 object: noti->vs->session];

    [center addObserver: noti
               selector: @selector(onNotify:)
                   name: AVCaptureDeviceWasConnectedNotification
                 object: noti->vs->session];

    [center addObserver: noti
               selector: @selector(onNotify:)
                   name: AVCaptureDeviceWasDisconnectedNotification
                 object: noti->vs->session];

    [center addObserver: noti
               selector: @selector(onNotify:)
                   name: AVCaptureSessionDidStopRunningNotification
                 object: noti->vs->session];
}

void notifer_video_unregister(void* v)
{
    my_assert(v != NULL);
    video_session* vs = (video_session *) v;
    my_assert(noti->vs == vs);

    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [center removeObserver: noti
                      name: AVCaptureSessionInterruptionEndedNotification
                    object: noti->vs->session];

    [center removeObserver: noti
                      name: AVCaptureDeviceWasConnectedNotification
                    object: noti->vs->session];

    [center removeObserver: noti
                      name: AVCaptureDeviceWasDisconnectedNotification
                    object: noti->vs->session];

    [center removeObserver: noti
                      name: AVCaptureSessionDidStopRunningNotification
                    object: noti->vs->session];

    noti->vs = NULL;
}

double ios_version()
{
    return [[UIDevice currentDevice].systemVersion doubleValue];
}
