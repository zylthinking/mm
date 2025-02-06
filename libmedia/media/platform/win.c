
#include "win.h"
#include "mem.h"
#include "shader.h"
#include "stage.h"
#include "area.h"
#if defined(__ANDROID__)
#include "android.h"
#endif

static LIST_HEAD(zombie);
static lock_t lck = lock_initial;

static intptr_t glenv_begin(window_t* wind)
{
    wind->shader = shader_create_objects();
    if (wind->shader == NULL) {
        return -1;
    }

    glcheck(glEnable(GL_DEPTH_TEST));
    glcheck(glDepthMask(GL_TRUE));
    glcheck(glDepthFunc(GL_LEQUAL));

    glcheck(glDepthRangef(0.0, 1.0));
    glcheck(glClearDepthf(1.0));
    glcheck(glClearColor(wind->r, wind->g, wind->b, wind->a));
    glcheck(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    glcheck(glGenVertexArrays(1, &wind->gl.vao_id));
    glcheck(glBindVertexArray(wind->gl.vao_id));
    glcheck(glGenTextures(elements(wind->gl.texture_obj), &wind->gl.texture_obj[0]));
    glcheck(glActiveTexture(GL_TEXTURE0));

    for (int i = 0; i < elements(wind->gl.texture_obj); ++i) {
        glcheck(glBindTexture(GL_TEXTURE_2D, wind->gl.texture_obj[i]));
        glcheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glcheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        glcheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        glcheck(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    }

    GLfloat textures[] = {
        0.0, 1.0,
        1.0, 1.0,
        0.0, 0.0,
        1.0, 0.0
    };

    glcheck(glGenBuffers(1, &wind->gl.tbuf));
    glcheck(glBindBuffer(GL_ARRAY_BUFFER, wind->gl.tbuf));
    glcheck(glBufferData(GL_ARRAY_BUFFER, sizeof(textures), textures, GL_STATIC_DRAW));
    shader_update_texture(wind->shader);

    GLfloat vertexes[] = {
#define z1 0.0
#define z2 1.0
        0.0, 0.0, z1,  1.0, 0.0, z1,  0.0, 1.0, z1,  1.0, 1.0, z1,
        0.0, 0.0, z2,  1.0, 0.0, z2,  0.0, 1.0, z2,  1.0, 1.0, z2,
        0.0, 1.0, z1,  0.0, 0.0, z1,  1.0, 1.0, z1,  1.0, 0.0, z1,
        0.0, 1.0, z2,  0.0, 0.0, z2,  1.0, 1.0, z2,  1.0, 0.0, z2,
        1.0, 1.0, z1,  0.0, 1.0, z1,  1.0, 0.0, z1,  0.0, 0.0, z1,
        1.0, 1.0, z2,  0.0, 1.0, z2,  1.0, 0.0, z2,  0.0, 0.0, z2,
        1.0, 0.0, z1,  1.0, 1.0, z1,  0.0, 0.0, z1,  0.0, 1.0, z1,
        1.0, 0.0, z2,  1.0, 1.0, z2,  0.0, 0.0, z2,  0.0, 1.0, z2,

        1.0, 0.0, z1,  0.0, 0.0, z1,  1.0, 1.0, z1,  0.0, 1.0, z1,
        1.0, 0.0, z2,  0.0, 0.0, z2,  1.0, 1.0, z2,  0.0, 1.0, z2,
        1.0, 1.0, z1,  1.0, 0.0, z1,  0.0, 1.0, z1,  0.0, 0.0, z1,
        1.0, 1.0, z2,  1.0, 0.0, z2,  0.0, 1.0, z2,  0.0, 0.0, z2,
        0.0, 1.0, z1,  1.0, 1.0, z1,  0.0, 0.0, z1,  1.0, 0.0, z1,
        0.0, 1.0, z2,  1.0, 1.0, z2,  0.0, 0.0, z2,  1.0, 0.0, z2,
        0.0, 0.0, z1,  0.0, 1.0, z1,  1.0, 0.0, z1,  1.0, 1.0, z1,
        0.0, 0.0, z2,  0.0, 1.0, z2,  1.0, 0.0, z2,  1.0, 1.0, z2
#undef z1
#undef z2
    };

    glcheck(glGenBuffers(1, &wind->gl.vbuf));
    glcheck(glBindBuffer(GL_ARRAY_BUFFER, wind->gl.vbuf));
    glcheck(glBufferData(GL_ARRAY_BUFFER, sizeof(vertexes), vertexes, GL_STATIC_DRAW));
    shader_ortho(wind->shader, 0.0, 1.0, 0.0, 1.0, -1.0, 1.0);

    wind->gl.z = 0;
    shader_update_vertex(wind->shader, 0, 1, 0);
    return 0;
}

void glenv_current_z(window_t* wind, uintptr_t z, uintptr_t angle)
{
    shader_update_vertex(wind->shader, (int) z, 0, (int) angle);
}

intptr_t glenv_change_mask(window_t* wind, intptr_t idx, float fx, float fy, float fw, float fh, float fz)
{
    return shader_change_mask(wind->shader, idx, fx, fy, fw, fh, fz);
}

intptr_t glenv_clear_mask(window_t* wind)
{
    return shader_clear_mask(wind->shader);
}

void glenv_ratio(window_t* wind, float width, float height)
{
    shader_texture_ratio(wind->shader, width, height);
}

static void glenv_end(window_t* wind)
{
    if (wind->gl.vao_id != 0) {
        glcheck(glDeleteVertexArrays(1, &wind->gl.vao_id));
    }

    for (int i = 0; i < elements(wind->gl.texture_obj); ++i) {
        if (wind->gl.texture_obj[i] != 0) {
            glcheck(glDeleteTextures(1, &wind->gl.texture_obj[i]));
        }
    }

    if (wind->gl.vbuf != 0) {
        glcheck(glDeleteBuffers(1, &wind->gl.vbuf));
    }

    if (wind->gl.tbuf != 0) {
        glcheck(glDeleteBuffers(1, &wind->gl.tbuf));
    }

    shader_delete_objects(wind->shader);
    wind->shader = NULL;
}

static void window_destroy(void* addr)
{
    window_t* win = (window_t *) addr;
    intptr_t n = gl_enter(win);
#if defined(__APPLE__)
    if (n == 1) {
        lock(&lck);
        // safe even win->head is not empty
        // the lost stages will take care of themselves
        list_add(&win->head, &zombie);
        unlock(&lck);
        return;
    }
#endif

    if (n == 0) {
        glenv_end(win);
        gl_leave(win, 1);
        win->eglop->finalize_fptr(win->uptr, &win->egl);
    } else {
        mark("the window_destroy failed to clean up gl resource\n");
    }
    my_free(win);
}

my_handle* window_create(float r, float g, float b, float a, uintptr_t w, uintptr_t h, void* uptr, eglop_t* eglop)
{
    window_t* win = NULL;
    lock(&lck);
    while (!list_empty(&zombie)) {
        struct list_head* ent = zombie.next;
        list_del(ent);
        if (win != NULL) {
            my_free(win);
        }

        win = list_entry(ent, window_t, head);
        glenv_end(win);
        win->eglop->finalize_fptr(win->uptr, &win->egl);
    }
    unlock(&lck);

    if (win == NULL) {
        win = (window_t *) my_malloc(sizeof(window_t));
        if (win == NULL) {
            return NULL;
        }
    }

    memset(win, 0, sizeof(window_t));
    win->r = r;
    win->g = g;
    win->b = b;
    win->a = a;
    win->uptr = uptr;
    win->eglop = eglop;
    win->lck = lock_val;
    win->sync = 2;
    win->killed = 0;
    win->dirty = 0;
#if defined(__ANDROID__)
    win->egl_bug_dirty = 0;
#endif
    win->shader = NULL;
    win->egl.w = w;
    win->egl.h = h;
    win->egl.dpy = EGL_NO_DISPLAY;
    win->egl.sfc = EGL_NO_SURFACE;
    win->egl.ctx = EGL_NO_CONTEXT;

    INIT_LIST_HEAD(&win->head);
    init_enter(&win->enter, 0);

    intptr_t n = win->eglop->init_fptr(win->uptr, &win->egl);
    if (n == -1) {
        my_free(win);
        return NULL;
    }

    if (-1 == glenv_begin(win)) {
        win->eglop->finalize_fptr(win->uptr, &win->egl);
        my_free(win);
        return NULL;
    }

    if (0 == gl_enter(win)) {
        win->eglop->draw_fptr(win->uptr, &win->egl);
        gl_leave(win, 1);
    }

    my_handle* handle = handle_attach(win, window_destroy);
    if (handle == NULL) {
        window_destroy(win);
    }
    return handle;
}

static void stage_resize(stage_t* stage, uintptr_t width, uintptr_t height)
{
    stage->area.x = 0;
    stage->area.y = 0;
    if (stage->area.angle & 1) {
        stage->area.w = stage->fmt->pixel->size->height;
        stage->area.h = stage->fmt->pixel->size->width;
    } else {
        stage->area.w = stage->fmt->pixel->size->width;
        stage->area.h = stage->fmt->pixel->size->height;
    }
    reopen_area(&stage->area, width, height);
}

void window_resize(window_t* wind, uintptr_t width, uintptr_t height)
{
    while (1) {
        intptr_t n = __sync_val_compare_and_swap(&wind->sync, 2, 0);
        if (n == 2) {
            my_assert(wind->sync == 0);
            break;
        }
        sched_yield();
    }

    wind->egl.w = width;
    wind->egl.h = height;
    lock(&wind->lck);

    if (list_empty(&wind->head)) {
        if (0 == gl_enter(wind)) {
            glClear(GL_COLOR_BUFFER_BIT);
            glenv_clear_mask(wind);
            wind->eglop->draw_fptr(wind->uptr, &wind->egl);
            gl_leave(wind, 1);
        }
#if defined(__ANDROID__)
        int64_t* feature = android_feature_address();
        intptr_t egl_bug_dirty = !!(feature[bug_mask] & bug_gl_clear);
        __sync_bool_compare_and_swap(&wind->egl_bug_dirty, 0, 1);
#endif
    } else {
        struct list_head* ent;
        for (ent = wind->head.next; ent != &wind->head; ent = ent->next) {
            stage_t* stage = list_entry(ent, stage_t, parent_entry);
            stage_resize(stage, width, height);
        }
        wind->dirty = 1;
    }

    unlock(&wind->lck);
    wind->sync = 2;
}

intptr_t gl_enter(window_t* wind)
{
    intptr_t n = wind->eglop->attach_fptr(wind->uptr, &wind->egl, 1);
    if (n == -1) {
        return -1;
    }

#if !defined(__APPLE__)
    return 0;
#endif

    n = enter_in(&wind->enter);
    return n;
}

void gl_leave(window_t* wind, intptr_t dettach)
{
#if !defined(__APPLE__)
    if (dettach) {
        wind->eglop->attach_fptr(wind->uptr, &wind->egl, 0);
    }
    return;
#endif
    enter_out(&wind->enter);
}

// should only be called only in main thread
void gl_allowed(window_t* wind, intptr_t allow)
{
#if !defined(__APPLE__)
    my_assert(0);
#endif

    enter_block(&wind->enter, !allow);
    if (allow == 0) {
        glcheck(glFlush());
    }
}
