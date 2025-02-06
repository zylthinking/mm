
#ifndef win_h
#define win_h

#include "mydef.h"
#include "list_head.h"
#include "my_handle.h"
#include "gl.h"
#include "lock.h"
#include "enter.h"

#if defined(__APPLE__)
#define EGLDisplay intptr_t
#define EGLSurface intptr_t
#define EGLContext void*
#define EGLConfig intptr_t
#define EGL_NO_DISPLAY -1
#define EGL_NO_SURFACE -1
#define EGL_NO_CONTEXT NULL

typedef struct {
    GLuint fbo, rbo[2];
} GLContext;

#elif defined(__ANDROID__)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

typedef struct {
    EGLConfig cfg;
    EGLDisplay dpy;
    EGLSurface sfc;
    EGLContext ctx;
    uintptr_t w, h;
#if defined(__APPLE__)
    GLContext globjs;
#endif
} eglenv_t;

typedef struct {
    intptr_t (*init_fptr) (void* uptr, eglenv_t* env);
    intptr_t (*attach_fptr) (void* uptr, eglenv_t* env, intptr_t attach);
    intptr_t (*draw_fptr) (void* uptr, eglenv_t* env);
    void (*finalize_fptr) (void* uptr, eglenv_t* env);
} eglop_t;

typedef struct {
    GLuint texture_obj[nr_textures];
    GLuint vao_id, vbuf, tbuf;
    uintptr_t z;
} glenv_t;

typedef struct {
    entry_point enter;
    float r, g, b, a;

    eglenv_t egl;
    glenv_t gl;
    intptr_t sync;
    intptr_t killed;
    intptr_t dirty;

#if defined(__ANDROID__)
    intptr_t egl_bug_dirty;
#endif

    lock_t lck;
    struct list_head head;
    void* uptr;
    eglop_t* eglop;
    void* shader;
} window_t;

capi my_handle* window_create(float r, float g, float b, float a, uintptr_t w, uintptr_t h, void* uptr, eglop_t* eglop);
capi void window_resize(window_t* win, uintptr_t width, uintptr_t height);
capi void glenv_current_z(window_t* wind, uintptr_t z, uintptr_t angle);
capi intptr_t glenv_change_mask(window_t* wind, intptr_t idx, float fx, float fy, float fw, float fh, float fz);
capi intptr_t glenv_clear_mask(window_t* wind);
capi void glenv_ratio(window_t* wind, float width, float height);

capi intptr_t gl_enter(window_t* wind);
capi void gl_leave(window_t* wind, intptr_t dettach);
capi void gl_allowed(window_t* wind, intptr_t allow);

#endif
