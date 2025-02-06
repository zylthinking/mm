
#import "FirstViewController.h"
#import "tableController.h"
#import <QuartzCore/CALayer.h>

#include "glview.h"
#include "lock.h"
#include "filefmt.h"
#include <AudioToolbox/AudioSession.h>

static void* something = NULL;
static GLView* glv = NULL;
static area_t* preview = NULL;
static lock_t lck = lock_initial;

intptr_t open_area(area_t* area)
{
    area->egl = [glv egl_get];
    area->x = 0;
    area->y = 0;
    area->w = [glv bounds].size.width;
    area->h = [glv bounds].size.height;
    area->z = 0;
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
    return 0;
}

static void stop_net_stream()
{
    void* p = __sync_lock_test_and_set(&something, NULL);
    if (p != NULL) {
        filefmt_close(p);
    }
}

static intptr_t notify(void* uptr, intptr_t type, void* any)
{
    fcontext_t* fctx = (fcontext_t *) any;
    logmsg("have: %llu, more = %llu, cursor = %llu, totall = %llu\n",
           fctx->have, fctx->more, fctx->cursor, fctx->duration);
    return 0;
}

static int32_t begin_net_stream(const char* ip, uint32_t port, uint32_t chan, uint32_t uid)
{
    if (something != NULL) {
        return 0;
    }
    callback_t callback;
    callback.uptr = NULL;
    callback.uptr_put = NULL;
    callback.notify = notify;
    //const char* pch = "http://jq.v.ismartv.tv/cdn/1/81/95e68bbdce46b5b8963b504bf73d1b/normal/slice/index.m3u8";
    const char* pch = "rtmp://live.hkstv.hk.lxdns.com/live/hks";
    //const char* pch = "http://ec.sinovision.net/video/ts/lv.m3u8";
    //const char* pch = "http://cdn.live.360.cn/huikan_huajiao/vod-bucket/d41714055b847f766e4f6eafefc25758_d41714055b847f766e4f6eafefc25758_110.m3u8";

    file_mode_t mode;
    mode.seekto = 0;
    mode.sync = 0;
    mode.latch[0] = 2000;
    mode.latch[1] = 4000;
    something = filefmt_open(pch, ftype_auto, &callback, NULL, &mode);
    if (something == NULL) {
        return -1;
    }
    return 0;
}

UIButton* video_button = nil;
UIButton* audio_button = nil;
@implementation FirstViewController

- (void)loadView
{
    [super loadView];
    CGRect rect = self.view.bounds;
    rect.size.height /= 2;
    rect.origin.x = 5;
    rect.origin.y = 5;
    rect.size.width -= 10;
    rect.size.height -= 20;

    GLView* view = [[GLView alloc] initWithFrame:rect r:0.1 g:1.0 b:0.8];
    [self.view addSubview:view];
    glv = view;

    CGRect rect2 = CGRectMake(0, rect.origin.y + rect.size.height, rect.size.width + 10, 160);
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
    [button setTitle:@"切换" forState:UIControlStateNormal];
    [button addTarget:self action:@selector(onDown:) forControlEvents:UIControlEventTouchDown];
    [button addTarget:self action:@selector(onClick1:) forControlEvents:UIControlEventTouchUpInside];
    [button addTarget:self action:@selector(onUp:) forControlEvents:UIControlEventTouchUpOutside];
    [self.view addSubview:button];

    rect2.origin.x += 10 + rect2.size.width;
    button = [[[UIButton alloc] initWithFrame:rect2] autorelease];
    button.backgroundColor = [UIColor lightGrayColor];

    layer = button.layer;
    layer.borderWidth = 1.0;
    layer.borderColor = [[UIColor lightGrayColor] CGColor];

    [button setTitleColor:[UIColor blackColor] forState:UIControlStateNormal];
    [button setTitle:@"尺寸" forState:UIControlStateNormal];
    [button addTarget:self action:@selector(onDown:) forControlEvents:UIControlEventTouchDown];
    [button addTarget:self action:@selector(onClick2:) forControlEvents:UIControlEventTouchUpInside];
    [button addTarget:self action:@selector(onUp:) forControlEvents:UIControlEventTouchUpOutside];
    [self.view addSubview:button];

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

- (void)onClick1:(UIButton *) button
{
    button.backgroundColor = [UIColor lightGrayColor];
    [self change_area_test];
}

- (void)onClick2:(UIButton *) button
{
    button.backgroundColor = [UIColor lightGrayColor];
    CGRect rect = glv.frame;

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

    glv.frame = rect;
}

- (void)onClick3:(UIButton *) button
{
    static int video_on = 0;
    if (something != NULL) {
        if (video_on == 0) {
            int n = -1;
            if (n == 0) {
                video_on = 1;
            }
        } else {
            video_on = 0;
        }
    } else {
        video_on = 0;
    }

    if (video_on == 0) {
        button.backgroundColor = [UIColor lightGrayColor];
    }
}

- (void)onClick4:(UIButton *) button
{
    static int audio_on = 0;

    if (something != NULL) {
        if (audio_on == 0) {
            int32_t n = -1;
            if (n == 0) {
                audio_on = 1;
            }
        } else {
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
    [glv suspend:yes];
}

- (void)change_area_test
{
    area_t templ;
    static uintptr_t n = 0;
    lock(&lck);
    if (preview == NULL) {
        unlock(&lck);
        return;
    }

    if (n == 1) {
        templ.x = glv->w / 2;
        templ.y = 0;
        templ.w = glv->w / 2;
        templ.h = glv->h / 2;
        templ.z = 1;
        n = 0;
    } else {
        templ.x = glv->w / 3;
        templ.y = 0;
        templ.w = glv->w * 2 / 3;
        templ.h = glv->h * 2 / 3;
        templ.z = 1;
        n = 1;
    }
    area_invalid(preview->egl, &templ);
    unlock(&lck);
}

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
