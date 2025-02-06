
#include "my_errno.h"
#include "lock.h"
#include "my_handle.h"
#include "now.h"
#include "mbuf.h"
#include "media_buffer.h"

#include "audio.h"
#include <stddef.h>
#include <assert.h>
#include <process.h>

#define device_buffer_ms 200
#define DIRECTSOUND_VERSION 0x0800
#include <mmsystem.h>
#include <dsound.h>
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "winmm.lib")

typedef struct {
    LPDIRECTSOUND8 dsound;
    LPDIRECTSOUNDBUFFER8 dbuf;
    audio_format* fmt[2];

    DWORD position;
    fraction end;
    unsigned int buffer_bytes;
    unsigned int align;

    void* ctx;
    fetch_pcm fetch_fptr;

    struct my_buffer* mbuf_pcm;
    char pcm_buffer[256 * 1024];
} out_context;

typedef struct {
    LPDIRECTSOUNDCAPTURE8 dsound;
    LPDIRECTSOUNDCAPTUREBUFFER8 dbuf;
    audio_format* fmt[2];

    DWORD position;
    unsigned int buffer_bytes;
    unsigned int ms_bytes;
    unsigned int align;
    uint64_t total_bytes;

    void* ctx;
    fetch_pcm fetch_fptr;
} in_context;

typedef struct {
    out_context render;
    in_context capture;

    int lck;
    my_handle* self;
    int zombie;
} dsound_context;

static dsound_context sound_obj = {
    {NULL, NULL, {NULL, NULL}, 0, {0, 1}, 0, 0, NULL, NULL, NULL, {0}},
    {NULL, NULL, {NULL, NULL}, 0, 0, 0, 0, 0, NULL, NULL},
    0, NULL, 0
};

int audio_initialize(audio_stat_fptr fptr)
{
    (void) fptr;
    return 0;
}

void audio_setformat(audio_format* output, audio_format* input)
{
    if (output != NULL) {
        sound_obj.render.fmt[1] = output;
    }

    if (input != NULL) {
        sound_obj.capture.fmt[1] = input;
    }
}

static int rendering_start(out_context* render, audio_format* fmt)
{
    HRESULT hr = DirectSoundCreate8(NULL, &render->dsound, NULL);
    if (FAILED(hr)) {
        return -1;
    }

    int n = 0;
    HWND hWnd = GetDesktopWindow();
    hr = render->dsound->SetCooperativeLevel(hWnd, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        render->dsound->Release();
        render->dsound = NULL;
        return -1;
    }

    DSBUFFERDESC desc = {0};
    desc.dwSize = sizeof(DSBUFFERDESC);
    desc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_STICKYFOCUS |
                   DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLVOLUME;

    WAVEFORMATEX wfx;
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.wBitsPerSample = fmt->pcm->sambits;
    wfx.nChannels = fmt->pcm->channel;
    wfx.nSamplesPerSec = fmt->pcm->samrate;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) >> 3;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    desc.dwBufferBytes = wfx.nAvgBytesPerSec * device_buffer_ms / 1000;
    desc.dwBufferBytes &= ~(wfx.nBlockAlign - 1);
    desc.lpwfxFormat = &wfx;

    render->buffer_bytes = desc.dwBufferBytes;
    render->align = wfx.nBlockAlign;
    fraction_zero(&render->end);

    LPDIRECTSOUNDBUFFER second_buffer;
    hr = render->dsound->CreateSoundBuffer(&desc, &second_buffer, 0);
    if (FAILED(hr)) {
        render->dsound->Release();
        render->dsound = NULL;
        return -1;
    }

    hr = second_buffer->QueryInterface(IID_IDirectSoundBuffer8, (LPVOID *) &render->dbuf);
    second_buffer->Release();

    if (FAILED(hr)) {
        render->dsound->Release();
        render->dsound = NULL;
        render->dbuf = NULL;
        return -1;
	}

LABEL:
    hr = render->dbuf->Play(0, 0, DSBPLAY_LOOPING);
    if(FAILED(hr)){
        if(hr == DSERR_BUFFERLOST){
            render->dbuf->Restore();
            goto LABEL;
        }
        render->dbuf->Release();
        render->dsound->Release();
        render->dsound = NULL;
        render->dbuf = NULL;
        return -1;
    }
    return 0;
}

static void render_clean(out_context* render)
{
    if (render->fmt[0] == NULL) {
        return;
    }

    render->dbuf->Stop();
    render->dbuf->Release();
    render->dsound->Release();
    render->dbuf = NULL;
    render->dsound = NULL;
    render->fmt[0] = NULL;

    render->position = 0;
    fraction_zero(&render->end);
}

static unsigned int calc_write_pos_and_length(out_context* render, unsigned int tick, DWORD* pos)
{
    DWORD pos1, pos2, end = render->end.num / render->end.den;
LABEL:
    HRESULT hr = render->dbuf->GetCurrentPosition(&pos1, &pos2);
    if (FAILED(hr)) {
        if (hr == DSERR_BUFFERLOST) {
            render->dbuf->Restore();
            goto LABEL;
        } else {
            render_clean(render);
            return 0;
        }
    }

    if (pos1 == pos2) {
        return 0;
    }

    unsigned int need_write;
    if (pos1 > pos2) {
        if (tick < end) {
            if (render->position > pos1 || render->position < pos2) {
                need_write = pos1 - pos2;
            } else {
                need_write = pos1 - render->position;
                pos2 = render->position;
            }
        } else {
            need_write = pos1 - pos2;
        }
    } else {
        if (tick < end) {
            if (render->position > pos2) {
                need_write = render->buffer_bytes - (render->position - pos1);
                pos2 = render->position;
            } else if (render->position >= pos1) {
                need_write = render->buffer_bytes - (pos2 - pos1);
            } else {
                need_write = pos1 - render->position;
                pos2 = render->position;
            }
        } else {
            need_write = render->buffer_bytes - (pos2 - pos1);
        }
    }

    *pos = pos2;
    return need_write;
}

static uint32_t sync_render(out_context* render, fetch_pcm fetch_fptr)
{
    audio_format* fmt = render->fmt[1];
    if (fetch_fptr == NULL) {
        fmt = NULL;
    }

    int n = 0;
    if (render->fmt[0] != fmt) {
        render_clean(render);

        if (fmt == NULL) {
            n = -1;
        } else {
            n = rendering_start(render, fmt);
            if (n == 0) {
                render->fmt[0] = fmt;
            }
        }
    }

    if (fmt == NULL) {
        n = (uint32_t) -1;
    } else if (n == -1) {
        n = 50;
    }
    return (uint32_t) n;
}

static uint32_t pcm_write_out(out_context* render, unsigned int tick)
{
    fetch_pcm fetch_fptr = render->fetch_fptr;
    uint32_t n = sync_render(render, fetch_fptr);
    if (n != 0) {
        return n;
    }

    DWORD pos;
    unsigned int need_write = calc_write_pos_and_length(render, tick, &pos);
    need_write = need_write & (~(render->align - 1));
    if (need_write == 0) {
        return 0;
    }
    render->mbuf_pcm->length = need_write;

    uint32_t bytes = 0;
    if (need_write > (render->buffer_bytes >> 2)) {
        bytes = fetch_fptr(render->ctx, render->mbuf_pcm->ptr[1], need_write);
    }

    render->mbuf_pcm->length = need_write;
    if (bytes < need_write) {
        memset(render->mbuf_pcm->ptr[1] + bytes, 0, need_write - bytes);
    }

    fraction silence;
    char *addr1, *addr2;
    DWORD bytes1, bytes2;

LABEL:
    HRESULT hr = render->dbuf->Lock(pos, need_write, (void **) &addr1, &bytes1, (void **) &addr2, &bytes2, 0);
    if (FAILED(hr)) {
        if (hr == DSERR_BUFFERLOST) {
            render->dbuf->Restore();
            goto LABEL;
        } else {
            render_clean(render);
            return 0;
        }
    }

    memcpy(addr1, render->mbuf_pcm->ptr[1], bytes1);
    if (bytes2 > 0) {
        memcpy(addr2, render->mbuf_pcm->ptr[1] + bytes1, bytes2);
    }
    render->dbuf->Unlock((void *) addr1, bytes1, (void *) addr2, bytes2);

    silence.num = (need_write - bytes) * device_buffer_ms;
    silence.den = render->buffer_bytes;
    render->end.num = device_buffer_ms;
    render->end.den = 1;
    fraction_sub(&render->end, &silence);
    render->position = (pos + bytes) % render->buffer_bytes;

    return (render->end.num / render->end.den);
}

static int capture_start(in_context* capture, audio_format* fmt)
{
    HRESULT hr = DirectSoundCaptureCreate8(NULL, &capture->dsound, NULL);
    if (FAILED(hr)) {
        return -1;
    }

    WAVEFORMATEX wfx;
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.wBitsPerSample = fmt->pcm->sambits;
    wfx.nChannels = fmt->pcm->channel;
    wfx.nSamplesPerSec = fmt->pcm->samrate;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) >> 3;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    DSCBUFFERDESC desc = {0};
    desc.dwSize = sizeof(DSCBUFFERDESC);
    desc.dwFlags = 0;
	desc.dwBufferBytes = wfx.nAvgBytesPerSec;
    desc.lpwfxFormat = &wfx;
    capture->buffer_bytes = wfx.nAvgBytesPerSec;
    capture->ms_bytes = capture->buffer_bytes / 1000;
    capture->align = wfx.nBlockAlign;

    LPDIRECTSOUNDCAPTUREBUFFER second_buffer;
    hr = capture->dsound->CreateCaptureBuffer(&desc, &second_buffer, 0);
    if (FAILED(hr)) {
        capture->dsound->Release();
        capture->dsound = NULL;
        return -1;
    }

    hr = second_buffer->QueryInterface(IID_IDirectSoundCaptureBuffer8, (LPVOID *) &capture->dbuf);
    second_buffer->Release();
    if (FAILED(hr)) {
        capture->dsound->Release();
        capture->dsound = NULL;
        capture->dbuf = NULL;
        return -1;
	}

    hr = capture->dbuf->Start(DSCBSTART_LOOPING);
    if(FAILED(hr)){
        capture->dbuf->Release();
        capture->dsound->Release();
        capture->dsound = NULL;
        capture->dbuf = NULL;
        return -1;
    }
    return 0;
}

static void capture_clean(in_context* capture)
{
    if (capture->fmt[0] == NULL) {
        return;
    }

    capture->dbuf->Stop();
    capture->dbuf->Release();
    capture->dsound->Release();
    capture->dbuf = NULL;
    capture->dsound = NULL;
    capture->fmt[0] = NULL;
    capture->position = 0;
}

static uint32_t sync_capture(in_context* capture, fetch_pcm fetch_fptr, audio_format* fmt)
{
    if (fetch_fptr == NULL) {
        fmt = NULL;
    }

    int n = 0;
    if (capture->fmt[0] != fmt) {
        capture_clean(capture);

        if (fmt == NULL) {
            n = -1;
        } else {
            n = capture_start(capture, fmt);
            if (n == 0) {
                capture->fmt[0] = fmt;
            }
        }
    }

    if (fmt == NULL) {
        n = (uint32_t) -1;
    } else if (n == -1) {
        n = 50;
    }
    return (uint32_t) n;
}

static uint32_t pcm_read_in(in_context* capture)
{
    audio_format* fmt = capture->fmt[1];
    fetch_pcm fetch_fptr = capture->fetch_fptr;
    uint32_t n = sync_capture(capture, fetch_fptr, fmt);
    if (n != 0) {
        return n;
    }

    DWORD pos1, pos2, ready_bytes;
    HRESULT hr = capture->dbuf->GetCurrentPosition(&pos1, &pos2);
    if (FAILED(hr)) {
        capture_clean(capture);
        return 0;
    }

    if (pos1 == pos2 || capture->position == pos2) {
        return 20;
    }

    if (pos1 > pos2) {
        if (capture->position > pos2 && capture->position <= pos1) {
            capture->position = pos1 + 1;
            capture->position %= capture->buffer_bytes;
        }

        if (capture->position > pos2) {
            ready_bytes = capture->buffer_bytes - (capture->position - pos2);
        } else {
            ready_bytes = pos2 - capture->position;
        }
    } else {
        if (capture->position > pos2 || capture->position <= pos1) {
            capture->position = pos1 + 1;
        }
        ready_bytes = pos2 - capture->position;
    }

    ready_bytes = ready_bytes & (~(capture->align - 1));
    if (ready_bytes == 0) {
        return 20;
    }

    char *addr1, *addr2;
    DWORD bytes1, bytes2;
    hr = capture->dbuf->Lock(capture->position, ready_bytes, (void **) &addr1, &bytes1, (void **) &addr2, &bytes2, 0);
    if (FAILED(hr)) {
        capture_clean(capture);
        return 0;
    }
    capture->position += ready_bytes;
    capture->position %= capture->buffer_bytes;

    struct my_buffer* mbuf = mbuf_alloc_2(sizeof(media_buffer) + ready_bytes);
    if (mbuf != NULL) {
        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        mbuf->ptr[1] = mbuf->ptr[0] + sizeof(media_buffer);
        mbuf->length = ready_bytes;
        media->pptr_cc = &fmt->cc;
        media->id = media_id_uniq();
        media->pts = (capture->total_bytes / capture->ms_bytes);
        capture->total_bytes += ready_bytes;

        memcpy(mbuf->ptr[1], addr1, bytes1);
        if (bytes2 > 0) {
            memcpy(mbuf->ptr[1] + bytes1, addr2, bytes2);
        }
        capture->dbuf->Unlock((void *) addr1, bytes1, (void *) addr2, bytes2);
        fetch_fptr(capture->ctx, (char *) mbuf, mbuf->length);
    } else {
        capture->dbuf->Unlock((void *) addr1, bytes1, (void *) addr2, bytes2);
    }
    return 40;
}

static unsigned __stdcall looper(void* any)
{
    my_handle* handle = (my_handle *) any;
    unsigned int old_tick = 0;

    while (1) {
        dsound_context* ctx = (dsound_context *) handle_get(handle);
        if (ctx == NULL) {
            break;
        }

        unsigned int interval = 50;
        unsigned int tick = now();
        uint32_t ms1 = pcm_write_out(&ctx->render, tick - old_tick);
        uint32_t ms2 = pcm_read_in(&ctx->capture);
        handle_put(handle);
        old_tick = tick;

        // assume pcm_write_out take 5 ms
        // because fetch_fptr will do resample things
        if (ms1 > 5) {
            ms1 -= 5;
        }

        // assume pcm_read_in take 2 ms
        if (ms2 > 2) {
            ms2 -= 2;
        }

        interval = min(interval, ms1);
        interval = min(interval, ms2);
        // should not too small, 10 ms may be proper value for decode
        if (interval < 10) {
            interval = 10;
        }
        tick += interval;

        unsigned int cur = now();
        if (cur < tick) {
            Sleep(tick - cur);
        }
    }

    handle_release(handle);
    return 0;
}

static void detach_dsound(void* addr)
{
    dsound_context* ctx = (dsound_context *) addr;
    sync_render(&ctx->render, NULL);
    sync_capture(&ctx->capture, NULL, NULL);

    ctx->self = NULL;
    wmb();
    ctx->zombie = 0;
}

static int create_looper()
{
    sound_obj.self = handle_attach(&sound_obj, detach_dsound);
    if (sound_obj.self == NULL) {
        errno = ENOMEM;
        return -1;
    }

    HANDLE thread = (HANDLE) _beginthreadex(NULL, 4096, looper, (void *) sound_obj.self, 0, NULL);
    if (thread == NULL) {
        handle_dettach(sound_obj.self);
        return -1;
    }

    CloseHandle(thread);
    return 0;
}

int audio_start_output(fetch_pcm fptr, void* ctx)
{
    errno = EFAILED;

    lock(&sound_obj.lck);
    if (sound_obj.render.mbuf_pcm == NULL) {
        sound_obj.render.mbuf_pcm = mbuf_alloc_6(sound_obj.render.pcm_buffer, sizeof(sound_obj.render.pcm_buffer));
    }

    if (sound_obj.render.fetch_fptr != NULL) {
        unlock(&sound_obj.lck);
        errno = EEXIST;
        return -1;
    }

    if (sound_obj.zombie == 1) {
        unlock(&sound_obj.lck);
        errno = EAGAIN;
        return -1;
    }

    if (sound_obj.self == NULL) {
        if (-1 == create_looper()) {
            unlock(&sound_obj.lck);
            return -1;
        }
    }

    handle_clone(sound_obj.self);
    sound_obj.render.fetch_fptr = fptr;
    sound_obj.render.ctx = ctx;
    unlock(&sound_obj.lck);
    return 0;
}

int audio_start_input(fetch_pcm fptr, void* ctx, enum aec_t aec)
{
    errno = EFAILED;
    lock(&sound_obj.lck);

    if (sound_obj.capture.fetch_fptr != NULL) {
        unlock(&sound_obj.lck);
        errno = EEXIST;
        return -1;
    }

    if (sound_obj.zombie == 1) {
        unlock(&sound_obj.lck);
        errno = EAGAIN;
        return -1;
    }

    rmb();

    if (sound_obj.self == NULL) {
        if (-1 == create_looper()) {
            unlock(&sound_obj.lck);
            return -1;
        }
    }

    handle_clone(sound_obj.self);
    sound_obj.capture.fetch_fptr = fptr;
    sound_obj.capture.ctx = ctx;
    unlock(&sound_obj.lck);
    return 0;
}

static void release_handle(my_handle* handle)
{
    if (sound_obj.capture.fetch_fptr == NULL &&
        sound_obj.render.fetch_fptr == NULL)
    {
        sound_obj.zombie = 1;
        handle_dettach(handle);
    } else {
        handle_release(handle);
    }
}

int audio_stop_output()
{
    lock(&sound_obj.lck);
    if (sound_obj.render.fetch_fptr != NULL) {
        sound_obj.render.fetch_fptr = NULL;
        release_handle(sound_obj.self);
    }
    unlock(&sound_obj.lck);

    return 0;
}

int audio_stop_input()
{
    lock(&sound_obj.lck);
    if (sound_obj.capture.fetch_fptr != NULL) {
        sound_obj.capture.fetch_fptr = NULL;
        release_handle(sound_obj.self);
    }
    unlock(&sound_obj.lck);

    return 0;
}

int audio_aec_ctl(enum aec_t aec, uint32_t vol1, uint32_t vol0)
{
    return -1;
}
