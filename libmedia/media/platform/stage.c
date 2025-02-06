
#include "stage.h"
#include "shader.h"
#include "my_handle.h"
#include "mem.h"
#include "media_buffer.h"
#include "win.h"
#include "gl.h"
#if defined(__ANDROID__)
#include "android.h"
#endif

static lock_t lck = lock_initial;
static LIST_HEAD(stage_list);

stage_t* stage_get(media_buffer* media)
{
    struct list_head* ent;
    stage_t* stage = NULL;

    for (ent = stage_list.next; ent != &stage_list; ent = ent->next) {
        stage = list_entry(ent, stage_t, sibling_entry);
        if (media_id_eqal(stage->area.identy, media->iden)) {
            return stage;
        }
    }

    stage = (stage_t *) my_malloc(sizeof(stage_t));
    if (stage == NULL) {
        return NULL;
    }

    video_format* fmt = to_video_format(media->pptr_cc);
    stage->area.identy = media->iden;
    stage->area.x = 0;
    stage->area.y = 0;
    stage->area.lck = lock_val;
    stage->area.idx = -1;
    if (media->angle & 1) {
        stage->area.w = fmt->pixel->size->height;
        stage->area.h = fmt->pixel->size->width;
    } else {
        stage->area.w = fmt->pixel->size->width;
        stage->area.h = fmt->pixel->size->height;
    }
    stage->area.angle = media->angle;

    intptr_t n = open_area(&stage->area);
    if (n == -1) {
        my_free(stage);
        return NULL;
    }

    window_t* wind = (window_t *) handle_get((my_handle *) stage->area.egl);
    if (wind == NULL) {
        close_area(&stage->area);
        my_free(stage);
        return NULL;
    }

    stage->w = stage->h = 0;
    stage->type = -1;
    stage->fmt = to_video_format(media->pptr_cc);
    stage->mbuf = NULL;

    lock(&wind->lck);
    list_add_tail(&stage->parent_entry, &wind->head);
    unlock(&wind->lck);
    handle_put((my_handle *) stage->area.egl);

    lock(&lck);
    list_add_tail(&stage->sibling_entry, &stage_list);
    unlock(&lck);
    return stage;
}

static void cache_stream(stage_t* stage, struct my_buffer* mbuf)
{
    if (stage->mbuf != NULL && stage->mbuf != mbuf) {
        stage->mbuf->mop->free(stage->mbuf);
    }
    stage->mbuf = mbuf;
}

static intptr_t area_mask_update(window_t* wind, area_t* area)
{
    float fx = (float) area->x / (float) wind->egl.w;
    float fy = (float) area->y / (float) wind->egl.h;
    float fw = (float) area->w / (float) wind->egl.w;
    float fh = ((float) area->h) / ((float) wind->egl.h);
    float fz = (float) area->z;
    area->idx = glenv_change_mask(wind, area->idx, fx, fy, fw, fh, fz);
    return area->idx;
}

static void window_refresh(window_t* wind, intptr_t surface_mismatch)
{
    intptr_t n = gl_enter(wind);
    if (n != 0) {
        return;
    }

    while (1) {
        n = __sync_val_compare_and_swap(&wind->sync, 2, 0);
        if (n == 2) {
            my_assert(wind->sync == 0);
            break;
        }
        sched_yield();
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glenv_clear_mask(wind);

#if !defined(__APPLE__)
    // this is neccessary because some times at some phone
    // the egl does not realized the window size has changed
    // so force a draw to sync. It works because egl spec force
    // this information to be synchronized at eglSwapBuffer call.
    if (surface_mismatch) {
        wind->eglop->draw_fptr(wind->uptr, &wind->egl);
    }

    int64_t* feature = android_feature_address();
    if (feature[bug_mask] & bug_gl_clear2) {
        glClear(GL_COLOR_BUFFER_BIT);
    }
#endif

    lock(&wind->lck);
    struct list_head* ent;
    for (ent = wind->head.next; ent != &wind->head; ent = ent->next) {
        stage_t* stage = list_entry(ent, stage_t, parent_entry);
        rend_to_stage(stage, NULL, 0);
    }
    unlock(&wind->lck);

    wind->eglop->draw_fptr(wind->uptr, &wind->egl);
    wind->sync = 2;
    gl_leave(wind, 0);
}

void wash_stage(stage_t* stage)
{
    my_handle* handle = (my_handle *) stage->area.egl;
    window_t* wind = (window_t *) handle_get(handle);
    if (wind == NULL) {
        return;
    }

    if (wind->dirty) {
        window_refresh(wind, 1);
        wind->dirty = 0;
    }
    handle_put(handle);
}

void stage_put(stage_t* stage)
{
    lock(&lck);
    list_del(&stage->sibling_entry);
    unlock(&lck);

    my_handle* handle = (my_handle *) stage->area.egl;
    window_t* wind = (window_t *) handle_get(handle);
    if (wind == NULL) {
        close_area(&stage->area);
        my_free(stage);
        return;
    }

    lock(&wind->lck);
    list_del(&stage->parent_entry);
    unlock(&wind->lck);

    // close_area will release handle
    // while, we are still using it.
    handle_clone(handle);
    intptr_t n = close_area(&stage->area);
    if (wind->killed == 0) {
        if (n == 1) {
            window_refresh(wind, 0);
        }
        handle_put(handle);
        handle_release(handle);
    } else {
        handle_put(handle);
        handle_dettach(handle);
    }

    cache_stream(stage, NULL);
    my_free(stage);
}

typedef struct {
    GLenum pixel_fmt;
    GLint internal_fmt;
    GLint pixel_layout;
    GLsizei width, height;
} texture_image_t;

static int images_fill(media_buffer* media, texture_image_t* images)
{
    video_format* fmt = to_video_format(media->pptr_cc);
    if (fmt->pixel->csp == csp_bgra) {
        images[0].pixel_fmt = GL_BGRA;
        images[0].internal_fmt = GL_RGBA;
        images[0].pixel_layout = GL_UNSIGNED_BYTE;
        images[0].width = (GLsizei) media->vp[0].stride;
        images[0].height = (GLsizei) my_min(media->vp[0].height, panel_height(0, fmt->pixel->size->height, csp_bgra));
        return shader_rgb;
    }

    if (fmt->pixel->csp == csp_rgb) {
        images[0].pixel_fmt = GL_RGB;
        images[0].internal_fmt = GL_RGB;
        images[0].pixel_layout = GL_UNSIGNED_BYTE;
        images[0].width = (GLsizei) media->vp[0].stride;
        images[0].height = (GLsizei) my_min(media->vp[0].height, panel_height(0, fmt->pixel->size->height, csp_rgb));
        return shader_rgb;
    }

    if (fmt->pixel->csp == csp_gbrp) {
        images[0].pixel_fmt = images[1].pixel_fmt = images[2].pixel_fmt = GL_LUMINANCE;
        images[0].internal_fmt = images[1].internal_fmt = images[2].internal_fmt = GL_LUMINANCE;
        images[0].pixel_layout = images[1].pixel_layout = images[2].pixel_layout = GL_UNSIGNED_BYTE;

        images[0].width = (GLsizei) media->vp[0].stride;
        images[0].height = (GLsizei) my_min(media->vp[0].height, panel_height(0, fmt->pixel->size->height, csp_gbrp));
        images[1].width = (GLsizei) media->vp[1].stride;
        images[1].height = (GLsizei) my_min(media->vp[1].height, panel_height(1, fmt->pixel->size->height, csp_gbrp));
        images[2].width = (GLsizei) media->vp[2].stride;
        images[2].height = (GLsizei) media->vp[2].height;
        return shader_gbrp;
    }

    if (fmt->pixel->csp == csp_i420 || fmt->pixel->csp == csp_i420ref) {
        images[0].pixel_fmt = GL_LUMINANCE;
        images[0].internal_fmt = GL_LUMINANCE;
        images[0].pixel_layout = GL_UNSIGNED_BYTE;
        images[1].pixel_fmt = images[2].pixel_fmt = GL_LUMINANCE;
        images[1].internal_fmt = images[2].internal_fmt = GL_LUMINANCE;
        images[1].pixel_layout = images[2].pixel_layout = GL_UNSIGNED_BYTE;

        images[0].width = (GLsizei) media->vp[0].stride;
        images[0].height = (GLsizei) my_min(media->vp[0].height, panel_height(0, fmt->pixel->size->height, csp_i420));
        images[1].width = (GLsizei) media->vp[1].stride;
        images[1].height = (GLsizei) my_min(media->vp[1].height, panel_height(1, fmt->pixel->size->height, csp_i420));
        images[2].width = (GLsizei) media->vp[2].stride;
        images[2].height = (GLsizei) my_min(media->vp[2].height, panel_height(2, fmt->pixel->size->height, csp_i420));
        return shader_i420;
    }

    if (fmt->pixel->csp == csp_nv12 || fmt->pixel->csp == csp_nv12ref) {
        images[0].pixel_fmt = GL_LUMINANCE;
        images[0].internal_fmt = GL_LUMINANCE;
        images[0].pixel_layout = GL_UNSIGNED_BYTE;
        images[0].width = (GLsizei) media->vp[0].stride;
        images[0].height = (GLsizei) my_min(media->vp[0].height, panel_height(0, fmt->pixel->size->height, csp_nv12));

        images[1].pixel_fmt = GL_LUMINANCE_ALPHA;
        images[1].internal_fmt = GL_LUMINANCE_ALPHA;
        images[1].pixel_layout = GL_UNSIGNED_BYTE;
        images[1].width = (GLsizei) media->vp[1].stride / 2;
        images[1].height = (GLsizei) my_min(media->vp[1].height, panel_height(1, fmt->pixel->size->height, csp_nv12));
        return shader_nv12;
    }

    if (fmt->pixel->csp == csp_nv21 || fmt->pixel->csp == csp_nv21ref) {
        images[0].pixel_fmt = GL_LUMINANCE;
        images[0].internal_fmt = GL_LUMINANCE;
        images[0].pixel_layout = GL_UNSIGNED_BYTE;
        images[0].width = (GLsizei) media->vp[0].stride;
        images[0].height = (GLsizei) my_min(media->vp[0].height, panel_height(0, fmt->pixel->size->height, csp_nv21));

        images[1].pixel_fmt = GL_LUMINANCE_ALPHA;
        images[1].internal_fmt = GL_LUMINANCE_ALPHA;
        images[1].pixel_layout = GL_UNSIGNED_BYTE;
        images[1].width = (GLsizei) media->vp[1].stride / 2;
        images[1].height = (GLsizei) my_min(media->vp[1].height, panel_height(1, fmt->pixel->size->height, csp_nv21));
        return shader_nv21;
    }

    return -1;
}

static void rend_to_texture(struct my_buffer* stream, stage_t* stage, window_t* win)
{
    media_buffer* media = (media_buffer *) stream->ptr[0];
    video_format* fmt = to_video_format(media->pptr_cc);

    static uint64_t pts = 0, seq = 0;
    pts = media->pts;
    seq = media->seq;

    texture_image_t images[3];
    intptr_t type = images_fill(media, images);
    if (type == -1) {
        logmsg("color space %d does not surpported yet by stage\n", fmt->pixel->csp);
        return;
    }

    shader_set_type(win->shader, (int) type);
    glenv_ratio(win, ((float) fmt->pixel->size->width) / ((float) images[0].width), 1.0);

    glcheck(glActiveTexture(GL_TEXTURE0));
    glcheck(glBindTexture(GL_TEXTURE_2D, win->gl.texture_obj[stage->area.z * 3 + 0]));

    int same = (stage->w == images[0].width &&
                stage->h == images[0].width &&
                stage->type == type);
    if (!same) {
        glcheck(glTexImage2D(GL_TEXTURE_2D, 0, images[0].internal_fmt, images[0].width, images[0].height,
                             0, images[0].pixel_fmt, images[0].pixel_layout, media->vp[0].ptr));
        stage->w = images[0].width;
        stage->h = images[0].height;
        stage->type = type;
    } else {
        glcheck(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, images[0].width, images[0].height,
                                images[0].pixel_fmt, images[0].pixel_layout, media->vp[0].ptr));
    }

    if (type == shader_rgb) {
        return;
    }

    glcheck(glActiveTexture(GL_TEXTURE1));
    glcheck(glBindTexture(GL_TEXTURE_2D, win->gl.texture_obj[stage->area.z * 3 + 1]));
    if (!same) {
        glcheck(glTexImage2D(GL_TEXTURE_2D, 0, images[1].internal_fmt, images[1].width, images[1].height,
                             0, images[1].pixel_fmt, images[1].pixel_layout, media->vp[1].ptr));
    } else {
        glcheck(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, images[1].width, images[1].height,
                                images[1].pixel_fmt, images[1].pixel_layout, media->vp[1].ptr));
    }

    if (type == shader_nv12 || type == shader_nv21) {
        return;
    }

    glcheck(glActiveTexture(GL_TEXTURE2));
    glcheck(glBindTexture(GL_TEXTURE_2D, win->gl.texture_obj[stage->area.z * 3 + 2]));
    if (!same) {
        glcheck(glTexImage2D(GL_TEXTURE_2D, 0, images[2].internal_fmt, images[2].width, images[2].height,
                             0, images[2].pixel_fmt, images[2].pixel_layout, media->vp[2].ptr));
    } else {
        glcheck(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, images[2].width, images[2].height,
                                images[2].pixel_fmt, images[2].pixel_layout, media->vp[2].ptr));
    }
}

intptr_t rend_to_stage(stage_t* stage, struct my_buffer* stream, intptr_t sync)
{
    my_assert(sync == 0 || sync == 1);
    my_handle* handle = (my_handle *) stage->area.egl;
    window_t* wind = (window_t *) handle_get(handle);
    if (wind == NULL) {
        errno = ENOENT;
        return -1;
    }

    if (wind->killed == 1) {
        errno = ENOENT;
        handle_put(handle);
        return -1;
    }
    my_assert(wind->sync != 1);

    __sync_val_compare_and_swap(&wind->sync, 2, sync);
    if (wind->sync != sync) {
        errno = EAGAIN;
        handle_put(handle);
        return -1;
    }

    intptr_t n = gl_enter(wind);
    if (n != 0) {
        goto LABEL2;
    }

    if (stream == NULL) {
        stream = stage->mbuf;
        if (stream == NULL) {
            goto LABEL1;
        }
    }

#if defined(__ANDROID__)
    if (wind->egl_bug_dirty == 1) {
        __sync_bool_compare_and_swap(&wind->egl_bug_dirty, 1, 0);
        glcheck(glClear(GL_COLOR_BUFFER_BIT));
    }
#endif
    rend_to_texture(stream, stage, wind);
    glcheck(glBindVertexArray(wind->gl.vao_id));

    area_t templ;
    lock(&stage->area.lck);
    templ.z = stage->area.z;
    templ.x = stage->area.x;
    templ.y = stage->area.y;
    templ.w = stage->area.w;
    templ.h = stage->area.h;
    unlock(&stage->area.lck);
    templ.idx = stage->area.idx;
    stage->area.idx = area_mask_update(wind, &templ);

    media_buffer* media = (media_buffer *) stream->ptr[0];
    intptr_t z = (templ.z << 8 | media->angle);
    if (wind->gl.z != z) {
        glenv_current_z(wind, stage->area.z, media->angle);
        wind->gl.z = z;
    }

    glcheck(glViewport((GLint) templ.x, (GLint) templ.y, (GLsizei) templ.w, (GLsizei) templ.h));
    glcheck(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
    if (__builtin_expect((wind->sync == 1), 1)) {
        wind->eglop->draw_fptr(wind->uptr, &wind->egl);
    }
LABEL1:
    gl_leave(wind, 0);
LABEL2:
    cache_stream(stage, stream);
    // must reset wind->sync after cache_stream
    // because window_refresh will access the memory.
    // no mb needed here because __sync_XXX make it no problem.
    if (__builtin_expect((wind->sync == 1), 1)) {
        wind->sync = 2;
    }
    handle_put(handle);
    return 0;
}

void area_invalid(area_t* area, area_t* templ)
{
    lock(&area->lck);
    area->x = templ->x;
    area->y = templ->y;
    area->z = templ->z;
    area->w = templ->w;
    area->h = templ->h;
    unlock(&area->lck);

    my_handle* handle = (my_handle *) area->egl;
    window_t* wind = (window_t *) handle_get(handle);
    if (wind == NULL) {
        return;
    }

    while (1) {
        intptr_t n = __sync_val_compare_and_swap(&wind->sync, 2, 0);
        if (n == 2) {
            my_assert(wind->sync == 0);
            break;
        }
        sched_yield();
    }

    if (list_empty(&wind->head)) {
        if (0 == gl_enter(wind)) {
            glClear(GL_COLOR_BUFFER_BIT);
            glenv_clear_mask(wind);
            wind->eglop->draw_fptr(wind->uptr, &wind->egl);
            gl_leave(wind, 1);
        }
    } else {
        wind->dirty = 1;
    }
    wind->sync = 2;
    handle_put(handle);
}

void do_close_area(area_t* area)
{
    my_handle* handle = (my_handle *) area->egl;
    handle_release(handle);
}

intptr_t __attribute__((weak)) open_area(area_t* area)
{
    my_assert(0);
    return -1;
}

void __attribute__((weak)) reopen_area(area_t* area, uintptr_t width, uintptr_t height)
{
    my_assert(0);
}

intptr_t  __attribute__((weak)) close_area(area_t* area)
{
    my_assert(0);
    return 0;
}
