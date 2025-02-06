
#import "FirstViewController.h"
#import "tableController.h"
#import <QuartzCore/CALayer.h>

#include "nav.h"
#include "mem.h"
#include "netdef.h"
#include "audioctl.h"
#include "media_addr.h"
#include "glview.h"
#include "media_buffer.h"
#include "lock.h"
#include "videoctl.h"
#include "writer.h"
#include "mfio_mp4.h"
#include <AudioToolbox/AudioSession.h>

#define mulview 1
#define snappic 0
#define save_mp4 1
static void* something = NULL;
static GLView* glv1 = NULL;
static GLView* glv2 = NULL;
static area_t* preview = NULL;
static lock_t lck = lock_initial;

typedef struct {
    lock_t lck;
    media_iden_t who;

    media_buffer audio;
    media_buffer video;
} mp4_stream_t;
static mp4_stream_t mp4 = {
    .lck = lock_initial
};

static void mp4_save_stream(usr_stream_t* msg)
{
    media_buffer* media = (media_buffer *) msg->media;
    if (!media_id_eqal(mp4.who, media_id_unkown) &&
        !media_id_eqal(mp4.who, media->iden))
    {
        return;
    }
    mp4.who = media->iden;

    if (video_type == media_type(*media->pptr_cc)) {
        mp4.video = *media;
    } else {
        mp4.audio = *media;
    }
}

static int notify(void* tcp, uint32_t notify, void* any)
{
    if (notify == notify_connected) {
        logmsg("net connected\n");
    } else if (notify == notify_disconnected) {
        logmsg("net broken\n");
    } else if (notify == notify_login) {
        logmsg("session login ok\n");
    } else if (notify == notify_user_stream) {
        usr_stream_t* msg = (usr_stream_t *) any;
        if (msg->media != NULL) {
            mp4_save_stream(msg);
        }
    }
    return 0;
}

intptr_t open_area(area_t* area)
{
    if (remote_stream(area->identy)) {
        area->egl = [glv1 egl_get];
        area->x = 0;
        area->y = 0;
        area->w = [glv1 bounds].size.width;
        area->h = [glv1 bounds].size.height;
        area->z = 0;
    } else if (mulview) {
        area->egl = [glv2 egl_get];
        area->x = 0;
        area->y = 0;
        area->w = [glv2 bounds].size.width;
        area->h = [glv2 bounds].size.height;
        area->z = 0;
    } else {
        area->egl = [glv1 egl_get];
        uint32_t width = [glv1 bounds].size.width;
        uint32_t height = [glv1 bounds].size.height;
        area->x = width / 2;
        area->y = 0;
        area->w = width / 2;
        area->h = height / 2;
        area->z = 1;
        preview = area;
    }
    return 0;
}

intptr_t close_area(area_t* area)
{
    do_close_area(area);
    if (area == preview) {
        lock(&lck);
        preview = NULL;
        unlock(&lck);
    }
    return 1;
}

static void stop_net_stream()
{
    void* p = __sync_lock_test_and_set(&something, NULL);
    if (p != NULL) {
        nav_close(p, 11);
    }
    logmsg("***memory %d\n", (int32_t) mem_bytes());
}

static int32_t begin_net_stream(const char* ip, uint32_t port, uint32_t chan, uint32_t uid)
{
    if (something != NULL) {
        return 0;
    }
    char info[1024];
    int bytes = tcpaddr_compile(info, chan, ip, port);

    nav_open_args args;
    args.uid = uid;
    args.info = (uint8_t *) &info[0];
    args.bytes = bytes;
    args.isp = 0;
    args.ms_wait = 10000;
    args.fptr.fptr = notify;
    args.fptr.ctx = NULL;
    args.dont_play = 0;
    args.dont_upload = 0;
    args.login_level = 2;

LABEL:
    something = nav_open(&args);
    if (something == NULL) {
        return -1;
    }
    return 0;
}

UIButton* video_button = nil;
UIButton* audio_button = nil;
@implementation FirstViewController
#if 1
- (void)loadView
{
    [super loadView];
    CGRect rect = self.view.bounds;
    rect.size.height /= 2;
    rect.origin.x = 5;
    rect.origin.y = 5;
    rect.size.width -= 10;
    rect.size.height -= 20;

    GLView* view = [[GLView alloc] initWithFrame:rect r:0.3 g:0.5 b:0.8 a:1.0];
    [self.view addSubview:view];
    glv1 = view;
    [glv1 layer].opacity = 1.0;

    CGRect rect2;
#if mulview
    rect2 = CGRectMake(rect.size.width / 2, rect.size.height / 2, rect.size.width / 2, rect.size.height / 2);
    view = [[GLView alloc] initWithFrame:rect2 r:0.0 g:0.0 b:0.0 a:0.4];
    [glv1 addSubview:view];
    glv2 = view;
#endif

    rect2 = CGRectMake(0, rect.origin.y + rect.size.height, rect.size.width + 10, 160);
    tableController* tac = [[[tableController alloc]
                            initWithStyle:UITableViewStyleGrouped
                            frame:rect2]
                            autorelease];
    [self.view addSubview:tac.view];
    [self addChildViewController:tac];

    rect2.origin.x = 10;
    rect2.origin.y += 150;
    rect2.size.width = 67;
    rect2.size.height = 30;

    UIButton* button = [[[UIButton alloc] initWithFrame:rect2] autorelease];
    button.backgroundColor = [UIColor lightGrayColor];

    CALayer* layer = button.layer;
    layer.borderWidth = 1.0;
    layer.borderColor = [[UIColor lightGrayColor] CGColor];

    [button setTitleColor:[UIColor blackColor] forState:UIControlStateNormal];
#if snappic
    [button setTitle:@"抓图" forState:UIControlStateNormal];
#elif save_mp4
    [button setTitle:@"录像" forState:UIControlStateNormal];
#else
#if mulview
    [button setTitle:@"隐藏" forState:UIControlStateNormal];
#else
    [button setTitle:@"切换" forState:UIControlStateNormal];
#endif
#endif
    [button addTarget:self action:@selector(onDown:) forControlEvents:UIControlEventTouchDown];
    [button addTarget:self action:@selector(onClick1:) forControlEvents:UIControlEventTouchUpInside];
    [button addTarget:self action:@selector(onUp:) forControlEvents:UIControlEventTouchUpOutside];
    [self.view addSubview:button];

//    rect2.origin.x += 10 + rect2.size.width;
//    button = [[[UIButton alloc] initWithFrame:rect2] autorelease];
//    button.backgroundColor = [UIColor lightGrayColor];
//
//    layer = button.layer;
//    layer.borderWidth = 1.0;
//    layer.borderColor = [[UIColor lightGrayColor] CGColor];
//
//    [button setTitleColor:[UIColor blackColor] forState:UIControlStateNormal];
//    [button setTitle:@"尺寸" forState:UIControlStateNormal];
//    [button addTarget:self action:@selector(onDown:) forControlEvents:UIControlEventTouchDown];
//    [button addTarget:self action:@selector(onClick2:) forControlEvents:UIControlEventTouchUpInside];
//    [button addTarget:self action:@selector(onUp:) forControlEvents:UIControlEventTouchUpOutside];
//    [self.view addSubview:button];

    rect2.origin.x += 10 + rect2.size.width;
    button = [[[UIButton alloc] initWithFrame:rect2] autorelease];
    button.backgroundColor = [UIColor lightGrayColor];

    layer = button.layer;
    layer.borderWidth = 1.0;
    layer.borderColor = [[UIColor lightGrayColor] CGColor];

    [button setTitleColor:[UIColor blackColor] forState:UIControlStateNormal];
    [button setTitle:@"视频" forState:UIControlStateNormal];
    [button addTarget:self action:@selector(onDown:) forControlEvents:UIControlEventTouchDown];
    [button addTarget:self action:@selector(onClick3:) forControlEvents:UIControlEventTouchUpInside];
    [button addTarget:self action:@selector(onUp:) forControlEvents:UIControlEventTouchUpOutside];
    video_button = button;
    [self.view addSubview:button];

    rect2.origin.x += 10 + rect2.size.width;
    button = [[[UIButton alloc] initWithFrame:rect2] autorelease];
    button.backgroundColor = [UIColor lightGrayColor];

    layer = button.layer;
    layer.borderWidth = 1.0;
    layer.borderColor = [[UIColor lightGrayColor] CGColor];

    [button setTitleColor:[UIColor blackColor] forState:UIControlStateNormal];
    [button setTitle:@"后置" forState:UIControlStateNormal];
    [button addTarget:self action:@selector(onDown:) forControlEvents:UIControlEventTouchDown];
    [button addTarget:self action:@selector(onClick5:) forControlEvents:UIControlEventTouchUpInside];
    [button addTarget:self action:@selector(onUp:) forControlEvents:UIControlEventTouchUpOutside];
    [self.view addSubview:button];

    rect2.origin.x += 10 + rect2.size.width;
    button = [[[UIButton alloc] initWithFrame:rect2] autorelease];
    button.backgroundColor = [UIColor lightGrayColor];

    layer = button.layer;
    layer.borderWidth = 1.0;
    layer.borderColor = [[UIColor lightGrayColor] CGColor];

    [button setTitleColor:[UIColor blackColor] forState:UIControlStateNormal];
    [button setTitle:@"音频" forState:UIControlStateNormal];
    [button addTarget:self action:@selector(onDown:) forControlEvents:UIControlEventTouchDown];
    [button addTarget:self action:@selector(onClick4:) forControlEvents:UIControlEventTouchUpInside];
    [button addTarget:self action:@selector(onUp:) forControlEvents:UIControlEventTouchUpOutside];
    audio_button = button;
    [self.view addSubview:button];
}

- (void)onDown:(UIButton *) button
{
    button.backgroundColor = [UIColor clearColor];
}

- (void)onUp:(UIButton *) button
{
    button.backgroundColor = [UIColor lightGrayColor];
}

static const char* mp4_file()
{
    static char name[1024];

    NSString* folder = nil;
    if (folder == nil) {
        folder = NSTemporaryDirectory();
    }
    const char* path = [folder UTF8String];

    intptr_t len = strlen(path);
    strcpy(name, path);

    if (name[len - 1] == '/') {
        name[len - 1] = 0;
    }
    strcat(name, "/z.mp4");
    return name;
}

static void* mp4_begin()
{
    static void* ptrs[3] = {NULL, NULL, NULL};
    if (mp4.audio.pptr_cc == NULL || mp4.video.pptr_cc == NULL) {
        return NULL;
    }

    audio_format* afmt = &aac_8k16b1;
    media_buffer audio;
    audio.pptr_cc = &afmt->cc;

    ptrs[0] = writer_open(&mp4_mfio_writer, mp4_file(), &audio, &mp4.video);
    if (ptrs[0] == NULL) {
        return NULL;
    }

    ptrs[1] = add_stream(&mp4_mfio_writer, ptrs[0], mp4.audio.iden, audio_type | video_type, 0);
    if (ptrs[1] == NULL) {
        writer_close(&mp4_mfio_writer, ptrs[0]);
        ptrs[0] = NULL;
        return NULL;
    }

    ptrs[2] = add_stream(&mp4_mfio_writer, ptrs[0], media_id_self, audio_type, 0);
    if (ptrs[2] == NULL) {
        delete_stream(ptrs[1]);
        writer_close(&mp4_mfio_writer, ptrs[0]);
        ptrs[0] = NULL;
        return NULL;
    }
    return ptrs;
}

static void mp4_end(void* ptr)
{
    void** ptrs = (void **) ptr;
    delete_stream(ptrs[2]);
    delete_stream(ptrs[1]);
    writer_close(&mp4_mfio_writer, ptrs[0]);
}

- (void)onClick1:(UIButton *) button
{
#if snappic
    UIImage* myImage = glv1.snap;
    if (myImage != NULL) {
        UIImageWriteToSavedPhotosAlbum(myImage, nil,nil, nil);
    }
    button.backgroundColor = [UIColor lightGrayColor];
#elif save_mp4
    static void* ptr = NULL;
    if (ptr == NULL) {
        ptr = mp4_begin();
    } else {
        mp4_end(ptr);
        ptr = NULL;
    }

    if (ptr == NULL) {
        button.backgroundColor = [UIColor lightGrayColor];
    }
#else
    static int hide = 0;
    button.backgroundColor = [UIColor lightGrayColor];
    if (mulview) {
        if (hide == 0) {
            [glv1 setHidden:YES];
            hide = 1;
        } else {
            [glv1 setHidden:NO];
            hide = 0;
        }
    } else {
        [self change_area_test];
    }
#endif
}

- (void)onClick2:(UIButton *) button
{
    button.backgroundColor = [UIColor lightGrayColor];
    CGRect rect = glv1.frame;

    static uintptr_t n = 0;
    if (n == 0) {
        rect.origin.x += 10;
        rect.size.width -= 20;
        n = 1;
    } else {
        rect.origin.x -= 10;
        rect.size.width += 20;
        n = 0;
    }
    glv1.frame = rect;
}

enum camera_position_t pos = front;
- (void)onClick3:(UIButton *) button
{
    video_param param;
    param.campos = pos;
    param.feedback = 1;
    param.width = 640;
    param.height = 480;
    param.fps = 10;
    param.gop = 10;
    param.kbps = 250;

    static int video_on = 0;
    if (something != NULL) {
        if (video_on == 0) {
            int n = nav_ioctl(something, video_capture_start, (void *) &param);
            if (n == 0) {
                video_on = 1;
            }
        } else {
            nav_ioctl(something, video_capture_stop, NULL);
            video_on = 0;
        }
    } else {
        video_on = 0;
    }

    if (video_on == 0) {
        button.backgroundColor = [UIColor lightGrayColor];
    }
}

- (void)onClick5:(UIButton *) button
{
    enum camera_position_t oldpos = pos;
    if (pos == front) {
        pos = back;
    } else {
        pos = front;
    }

    if (something != NULL) {
        int n = nav_ioctl(something, video_use_camera, (void *) &pos);
        if (n == -1) {
            pos = oldpos;
        }
    }

    if (pos == front) {
        button.backgroundColor = [UIColor lightGrayColor];
    }
}

- (void)onClick4:(UIButton *) button
{
    audio_param param;
    param.fmt = &silk_8k16b1;
    param.aec = ios;
    param.effect = type_none;
    static int audio_on = 0;

    if (something != NULL) {
        if (audio_on == 0) {
            int32_t n = nav_ioctl(something, audio_capture_start, (void *) &param);
            if (n == 0) {
                audio_on = 1;
            }
        } else {
            nav_ioctl(something, audio_capture_stop, NULL);
            audio_on = 0;
        }
    } else {
        audio_on = 0;
    }

    if (audio_on == 0) {
        button.backgroundColor = [UIColor lightGrayColor];
    }
}

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
    tableController* tac = (tableController *) self.childViewControllers[0];
    [tac listen:self action:@selector(onStart:)];
}

- (void)suspend:(intptr_t) yes
{
    [glv1 suspend:yes];
}

- (void)change_area_test
{
    static uintptr_t n = 0;
    lock(&lck);
    if (preview == NULL) {
        unlock(&lck);
        return;
    }

    area_t templ;
    if (n == 1) {
        templ.x = glv1->w / 2;
        templ.y = 0;
        templ.w = glv1->w / 2;
        templ.h = glv1->h / 2;
        templ.z = 1;
        n = 0;
    } else {
        templ.x = glv1->w / 3;
        templ.y = 0;
        templ.w = glv1->w * 2 / 3;
        templ.h = glv1->h * 2 / 3;
        templ.z = 1;
        n = 1;
    }

    area_invalid(preview, &templ);
    unlock(&lck);
}

#else
- (void) viewDidLoad
{
    [super viewDidLoad];
    UIView *redView = [[UIView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    redView.backgroundColor = [UIColor redColor];
    [self.view addSubview:redView];

    UIView *yellowView = [[UIView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    yellowView.backgroundColor = [UIColor yellowColor];
    [self.view addSubview:yellowView];

    UIButton *button = [UIButton buttonWithType:UIButtonTypeRoundedRect];
    [button setTitle:@"改变" forState:UIControlStateNormal];
    button.frame = CGRectMake(10, 10, 300, 40);
    [button addTarget:self action:@selector(changeUIView) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:button];

    UIButton *button1 = [UIButton buttonWithType:UIButtonTypeRoundedRect];
    [button1 setTitle:@"改变1" forState:UIControlStateNormal];
    button1.frame = CGRectMake(10, 60, 300, 40);
    [button1 addTarget:self action:@selector(changeUIView1) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:button1];
}

- (void)changeUIView
{
    [self.view exchangeSubviewAtIndex:1 withSubviewAtIndex:0];
}

- (void)changeUIView1{
    [UIView beginAnimations:@"animation" context:nil];
    [UIView setAnimationDuration:10.0f];
    [UIView setAnimationCurve:UIViewAnimationCurveEaseInOut];
    [UIView setAnimationTransition:UIViewAnimationTransitionCurlDown forView:self.view cache:YES];
    [UIView commitAnimations];
    [self.view exchangeSubviewAtIndex:1 withSubviewAtIndex:0];
}
#endif

- (void) onStart:(UISwitch *) sb
{
    tableController* tac = (tableController *) self.childViewControllers[0];
    if (sb.on) {
        int32_t n = begin_net_stream(tac->ip, tac->port, tac->chan, tac->uid);
        if (n == -1) {
            [tac notifyFailed];
        } else {
            tac->run = 1;
        }
    } else {
        stop_net_stream();
        [self onClick3:video_button];
        [self onClick4:audio_button];
        tac->run = 0;
    }
}

@end
