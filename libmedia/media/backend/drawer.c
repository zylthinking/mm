
#include "drawer.h"
#include "stage.h"
#include "mem.h"
#include "media_buffer.h"

#if defined(__ANDROID__)
#include "android.h"
#include "pthread.h"

typedef struct {
    intptr_t intptr;
    lock_t lck0;
    lock_t lck1;
    stage_t* stage;
    struct my_buffer* stream;
} render_task_t;

static void* render_thread(void* any)
{
    render_task_t* task = (render_task_t *) any;
    do {
        lock(&task->lck0);
        task->intptr = rend_to_stage(task->stage, task->stream, task->intptr);
        unlock(&task->lck1);
    } while (1);
    return NULL;
}

static intptr_t rend_stage(stage_t* stage, struct my_buffer* stream, intptr_t sync)
{
    int64_t* feature = android_feature_address();
    if (__builtin_expect(0 == (feature[bug_mask] & bug_egl_dettach), 1)) {
        return rend_to_stage(stage, stream, sync);
    }

    static render_task_t task = {
        .lck0 = lock_initial_locked,
        .lck1 = lock_initial_locked
    };
    task.intptr = sync;
    task.stage = stage;
    task.stream = stream;

    static pthread_t tid = 0;
    if (tid == 0) {
        int n = pthread_create(&tid, NULL, render_thread, &task);
        if (n != 0) {
            my_assert(tid == 0);
            return -1;
        }
        my_assert(tid != 0);
        pthread_detach(tid);
    }
    unlock(&task.lck0);

    lock(&task.lck1);
    return task.intptr;
}
#else
#define rend_stage rend_to_stage
#endif

typedef struct {
    struct list_head entry;
    media_iden_t id;
    stage_t* stage;
} canvas_t;

typedef struct {
    struct list_head head;
} drawer_t;

void* drawer_get()
{
    drawer_t* draw = (drawer_t *) my_malloc(sizeof(drawer_t));
    if (draw != NULL) {
        INIT_LIST_HEAD(&draw->head);
    }
    return draw;
}

static canvas_t* canvas_get(drawer_t* draw, media_buffer* media, intptr_t alloc)
{
    canvas_t* canvas = NULL;
    struct list_head* ent;

    for (ent = draw->head.next; ent != &draw->head; ent = ent->next) {
        canvas = list_entry(ent, canvas_t, entry);
        if (media_id_eqal(canvas->id, media->iden)) {
            return canvas;
        }
    }

    if (!alloc) {
        return NULL;
    }

    canvas = (canvas_t *) my_malloc(sizeof(canvas_t));
    if (canvas != NULL) {
        canvas->id = media->iden;
        canvas->stage = stage_get(media);
        if (canvas->stage == NULL) {
            my_free(canvas);
            return NULL;
        }
        list_add(&canvas->entry, &draw->head);
    }
    return canvas;
}

static void canvas_put(canvas_t* canvas)
{
    list_del(&canvas->entry);
    stage_put(canvas->stage);
    my_free(canvas);
}

void drawer_write(void* rptr, struct list_head* headp)
{
    drawer_t* draw = (drawer_t *) rptr;
    canvas_t* canvas = NULL;
    struct list_head* ent;
    for (ent = draw->head.next; ent != &draw->head; ent = ent->next) {
        canvas = list_entry(ent, canvas_t, entry);
        wash_stage(canvas->stage);
    }

    struct list_head head;
    INIT_LIST_HEAD(&head);
    canvas = NULL;

    while (!list_empty(headp)) {
        struct list_head* ent = headp->prev;
        list_del(ent);

        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->vp[0].stride == 0) {
            list_add(ent, &head);
            continue;
        }

        if (canvas == NULL || !media_id_eqal(canvas->id, media->iden)) {
            canvas = canvas_get(draw, media, 1);
            if (canvas == NULL) {
                mbuf->mop->free(mbuf);
                continue;
            }
        } else {
            mbuf->mop->free(mbuf);
            continue;
        }

        if (-1 == rend_stage(canvas->stage, mbuf, 1)) {
            if (ENOENT == errno) {
                canvas_put(canvas);
                canvas = NULL;
            }
            mbuf->mop->free(mbuf);
        }
    }

    while (!list_empty(&head)) {
        struct list_head* ent = head.next;
        list_del(ent);

        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        media_iden_t id = media->iden;
        mbuf->mop->free(mbuf);

        if (canvas == NULL || !media_id_eqal(canvas->id, id)) {
            canvas = canvas_get(draw, media, 0);
            if (canvas == NULL) {
                continue;
            }
        }

        canvas_put(canvas);
        canvas = NULL;
    }
}

void drawer_put(void* rptr)
{
    drawer_t* draw = (drawer_t *) rptr;
    while (!list_empty(&draw->head)) {
        struct list_head* ent = draw->head.next;
        list_del(ent);
        canvas_t* canvas = list_entry(ent, canvas_t, entry);
        stage_put(canvas->stage);
        my_free(canvas);
    }
    my_free(rptr);
}
