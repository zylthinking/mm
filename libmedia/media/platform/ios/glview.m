
#import "glview.h"
#include "win.h"
#include "gl.h"
#include <OpenGLES/EAGL.h>
#include <QuartzCore/QuartzCore.h>

static eglop_t eglop;

static void buffer_release(void* info, const void* data, size_t size)
{
    (void) info;
    (void) size;
    free((void *) data);
}

static UIImage* print_view(GLView* view)
{
    CGSize viewSize = view.frame.size;
    NSInteger bytes = viewSize.width * viewSize.height * 4;

    GLubyte *buffer2 = (GLubyte *) malloc(bytes * 2);
    if (buffer2 == NULL) {
        return NULL;
    }

    GLubyte *buffer1 = (GLubyte *) buffer2 + bytes;
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, buffer2, bytes, buffer_release);
    if (provider == NULL || provider == nil) {
        free(buffer2);
        return NULL;
    }

    glReadPixels(0, 0, viewSize.width, viewSize.height, GL_RGBA, GL_UNSIGNED_BYTE, buffer1);
    for(int y = 0; y < viewSize.height; y++) {
        for(int x = 0; x < viewSize.width* 4; x++) {
            buffer2[(int)((viewSize.height-1 - y) * viewSize.width * 4 + x)] = buffer1[(int)(y * 4 * viewSize.width + x)];
        }
    }

    int bitsPerComponent = 8;
    int bitsPerPixel = 32;
    int bytesPerRow = 4 * viewSize.width;
    CGColorSpaceRef colorSpaceRef = CGColorSpaceCreateDeviceRGB();
    CGImageRef imageRef = CGImageCreate(viewSize.width, viewSize.height,
                                        bitsPerComponent, bitsPerPixel, bytesPerRow, colorSpaceRef,
                                        kCGBitmapByteOrderDefault, provider, NULL, NO, kCGRenderingIntentDefault);
    CFRelease(colorSpaceRef);
    CFRelease(provider);
    if (imageRef == NULL || imageRef == nil) {
        return NULL;
    }

    UIImage* myImage = [UIImage imageWithCGImage:imageRef];
    CFRelease(imageRef);
    if (myImage == nil) {
        return NULL;
    }
    return myImage;
}

@implementation GLView
+ (Class) layerClass {
    return [CAEAGLLayer class];
}

- (void) suspend:(intptr_t) yes
{
    my_handle* handle = (my_handle *) egl;
    window_t* wind = (window_t *) handle_get(handle);
    gl_allowed(wind, !yes);
    handle_put(handle);
}

- (id) initWithFrame:(CGRect)frame r:(float)r g:(float)g b:(float)b a:(float)a
{
    if (self = [super initWithFrame:frame]) {
        self.opaque = (a == 1.0);

        x = y = 0;
        w = (uintptr_t) CGRectGetWidth(frame);
        h = (uintptr_t) CGRectGetHeight(frame);
        egl = (void *) window_create(r, g, b, a, w, h, (void *) self, &eglop);
        if (egl == NULL) {
            return nil;
        }
    }

    return self;
}

- (void *) egl_get
{
    my_handle* handle = (my_handle *) egl;
    handle_clone(handle);
    return egl;
}

- (void) super_dealloc
{
    [super dealloc];
}

- (void) dealloc
{
    [self suspend:1];
    my_handle* handle = (my_handle *) egl;
    handle_dettach(handle);
}

- (UIImage *) snap
{
    UIImage* image = NULL;
    my_handle* handle = (my_handle *) egl;
    window_t* win = (window_t *) handle_get(handle);
    if (0 == gl_enter(win)) {
        image = print_view(self);
        gl_leave(win, 1);
    }
    handle_put(handle);
    return image;
}

@end

static void egl_frameBuffer_free(eglenv_t* env)
{
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glDeleteBuffers(2, &env->globjs.rbo[0]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &env->globjs.fbo);

    EAGLContext* eagl = (EAGLContext *) env->ctx;
    [eagl release];

    env->dpy = EGL_NO_DISPLAY;
    env->sfc = EGL_NO_SURFACE;
    env->ctx = EGL_NO_CONTEXT;
    env->globjs.fbo = 0;
    env->globjs.rbo[0] = env->globjs.rbo[1] = 0;
}

static intptr_t eglenv_init(void* uptr, eglenv_t* env)
{
    GLView* view = (GLView *) uptr;
    CAEAGLLayer* layer = (CAEAGLLayer *) view.layer;

    NSNumber* num = [NSNumber numberWithBool:YES];
    if (num == nil) {
        return -1;
    }

    NSDictionary* dict = [NSDictionary dictionaryWithObject:num forKey:kEAGLDrawablePropertyRetainedBacking];
    if (dict == nil) {
        return -1;
    }
    layer.drawableProperties = dict;

    // no lock need here
    // eglenv_init will be and only be excuted in main thread
    EAGLContext* eagl_ctx = [[EAGLContext alloc] initWithAPI: kEAGLRenderingAPIOpenGLES2];
    if (eagl_ctx == nil) {
        return -1;
    }

    if (![EAGLContext setCurrentContext:eagl_ctx]) {
        [eagl_ctx release];
        return -1;
    }
    env->ctx = (void *) eagl_ctx;

    GLContext* ctx = &env->globjs;
    glcheck(glGenFramebuffers(1, &ctx->fbo));
    glcheck(glGenRenderbuffers(2, &ctx->rbo[0]));
    glcheck(glBindFramebuffer(GL_FRAMEBUFFER, ctx->fbo));
    glcheck(glBindRenderbuffer(GL_RENDERBUFFER, ctx->rbo[0]));

    BOOL b = [eagl_ctx renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer];
    if (!b) {
        egl_frameBuffer_free(env);
        return -1;
    }
    glcheck(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, ctx->rbo[0]));

    GLint width, height;
    glcheck(glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width));
    glcheck(glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height));

    glcheck(glBindRenderbuffer(GL_RENDERBUFFER, ctx->rbo[1]));
    glcheck(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height));
    glcheck(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ctx->rbo[1]));

    glcheck(glBindRenderbuffer(GL_RENDERBUFFER, ctx->rbo[0]));
    return 0;
}

static intptr_t eglenv_make_current(void* uptr, eglenv_t* env, intptr_t attach)
{
    (void) uptr;
    if (attach == 0) {
        return 0;
    }

    if (env->ctx == [EAGLContext currentContext]) {
        return 0;
    }

    BOOL b = [EAGLContext setCurrentContext:env->ctx];
    if (!b) {
        my_assert(0);
        return -1;
    }

    glcheck(glBindFramebuffer(GL_FRAMEBUFFER, env->globjs.fbo));
    glcheck(glBindRenderbuffer(GL_RENDERBUFFER, env->globjs.rbo[0]));
    return 0;
}

static intptr_t eglenv_draw(void* uptr, eglenv_t* env)
{
    (void) uptr;
    EAGLContext* eagl = (EAGLContext *) env->ctx;
    BOOL b = [eagl presentRenderbuffer:GL_RENDERBUFFER];
    if (!b) {
        my_assert(0);
        return -1;
    }
    return 0;
}

static void eglenv_finalize(void* uptr, eglenv_t* env)
{
    GLView* view = (GLView *) uptr;
    egl_frameBuffer_free(env);
    [view super_dealloc];
}

static eglop_t eglop = {
    .init_fptr = eglenv_init,
    .attach_fptr = eglenv_make_current,
    .draw_fptr = eglenv_draw,
    .finalize_fptr = eglenv_finalize
};
