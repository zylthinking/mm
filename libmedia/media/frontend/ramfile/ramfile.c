
#include "ramfile.h"
#include "list_head.h"
#include "file_jointor.h"
#include "mem.h"
#include "fmt.h"
#include "lock.h"
#include "my_handle.h"

typedef struct {
    media_struct_t media;
    fourcc** cc;
    jointor* jointor;

    intptr_t ref;
    lock_t lck;
    struct list_head head[2];
} ramfile_t;

static void* file_open(void* any)
{
    ramfile_t* ram = (ramfile_t *) any;
    ram->media.any = as_stream;
    __sync_add_and_fetch(&ram->ref, 1);
    return any;
}

// this function assume being called in single thread, to avoid a lock on ram->head[0]
static int file_read_video(ramfile_t* ram, struct list_head* headp, const fraction* f)
{
    int nr = 0;
    uint64_t pts = (uint64_t) -1;
    if (f != NULL) {
        pts = f->num;
    }

    if (pts == 0) {
        if (list_empty(&ram->head[0])) {
            lock(&ram->lck);
            list_add(&ram->head[0], &ram->head[1]);
            list_del_init(&ram->head[1]);
            unlock(&ram->lck);
        }

        if (list_empty(&ram->head[0])) {
            return 0;
        }

        struct my_buffer* mbuf = list_entry(ram->head[0].next, struct my_buffer, head);
        struct my_buffer* mbuf2 = mbuf->mop->clone(mbuf);
        if (mbuf2 == NULL) {
            return 0;
        }

        mbuf2->ptr[1] += mbuf2->length;
        mbuf2->length = 0;
        list_add(&mbuf2->head, headp);
        return 1;
    }

LABEL:
    while (!list_empty(&ram->head[0])) {
        struct list_head* ent = ram->head[0].next;
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        if (media->pts > pts) {
            return nr;
        }

        ++nr;
        list_del(ent);
        list_add_tail(ent, headp);
    }

    lock(&ram->lck);
    list_add(&ram->head[0], &ram->head[1]);
    list_del_init(&ram->head[1]);
    unlock(&ram->lck);

    if (!list_empty(&ram->head[0])) {
        goto LABEL;
    }
    return nr;
}

static int file_read(void* id, struct list_head* headp, const fraction* f)
{
    int n = 0;
    ramfile_t* ram = (ramfile_t *) id;
    if (ram->cc == NULL) {
        return 0;
    }

    if (audio_type == media_type(*ram->cc)) {
        my_assert(0);
        n = -1;
    } else {
        n = file_read_video(ram, headp, f);
    }
    return n;
}

static void file_close(void* id)
{
    ramfile_t* ram = (ramfile_t *) id;
    free_buffer(&ram->head[0]);
    free_buffer(&ram->head[1]);
    if (0 == __sync_sub_and_fetch(&ram->ref, 1)) {
        my_free(ram);
    }
}

static media_operation_t ramops = {
    file_open,
    file_read,
    file_close
};

static void ramfile_free(void* addr)
{
    ramfile_t* ram = (ramfile_t *) addr;
    if (ram->jointor != NULL) {
        file_jointor_close(ram->jointor);
        ram->jointor = NULL;
    }

    if (0 == __sync_sub_and_fetch(&ram->ref, 1)) {
        my_free(ram);
    }
}

void* ramfile_open()
{
    ramfile_t* ram = (ramfile_t *) my_malloc(sizeof(ramfile_t));
    if (ram == NULL) {
        return NULL;
    }

    my_handle* handle = handle_attach(ram, ramfile_free);
    if (handle == NULL) {
        my_free(ram);
        return NULL;
    }

    INIT_LIST_HEAD(&ram->head[0]);
    INIT_LIST_HEAD(&ram->head[1]);
    ram->media.ops = &ramops;
    ram->cc = NULL;
    ram->lck = lock_val;
    ram->ref = 1;

    ram->jointor = file_jointor_open(&ram->media);
    if (ram->jointor == NULL) {
        handle_dettach(handle);
        handle = NULL;
    }
    return (void *) handle;
}

jointor* ramfile_jointor_get(void* ptr)
{
    ramfile_t* ram = (ramfile_t *) handle_get((my_handle *) ptr);
    if (ram == NULL) {
        return NULL;
    }

    jointor* join = ram->jointor;
    int n = join->ops->jointor_get(join);
    my_assert(n > 0);

    handle_put((my_handle *) ptr);
    return join;
}

int32_t ramfile_write(void* ptr, struct my_buffer* mbuf)
{
    ramfile_t* ram = (ramfile_t *) handle_get((my_handle *) ptr);
    if (ram == NULL) {
        return -1;
    }

    if (ram->cc == NULL) {
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        ram->cc = media->pptr_cc;
    }

    lock(&ram->lck);
    list_add_tail(&mbuf->head, &ram->head[1]);
    unlock(&ram->lck);

    handle_put((my_handle *) ptr);
    return 0;
}

void ramfile_close(void* ptr)
{
    handle_dettach((my_handle *) ptr);
}
