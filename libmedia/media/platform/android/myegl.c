
#include "win.h"
#include "thread_clean.h"
#include "android.h"
#include <android/native_window.h>

static void egl_context_free(eglenv_t* env)
{
    if (EGL_NO_CONTEXT != env->ctx) {
        eglDestroyContext(env->dpy, env->ctx);
        env->ctx = EGL_NO_CONTEXT;
    }
}

static void egl_uninit(eglenv_t* env)
{
    EGLBoolean b = eglReleaseThread();
    my_assert(b);
    egl_context_free(env);

    if (EGL_NO_SURFACE != env->sfc) {
        eglDestroySurface(env->dpy, env->sfc);
        env->sfc = EGL_NO_SURFACE;
    }

    eglTerminate(env->dpy);
    env->dpy = EGL_NO_DISPLAY;
}

static intptr_t eglenv_reinit(void* uptr, eglenv_t* env)
{
    (void) uptr;
    egl_context_free(env);

    EGLint attr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    env->ctx = eglCreateContext(env->dpy, env->cfg, EGL_NO_CONTEXT, attr);
    if (EGL_NO_CONTEXT == env->ctx) {
        return -1;
    }

    if (!eglMakeCurrent(env->dpy, env->sfc, env->sfc, env->ctx)) {
        egl_context_free(env);
        return -1;
    }

    return 0;
}

static intptr_t eglenv_init(void* uptr, eglenv_t* env)
{
    ANativeWindow* android_window = (ANativeWindow *) uptr;
    env->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (EGL_NO_DISPLAY == env->dpy) {
        return -1;
    }

    if (!eglInitialize(env->dpy, NULL, NULL)) {
        return -1;
    }

    EGLint swapbit = EGL_SWAP_BEHAVIOR_PRESERVED_BIT;
    EGLint value;
    EGLint attr1[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | swapbit,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };

    EGLBoolean b;
LABEL:
    b = eglChooseConfig(env->dpy, attr1, &env->cfg, 1, &value);
    if ((!b || value == 0)) {
        if (swapbit != 0) {
            swapbit = 0;
            attr1[3] = EGL_WINDOW_BIT;
            goto LABEL;
        }
        egl_uninit(env);
        return -1;
    }

    if (!eglGetConfigAttrib(env->dpy, env->cfg, EGL_NATIVE_VISUAL_ID, &value)) {
        egl_uninit(env);
        return -1;
    }

    int32_t n = ANativeWindow_setBuffersGeometry(android_window, 0, 0, value);
    my_assert(0 == n);

    EGLint attr2[] = {
        EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
        EGL_NONE
    };

    env->sfc = eglCreateWindowSurface(env->dpy, env->cfg, android_window, attr2);
    if (EGL_NO_SURFACE == env->sfc) {
        egl_uninit(env);
        return -1;
    }

    if (swapbit != 0 && !eglSurfaceAttrib(env->dpy, env->sfc, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED)) {
        egl_uninit(env);
        return -1;
    }

    if (-1 == eglenv_reinit(uptr, env)) {
        egl_uninit(env);
        return -1;
    }
    return 0;
}

static void egl_exit_thread()
{
    int64_t* feature = android_feature_address();
    if (0 == feature[bug_mask] & bug_gl_flush) {
        glcheck(glFlush());
    }

    if (feature[bug_mask] & bug_egl_release) {
        eglMakeCurrent(eglGetCurrentDisplay(),
            EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    } else {
        EGLBoolean b = eglReleaseThread();
        my_assert(b);
    }
}

static void egl_dettach(void* value)
{
    (void) value;
    egl_exit_thread();
}

static intptr_t eglenv_make_current(void* uptr, eglenv_t* env, intptr_t attach)
{
    if (attach == 0) {
        egl_dettach(NULL);
        return 0;
    }

    if (NULL == thread_clean_push(1, egl_dettach, 0)) {
        errno = ENOENT;
        return -1;
    }

    if (!eglMakeCurrent(env->dpy, env->sfc, env->sfc, env->ctx)) {
        EGLint e = eglGetError();
        if (env->ctx == EGL_NO_CONTEXT || EGL_CONTEXT_LOST == e) {
            return eglenv_reinit(uptr, env);
        }
        mark("err = %d\n", (int) e);
        return -1;
    }

    // egl does not specific what depth buffer contains
    // after eglSwapBuffers, and sony does draw nothing
    // because of lack of this
    // can't put it after eglSwapBuffers, lenovo idiot
    // crashes there.
    glcheck(glClear(GL_DEPTH_BUFFER_BIT));
    return 0;
}

static intptr_t eglenv_draw(void* uptr, eglenv_t* env)
{
LABEL:
    if (!eglSwapBuffers(env->dpy, env->sfc)) {
        if ((EGL_CONTEXT_LOST == eglGetError()) &&
            (0 == eglenv_reinit(uptr, env)))
        {
            goto LABEL;
        }
        return -1;
    }
    return 0;
}

static void eglenv_finalize(void* uptr, eglenv_t* env)
{
    ANativeWindow* android_window = (ANativeWindow *) uptr;
    egl_uninit(env);
    ANativeWindow_release(android_window);
}

eglop_t eglop = {
    .init_fptr = eglenv_init,
    .attach_fptr = eglenv_make_current,
    .draw_fptr = eglenv_draw,
    .finalize_fptr = eglenv_finalize
};
