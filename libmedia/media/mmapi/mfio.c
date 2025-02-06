
#include "mfio.h"
#include "mem.h"
#include "fmt.h"
#include "list_head.h"
#include "my_handle.h"
#include "my_errno.h"
#include "media_buffer.h"
#include "lock.h"
#include "now.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>

typedef struct {
    media_iden_t uid;
    uint32_t delay;
    uint32_t duration;
    uint32_t offset;

    uint32_t fourcc;
    union {
        struct {
            uint8_t chans;
            uint8_t samrate;
        } a;

        struct {
            uint16_t w, h;
        } v;
    };
    uint32_t next;
} stream_header_t;

typedef struct {
    uint32_t prev, next;
    uint64_t pts;
    uint32_t frametype;
    uint32_t angle;
    uint32_t bytes;
} frame_t;

typedef struct {
    lock_t lck;
    int fd;

    struct list_head head;
    uint32_t base;
    uint32_t cursor;
    uint32_t next;
} stream_file_t;

typedef struct {
    media_iden_t iden;
    uint64_t pts;
    uint32_t duration;
    uint32_t tms;
    uint32_t offset;
    uint32_t bytes;
    uint32_t pos[2];
    fourcc** fmt;

    lock_t lck;
    uint32_t token, header;
    struct list_head head;
    struct list_head entry;

    my_handle* handle;
    my_handle* self;
    stream_file_t* writer;
} writer_entry_t;

static intptr_t write_stream(stream_file_t* writer, writer_entry_t* went)
{
    intptr_t n = 0;
    struct list_head head;
    lock(&went->lck);
    list_add(&head, &went->head);
    list_del_init(&went->head);
    unlock(&went->lck);

    lock(&writer->lck);
    while (!list_empty(&head)) {
        struct list_head* ent = head.next;
        list_del(ent);
        struct my_buffer* mbuf = list_entry(ent, struct my_buffer, head);
        media_buffer* media = (media_buffer *) mbuf->ptr[0];

        if (media->fragment[0] == 0) {
            frame_t frame;
            frame.prev = went->pos[0];
            frame.next = (uint32_t) -1;
            frame.pts = media->pts;
            frame.frametype = media->frametype;
            frame.angle = media->angle;

            uint32_t bytes = write_n(writer->fd, &frame, sizeof(frame));
            writer->cursor += bytes;
            went->bytes = 0;
            if (bytes == sizeof(frame)) {
                went->pos[1] = writer->cursor - bytes;
            } else {
                n = -1;
                mbuf->mop->free(mbuf);
                break;
            }
        }

        for (int i = 0; i < 3; i++) {
            if (media->vp[i].ptr == NULL) {
                break;
            }

            uint32_t bytes = media->vp[i].stride * media->vp[i].height;
            uint32_t nb = write_n(writer->fd, media->vp[i].ptr, bytes);
            writer->cursor += nb;
            went->bytes += nb;
            if (nb != bytes) {
                n = -1;
                break;
            }
        }

        mbuf->mop->free(mbuf);
        if (n == -1) {
            break;
        }

        if (media->fragment[0] == media->fragment[1] - 1) {
            uint32_t pos = went->pos[1] + offsetof(frame_t, bytes);
            uint32_t bytes = write_np(writer->fd, &went->bytes, sizeof(uint32_t), pos);
            if (sizeof(uint32_t) != bytes) {
                n = -1;
                break;
            }
            went->bytes = 0;

            bytes = write_np(writer->fd, &went->pos[1], sizeof(uint32_t), went->pos[0]);
            if (sizeof(uint32_t) != bytes) {
                n = -1;
                break;
            }
            went->pos[0] = went->pos[1] + offsetof(frame_t, next);

            if (went->offset == (uint32_t) -1) {
                went->offset = went->pos[1];
            }

            if (went->tms == (uint32_t) -1) {
                uint32_t current = now();
                went->tms = current;
                if (writer->base == (uint32_t) -1) {
                    writer->base = current;
                }
            }

            if (media->pts > went->pts) {
                went->duration = (uint32_t) (media->pts - went->pts);
            }
        }
    }

    unlock(&writer->lck);
    free_buffer(&head);
    return n;
}

static intptr_t stream_flush(stream_file_t* writer, writer_entry_t* went)
{
    intptr_t ret = -1, n = write_stream(writer, went);
    if (n == -1) {
        return -1;
    }

    stream_header_t shd;
    shd.uid = went->iden;
    shd.delay = went->tms - writer->base;
    shd.duration = went->duration;
    shd.offset = went->offset;
    my_assert(shd.offset != (uint32_t) -1);

    shd.fourcc = went->fmt[0]->code;
    if (audio_type == media_type(went->fmt[0])) {
        audio_format* fmt = to_audio_format(went->fmt);
        shd.a.chans = fmt->pcm->channel;
        shd.a.samrate = fmt->pcm->samrate;
    } else {
        video_format* fmt = to_video_format(went->fmt[0]);
        shd.v.w = fmt->pixel->size->width;
        shd.v.h = fmt->pixel->size->height;
    }
    shd.next = (uint32_t) -1;

    lock(&writer->lck);
    n = write_n(writer->fd, &shd, sizeof(stream_header_t));
    if (n == sizeof(stream_header_t)) {
        int nb = write_np(writer->fd, &writer->cursor, sizeof(uint32_t), writer->next);
        if (nb == sizeof(uint32_t)) {
            ret = 0;
            writer->next = writer->cursor + offsetof(stream_header_t, next);
        }
    }
    writer->cursor += n;
    unlock(&writer->lck);
    return ret;
}

static void writer_clean(void* addr)
{
    stream_file_t* writer = (stream_file_t *) addr;

    while (!list_empty(&writer->head)) {
        struct list_head* ent = writer->head.next;
        list_del_init(ent);

        writer_entry_t* went = list_entry(ent, writer_entry_t, entry);
        my_handle* handle = went->self;
        handle_clone(handle);
        went = handle_get(handle);
        my_assert(went != NULL);
        went->self = NULL;
        handle_dettach(handle);

        stream_flush(writer, went);
        handle_put(handle);
        handle_release(handle);
    }

    close(writer->fd);
    heap_free(writer);
}

static void* writer_thread(void* cptr)
{
    my_handle* handle = (my_handle *) cptr;
    stream_file_t* writer = (stream_file_t *) handle_get(handle);

    uint32_t token = 1;
    while (writer != NULL) {
        token += 2;

        while (1) {
            writer_entry_t* went = NULL;
            lock(&writer->lck);

            if (!list_empty(&writer->head)) {
                struct list_head* ent = writer->head.next;
                went = list_entry(ent, writer_entry_t, entry);
                if (went->token == token) {
                    went = NULL;
                } else {
                    went->token = token;
                    list_del(ent);
                    list_add_tail(ent, &writer->head);

                    handle_clone(went->self);
                    went = (writer_entry_t *) handle_get(went->self);
                    my_assert(went != NULL);
                }
            }
            unlock(&writer->lck);

            if (went == NULL) {
                break;
            }

            intptr_t n = write_stream(writer, went);
            my_handle* went_handle = went->self;
            handle_put(went_handle);
            if (n == 0) {
                handle_release(went_handle);
            } else {
                lock(&writer->lck);
                if (!list_empty(&went->entry)) {
                    my_assert(went->writer == NULL);
                    list_del_init(&went->entry);
                    went->self = NULL;
                } else {
                    my_assert(went->writer != NULL);
                    handle_put(went->handle);
                    went->writer = NULL;
                }
                unlock(&writer->lck);

                handle_release(handle);
                handle_dettach(handle);
            }
        }

        handle_put(handle);
        usleep(1000 * 100);
        writer = (stream_file_t *) handle_get(handle);
    }

    handle_release(handle);
    return NULL;
}

static void* mfio_writer_open(const char* file, media_buffer* audio, media_buffer* video)
{
    (void) audio;
    (void) video;

    errno = ENOMEM;
    my_handle* handle = NULL;
    stream_file_t* writer = heap_alloc(writer);
    if (writer == NULL) {
        return NULL;
    }

    writer->fd = open(file, O_CREAT, S_IRUSR | S_IWUSR);
    if (writer->fd == -1) {
        heap_free(writer);
        return NULL;
    }

    uint32_t next = (uint32_t) -1;
    uint32_t nb = write_n(writer->fd, &next, sizeof(uint32_t));
    if (nb == sizeof(uint32_t)) {
        handle = handle_attach(writer, writer_clean);
    }

    if (handle == NULL) {
        close(writer->fd);
        remove(file);
        return NULL;
    }

    INIT_LIST_HEAD(&writer->head);
    writer->base = (uint32_t) -1;
    writer->lck = lock_val;
    writer->cursor = sizeof(uint32_t);
    writer->next = 0;

    pthread_t tid;
    int n = pthread_create(&tid, NULL, writer_thread, handle);
    if (n == 0) {
        handle_clone(handle);
        pthread_detach(tid);
    } else {
        handle_dettach(handle);
        handle = NULL;
    }
    return handle;
}

static void stream_clean(void* addr)
{
    writer_entry_t* went = (writer_entry_t *) addr;
    my_assert(went->self == NULL);
    if (went->writer != NULL) {
        stream_flush(went->writer, went);
        handle_put(went->handle);
    }
    handle_release(went->handle);

    free_buffer(&went->head);
    heap_free(went);
}

static void* mfio_add_stream(void* mptr)
{
    stream_file_t* writer = (stream_file_t *) handle_get((my_handle *) mptr);
    if (writer == NULL) {
        errno = ENOENT;
        return NULL;
    }

    errno = ENOMEM;
    writer_entry_t* went = heap_alloc(went);
    if (went == NULL) {
        handle_put((my_handle *) mptr);
        return NULL;
    }

    my_handle* handle = handle_attach(went, stream_clean);
    if (handle == NULL) {
        handle_put((my_handle *) mptr);
        heap_free(went);
        return NULL;
    }

    memset(went, 0, sizeof(writer_entry_t));
    INIT_LIST_HEAD(&went->head);
    went->iden = media_id_unkown;
    went->lck = lock_val;
    went->offset = (uint32_t) -1;
    went->tms = (uint32_t) -1;
    went->pts = (uint64_t) -1;
    went->pos[0] = went->pos[1] = (uint32_t) -1;
    went->handle = (my_handle *) mptr;
    handle_clone(went->handle);

    handle_clone(handle);
    lock(&writer->lck);
    list_add_tail(&went->entry, &writer->head);
    unlock(&writer->lck);

    handle_put((my_handle *) mptr);
    return handle;
}

static intptr_t mfio_stream_write(void* mptr, struct my_buffer* mbuf)
{
    my_handle* handle = (my_handle *) mptr;
    writer_entry_t* went = (writer_entry_t *) handle_get(handle);
    if (went == NULL) {
        return -1;
    }

    struct my_buffer* header = NULL;
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    if (media_type_raw(media->pptr_cc)) {
        handle_put(handle);
        return -1;
    }

    if (went->header == 0) {
        if (media_has_header(media->pptr_cc[0])) {
            header = stream_header_get(media->iden, media->pptr_cc);
            if (header == NULL) {
                handle_put(handle);
                return -1;
            }
        }
        went->fmt = media->pptr_cc;
        went->pts = media->pts;
        went->iden = media->iden;
        went->header = 1;
    }
    mbuf = mbuf->mop->clone(mbuf);

    lock(&went->lck);
    if (header != NULL) {
        list_add_tail(&header->head, &went->head);
    }

    if (mbuf != NULL) {
        list_add_tail(&mbuf->head, &went->head);
    }
    unlock(&went->lck);

    handle_put(handle);
    return 0;
}

static void mfio_delete_stream(void* mptr)
{
    my_handle* handle = (my_handle *) mptr;
    writer_entry_t* went = (writer_entry_t *) handle_get(handle);
    if (went == NULL) {
        handle_release(handle);
        return;
    }

    stream_file_t* writer = (stream_file_t *) handle_get(went->handle);
    if (writer == NULL) {
        handle_put(handle);
        handle_release(handle);
        return;
    }

    lock(&writer->lck);
    intptr_t empty = list_empty(&went->entry);
    if (!empty) {
        list_del_init(&went->entry);
    }
    unlock(&writer->lck);

    if (!empty) {
        handle_release(went->self);
        went->self = NULL;
        went->writer = writer;
    } else {
        handle_put(went->handle);
    }
    handle_put(handle);
    handle_dettach(handle);
}

static void mfio_writer_close(void* mptr)
{
    my_handle* handle = (my_handle *) mptr;
    handle_dettach(handle);
}

mfio_writer_t my_mfio_writer = {
    .open = mfio_writer_open,
    .add_stream = mfio_add_stream,
    .delete_stream = mfio_delete_stream,
    .write = mfio_stream_write,
    .close = mfio_writer_close
};
