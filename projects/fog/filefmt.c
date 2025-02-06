
#include "filefmt.h"
#include "fileit.h"
#include "file_jointor.h"
#include "playback.h"
#include "mem.h"
#include "my_handle.h"
#include <string.h>

typedef struct {
    jointor* file;
    jointor* playback;
    fcontext_t* fctx;
    filefmt_t* ffmt;
    void* rptr;
} filefmt_context_t;

static intptr_t ftype_from_uri(const char* uri)
{
    if (uri[0] == 'r' && uri[1] == 't' &&
        uri[2] == 'm' && uri[3] == 'p' && uri[4] == ':')
    {
        return ftype_rtmp;
    }

    const char* ext = strrchr(uri, '.');
    if (ext == NULL) {
        return -1;
    }
    ext += 1;

    if (0 == strcmp(ext, "m3u8")) {
        return ftype_m3u8;
    }

    if (0 == strcmp(ext, "mp4")) {
        return ftype_mp4;
    }

    return -1;
}

static void filefmt_context_free(void* addr)
{
    filefmt_context_t* fmctx = (filefmt_context_t *) addr;
    if (fmctx->playback != NULL) {
        playback_close(fmctx->playback, fmctx->file);
    }

    if (fmctx->file != NULL) {
        fmctx->file->ops->jointor_put(fmctx->file);
    }

    if (fmctx->fctx != NULL) {
        fmctx->ffmt->ops->fcontext_put(fmctx->rptr);
    }

    if (fmctx->rptr != NULL) {
        fmctx->ffmt->ops->close(fmctx->rptr);
    }
    my_free(addr);
}

void* filefmt_open(const char* uri, uint32_t type, callback_t* cb1, file_mode_t* mode)
{
    errno = ENOSYS;
    my_assert(uri != NULL);

    intptr_t ftype = type;
    if (ftype == ftype_auto) {
        ftype = ftype_from_uri(uri);
    }

    if (ftype == -1) {
        return NULL;
    }

    filefmt_t* file = fiflefmt_get(ftype);
    if (file == NULL) {
        return NULL;
    }

    filefmt_context_t* fmctx = (filefmt_context_t *) my_malloc(sizeof(filefmt_context_t));
    if (fmctx == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    memset(fmctx, 0, sizeof(filefmt_context_t));
    fmctx->ffmt = file;

    my_handle* handle = handle_attach(fmctx, filefmt_context_free);
    if (handle == NULL) {
        my_free(fmctx);
        return NULL;
    }

    fmctx->rptr = file->ops->open(uri, cb1, mode);
    if (fmctx->rptr == NULL) {
        handle_dettach(handle);
        return NULL;
    }

    fmctx->file = file->ops->jointor_get(fmctx->rptr);
    if (fmctx->file == NULL) {
        handle_dettach(handle);
        return NULL;
    }

    fmctx->playback = playback_open(NULL, fmctx->file);
    if (fmctx->playback == NULL) {
        handle_dettach(handle);
        handle = NULL;
    }
    return handle;
}

fcontext_t* fcontext_get(void* ctx)
{
    errno = EBADF;
    my_handle* handle = (my_handle *) ctx;
    filefmt_context_t* fmctx = (filefmt_context_t *) handle_get(handle);
    if (fmctx == NULL) {
        return NULL;
    }

    if (fmctx->fctx == NULL) {
        fmctx->fctx = fmctx->ffmt->ops->fcontext_get(fmctx->rptr);
    }

    fcontext_t* fctx = fmctx->ffmt->ops->fcontext_get(fmctx->rptr);
    if (fctx != NULL) {
        fmctx->ffmt->ops->fcontext_put(fmctx->rptr);
    }
    handle_put(handle);
    return fctx;
}

intptr_t filefmt_seek(void* ctx, uintptr_t seekto)
{
    errno = EBADF;
    my_handle* handle = (my_handle *) ctx;
    filefmt_context_t* fmctx = (filefmt_context_t *) handle_get(handle);
    if (fmctx == NULL) {
        return -1;
    }

    intptr_t n = fmctx->ffmt->ops->seek(fmctx->rptr, seekto);
    handle_put(handle);
    return n;
}

void filefmt_close(void* ctx)
{
    handle_dettach((my_handle *) ctx);
}
