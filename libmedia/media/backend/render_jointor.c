
#include "render_jointor.h"
#include "audio.h"
#include "lock.h"
#include "mbuf.h"
#include "mem.h"
#include "my_handle.h"
#include "my_errno.h"
#include "drawer.h"
#include <pthread.h>
#include <string.h>
#include "semaphore2.h"

struct render {
    jointor join;
    my_handle* self;

    int refs;
    jointor* message;
    jointor* stream;
    semphore_t semobj;
    struct my_buffer* mbuf;
};
#define audio_pull_ms 40

static int jointor_put(jointor* join)
{
    struct render* halp = (struct render *) join;
    int n = __sync_sub_and_fetch(&halp->refs, 1);
    if (n == 0) {
        semaphore_post(&halp->semobj);
        handle_dettach(halp->self);
        logmsg_d("sempost called\n");
    }
    return n;
}

static int jointor_get(jointor* join)
{
    struct render* halp = (struct render *) join;
    return __sync_add_and_fetch(&halp->refs, 1);
}

static int jointor_push(void* any, int cmd, void* data)
{
    if (cmd != link_from_up) {
        errno = ENOSYS;
        return -1;
    }

    jointor* jointer = (jointor *) data;
    struct render* halp = (struct render *) any;
    if (jointer->ctx.stand != stand_mixer) {
        errno = EPERM;
        return -1;
    }

    int n = -1;
    if (halp->stream != NULL) {
        errno = EBUSY;
    } else {
        jointer->ops->jointor_get(jointer);
        halp->stream = jointer;
        n = semaphore_post(&halp->semobj);
        logmsg_d("sempost called\n");
        my_assert(n == 0);
    }
    return n;
}

static int fetch_pcm_cb(void* any, audio_format* fmt, char* buf, unsigned int length)
{
    intptr_t* intp = (intptr_t *) any;
    jointor* stream = (jointor *) intp[1];
    if (__builtin_expect(buf == NULL, 0)) {
        stream->ops->jointor_put(stream);
        if (0 == __sync_sub_and_fetch(&intp[2], 1)) {
            my_free(intp);
        }
        return 0;
    }

    struct list_head head;
    INIT_LIST_HEAD(&head);
    media_buffer media;
    media.pptr_cc = &fmt->cc;
    struct my_buffer mbuf = {0};
    mbuf.ptr[0] = (char *) &media;
    mbuf.ptr[1] = buf;
    mbuf.length = length;
    list_add(&mbuf.head, &head);

    fraction frac = fmt->ops->ms_from(fmt, length, 0);
    int n = stream->ops->jointor_pull(stream->any, &head, &frac);
    if (n == -1) {
        n = 0;
    } else if (list_empty(&head)) {
        intp[0] = 0;
    }
    return n;
}

static inline intptr_t* begin_play(struct render* halp, audio_format* fmt)
{
    logmsg_d("audio begin\n");
    jointor* stream = halp->stream;
    stream->ops->jointor_get(stream);
    intptr_t* intp = NULL;

    while (0 != ((volatile int) halp->refs)) {
        if (intp == NULL) {
            intp = (intptr_t *) my_malloc(sizeof(intptr_t) * 3);
            intp[0] = 1;
            intp[1] = (intptr_t) stream;
            intp[2] = 1;
        }

        if (intp != NULL) {
            int n = audio_start_output(fetch_pcm_cb, fmt, (void *) intp);
            if (n == 0) {
                __sync_add_and_fetch(&intp[2], 1);
                return intp;
            }
        }
        usleep(10000);
    }

    stream->ops->jointor_put(stream);
    if (intp != NULL) {
        my_free(intp);
    }
    return NULL;
}

static inline void end_play(intptr_t* intp)
{
    if (0 == __sync_sub_and_fetch(&intp[2], 1)) {
        my_free(intp);
    }

    audio_stop_output();
    logmsg_d("audio end\n");
}

#define clean_stream(halp, intp) \
do { \
    halp->stream->ops->jointor_put(halp->stream); \
    halp->stream = NULL; \
    if (intp != NULL) { \
        end_play(intp); \
        intp = NULL; \
    } \
} while (0)

static void pull_loop(my_handle* handle)
{
    int n;
    struct list_head head;
    INIT_LIST_HEAD(&head);

    void* drawer = NULL;
    intptr_t* intp = NULL;

    fraction frac;
    frac.num = audio_pull_ms;
    frac.den = 1;
    struct render* halp = (struct render *) handle_get(handle);
    media_buffer* media = (media_buffer *) halp->mbuf->ptr[0];
    uintptr_t detect_pcm = (media->pptr_cc == NULL);

    uintptr_t begin_tms = now();
    while (halp != NULL) {
        if (intp == NULL) {
            list_add(&halp->mbuf->head, &head);
            fraction* need = &frac;
            n = halp->stream->ops->jointor_pull(halp->stream->any, &head, need);
            list_del_init(&head);
            fourcc** pptr_cc = media->pptr_cc;
            if (detect_pcm) {
                media->pptr_cc = NULL;
            }

            if (n == -1) {
                clean_stream(halp, intp);
                handle_put(handle);
                break;
            }

            if (n > 0) {
                intp = begin_play(halp, to_audio_format(pptr_cc));
                if (NULL == intp) {
                    clean_stream(halp, intp);
                    handle_put(handle);
                    break;
                }
            }
        } else if (intp[0] == 0) {
            end_play(intp);
            intp = NULL;
        }

        n = halp->stream->ops->jointor_pull(halp->stream->any, &head, NULL);
        if (n == -1) {
            clean_stream(halp, intp);
            handle_put(handle);
            break;
        }

        if (!list_empty(&head)) {
            if (drawer == NULL) {
                drawer = drawer_get();
            }

            if (drawer != NULL) {
                drawer_write(drawer, &head);
            }

            if (__builtin_expect(!list_empty(&head), 0)) {
                free_buffer(&head);
            }
        }

        handle_put(handle);
        uintptr_t end_tms = now();

        if (audio_pull_ms > end_tms - begin_tms) {
            usleep((useconds_t) ((audio_pull_ms + begin_tms - end_tms) * 1000));
            begin_tms = now();
        } else {
            begin_tms = end_tms;
        }

        halp = (struct render *) handle_get(handle);
    }

    if (intp != NULL) {
        end_play(intp);
    }

    if (drawer != NULL) {
        drawer_put(drawer);
    }
}

static void* pull_thread(void* arg)
{
    mark("muxer puller thread start");
    my_handle* handle = (my_handle *) arg;
    struct render* halp = (struct render *) handle_get(handle);
    while (halp != NULL) {
        if (-1 == semaphore_wait(&halp->semobj)) {
            continue;
        }

        if (halp->refs == 0) {
            handle_put(handle);
            break;
        }

        logmsg_d("render begin\n");
        pull_loop(handle);
        logmsg_d("render end\n");
    }

    handle_dettach(handle);
    logmsg_d("render pull thread exit\n");
    return NULL;
}

static void render_destroy(void* addr)
{
    struct render* halp = (struct render *) addr;
    if (halp->stream != NULL) {
        halp->stream->ops->jointor_put(halp->stream);
    }

    semaphore_destroy(&halp->semobj);
    halp->mbuf->mop->free(halp->mbuf);
    my_free(addr);
}

jointor* render_create(audio_format* fmt)
{
    errno = EINVAL;
    if (fmt != NULL && audio_raw_format(fmt->pcm) != fmt) {
        return NULL;
    }

    errno = ENOMEM;
    struct render* halp = (struct render *) my_malloc(sizeof(*halp));
    if (halp == NULL) {
        return NULL;
    }

    static struct jointor_ops ops = {
        jointor_get,
        jointor_put,
        NULL,
        jointor_push
    };
    halp->join.ctx.stand = stand_join;
    halp->join.ctx.extra = flag_realtime;
    halp->join.ctx.identy = (intptr_t) &halp;
    halp->join.any = halp;
    halp->join.ops = &ops;
    INIT_LIST_HEAD(&halp->join.entry[0]);
    INIT_LIST_HEAD(&halp->join.entry[1]);

    halp->message = NULL;
    halp->stream = NULL;

    uintptr_t bytes = 4;
    fourcc** cc = NULL;
    if (fmt != NULL) {
        fraction frac;
        frac = pcm_bytes_from_ms(fmt->pcm, audio_pull_ms);
        bytes = frac.num / frac.den;
        cc = &fmt->cc;
    }

    halp->mbuf = mbuf_alloc_2(sizeof(media_buffer) + bytes);
    if (halp->mbuf == NULL) {
        my_free(halp);
        return NULL;
    }
    halp->mbuf->ptr[1] += sizeof(media_buffer);
    halp->mbuf->length -= sizeof(media_buffer);

    media_buffer* media = (media_buffer *) halp->mbuf->ptr[0];
    media->pptr_cc = cc;
    if (-1 == semaphore_init(&halp->semobj, 0)) {
        halp->mbuf->mop->free(halp->mbuf);
        my_free(halp);
        return NULL;
    }

    halp->self = handle_attach(halp, render_destroy);
    if (halp->self == NULL) {
        semaphore_destroy(&halp->semobj);
        halp->mbuf->mop->free(halp->mbuf);
        my_free(halp);
        return NULL;
    }

    pthread_t tid;
    handle_clone(halp->self);
    int n = pthread_create(&tid, NULL, pull_thread, halp->self);
    if (n != 0) {
        handle_release(halp->self);
        handle_dettach(halp->self);
        errno = -n;
        return NULL;
    }
    pthread_detach(tid);
    halp->refs = 1;
    return &halp->join;
}

int render_link_upstream(jointor* self, jointor* upstream)
{
    if (upstream->ops->jointor_push == NULL || self->ops->jointor_push == NULL) {
        errno = EINVAL;
        return -1;
    }

    struct render* halp = (struct render *) self;
    int32_t n = __sync_bool_compare_and_swap((void **) &halp->message, NULL, upstream);
    if (n == 0) {
        errno = EBUSY;
        return -1;
    }

    n = upstream->ops->jointor_push(upstream->any, link_from_down, self);
    if (n == -1) {
        halp->message = NULL;
    } else {
        upstream->ops->jointor_get(upstream);
    }
    return n;
}

void render_unlink_upstream(jointor* self, jointor* upstream)
{
    struct render* halp = (struct render *) self;
    if (halp->message == upstream) {
        halp->message = NULL;
        upstream->ops->jointor_push(upstream->any, unlink_from_down, self);
        upstream->ops->jointor_put(upstream);
    }
}
