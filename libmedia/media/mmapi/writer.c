
#include "writer.h"
#include "my_handle.h"
#include "captofile.h"
#include "playback.h"

typedef struct {
    media_iden_t iden;
    uint32_t mask;
    mfio_writer_t* mfio;
    void* mfio_s0;
    void* mfio_s1;
    void* item0;
    void* item1;
} mfio_entry_t;

void* writer_open(mfio_writer_t* mfio, const char* filename, media_buffer* audio, media_buffer* video)
{
    return mfio->open(filename, audio, video);
}

static void unhook(mfio_entry_t* mfent)
{
    if (media_id_eqal(mfent->iden, media_id_self)) {
        if (mfent->item0 != NULL) {
            hook_control(NULL, 0, mfent->item0, 0);
        }

        if (mfent->item1 != NULL) {
            hook_control(NULL, 0, mfent->item1, 0);
        }
    } else {
        playback_hook_delete(mfent->item0);
    }

    mfent->mask = 0;
    mfent->item0 = mfent->item1 = NULL;
}

static void stream_free(void* uptr)
{
    mfio_entry_t* mfent = (mfio_entry_t *) uptr;
    unhook(mfent);

    if (mfent->mfio_s0 != NULL) {
        mfent->mfio->delete_stream(mfent->mfio_s0);
    }

    if (mfent->mfio_s1 != NULL) {
        mfent->mfio->delete_stream(mfent->mfio_s1);
    }
    heap_free(mfent);
}

static void stream_dettach(void* mptr)
{
    handle_dettach((my_handle *) mptr);
}

static intptr_t stream_notify(void* mptr, intptr_t identy, void* any)
{
    my_handle* handle = (my_handle *) mptr;
    mfio_entry_t* mfent = (mfio_entry_t *) handle_get(handle);
    if (mfent == NULL) {
        return -1;
    }

    codectx_t* ctx = (codectx_t *) any;
    struct my_buffer* mbuf = ctx->mbuf;
    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    uint32_t type = media_type((*media->pptr_cc));

    if (media_id_eqal(mfent->iden, media->iden)) {
        if (audio_type == (mfent->mask & type)) {
            mfent->mfio->write(mfent->mfio_s0, mbuf);
        } else if (video_type == (mfent->mask & type)) {
            mfent->mfio->write(mfent->mfio_s1, mbuf);
        }
    }
    handle_put(handle);
    return -1;
}

static intptr_t mfio_stream_get(mfio_entry_t* mfent, void* writer, uint32_t mask)
{
    if (mask & audio_type) {
        mfent->mfio_s0 = mfent->mfio->add_stream(writer);
        if (mfent->mfio_s0 == NULL) {
            return -1;
        }
    }

    if (mask & video_type) {
        mfent->mfio_s1 = mfent->mfio->add_stream(writer);
        if (mfent->mfio_s1 == NULL) {
            return -1;
        }
    }
    return 0;
}

void* add_stream(mfio_writer_t* mfio, void* writer, media_iden_t iden, uint32_t mask, intptr_t sync)
{
    errno = ENOMEM;
    mfio_entry_t* mfent = heap_alloc(mfent);
    if (mfent == NULL) {
        return NULL;
    }
    mfent->mfio = NULL;
    mfent->mfio_s0 = mfent->mfio_s1 = NULL;
    mfent->item0 = mfent->item1 = NULL;

    my_handle* handle = handle_attach(mfent, stream_free);
    if (handle == NULL) {
        heap_free(mfent);
        return NULL;
    }

    errno = EFAILED;
    mfent->mfio = mfio;
    mfent->iden = iden;
    mfent->mask = 0;
    if (-1 == mfio_stream_get(mfent, writer, mask)) {
        handle_dettach(handle);
        return NULL;
    }

    callback_t hook;
    hook.uptr = handle;
    hook.notify = stream_notify;
    hook.uptr_put = stream_dettach;

    intptr_t n = 0;
    mfent = (mfio_entry_t *) handle_get(handle);
    if (media_id_eqal(iden, media_id_self)) {
        if (mask & audio_type) {
            handle_clone(handle);
            mfent->item0 = hook_control(&hook, 0, NULL, sync);
            if (mfent->item0 == NULL) {
                handle_release(handle);
                n = -1;
            }
        }

        if ((n == 0) && (mask & video_type)) {
            handle_clone(handle);
            mfent->item1 = hook_control(&hook, 1, NULL, sync);
            if (mfent->item1 == NULL) {
                handle_release(handle);
                n = -1;
            }
        }
    } else {
        handle_clone(handle);
        mfent->item0 = playback_hook_add(&hook);
        if (mfent->item0 == NULL) {
            handle_release(handle);
            n = -1;
        }
    }

    if (n == -1) {
        handle_put(handle);
        handle_dettach(handle);
        handle = NULL;
    } else {
        mfent->mask = mask;
        handle_put(handle);
    }
    return handle;
}

void delete_stream(void* mptr)
{
    my_handle* handle = (my_handle *) mptr;
    handle_dettach(handle);
}

void writer_close(mfio_writer_t* mfio, void* writer)
{
    mfio->close(writer);
}
