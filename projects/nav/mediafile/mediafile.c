
#include "mediafile.h"
#include "mem.h"
#include "fmt.h"
#include "mbuf.h"
#include <stdio.h>
#include <string.h>
#include "my_errno.h"

#ifndef _MSC_VER
#include <arpa/inet.h>
#endif

struct media_file_context {
    FILE* file;
    int end;
    int mode;
    struct file_header header;

    struct {
        int pos, trunk;
        media_buffer media;
    } rd;
};

void* media_file_open(const char* fullpath, int read0_write1)
{
    struct media_file_context* ctx = (struct media_file_context *) my_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    ctx->file = fopen(fullpath, read0_write1 ? "wb" : "rb");
    if (ctx->file == NULL) {
        my_free(ctx);
        return NULL;
    }
    ctx->mode = read0_write1;

    if (read0_write1 != 0) {
        strncpy(ctx->header.magic, "txla", 4);
        ctx->header.be_version = htonl(1);
        ctx->header.be_bytes = 0;
        ctx->header.be_file_ms = 0;
        ctx->header.be_codec = 0;
        ctx->header.be_frame_ms = 0;
        ctx->header.be_samrate = 0;
        ctx->header.be_chans = 0;
        ctx->header.be_crc = 0;
        ctx->end = 0;
        return ctx;
    }

    long offset = sizeof(struct file_header);
    int n = fseek(ctx->file, -offset, SEEK_END);
    if (n == -1) {
        goto LABEL;
    }

    ctx->end = (int) ftell(ctx->file);
    n = (int) fread(&ctx->header, 1, offset, ctx->file);
    if (n != offset) {
         goto LABEL;
    }

    n = fseek(ctx->file, 0, SEEK_SET);
    if (n != 0) {
        goto LABEL;
    }

    if (0 != strcmp(ctx->header.magic, "txla")) {
        errno = EINVAL;
        goto LABEL;
    }

    ctx->header.be_version = ntohl(ctx->header.be_version);
    ctx->header.be_bytes = ntohl(ctx->header.be_bytes);
    ctx->header.be_bytes -= offset;
    ctx->header.be_file_ms = ntohl(ctx->header.be_file_ms);
    ctx->header.be_codec = ntohl(ctx->header.be_codec);
    ctx->header.be_frame_ms = ntohl(ctx->header.be_frame_ms);
    ctx->header.be_samrate = ntohl(ctx->header.be_samrate);
    ctx->header.be_chans = ntohl(ctx->header.be_chans);
    ctx->header.be_crc = ntohl(ctx->header.be_crc);

    ctx->rd.pos = 0;
    ctx->rd.trunk = 0;
    ctx->rd.media.pts = 0;
    ctx->rd.media.iden = media_id_uniq(0);
    ctx->rd.media.seq = 0;
    ctx->rd.media.pptr_cc = fourcc_get(ctx->header.be_codec,
                                       ctx->header.be_samrate, ctx->header.be_chans);
    return ctx;
LABEL:
    fclose(ctx->file);
    my_free(ctx);
    return NULL;
}

struct file_header* media_file_header(void* any)
{
    struct media_file_context* ctx = (struct media_file_context *) any;
    return &ctx->header;
}

int memdia_file_write(void* any, media_buffer* media, const char* buffer, int length)
{
    struct media_file_context* ctx = (struct media_file_context *) any;
    if (ctx->mode != 1) {
        errno = EINVAL;
        return -1;
    }

    if (ctx->header.be_bytes == 0) {
        audio_format* fmt = to_audio_format(media->pptr_cc);
        ctx->header.be_codec = htonl(fmt->cc->code);
        ctx->header.be_samrate = htonl(fmt->pcm->samrate);
        ctx->header.be_chans = htonl(fmt->pcm->channel);
        fraction frac = fmt->ops->ms_from(fmt, 1, 1);
        ctx->header.be_frame_ms = frac.num / frac.den;
    }

    ctx->header.be_bytes += length + 2;
    short len = htons((short) length);
    int n = (int) fwrite(&len, 1, 2, ctx->file);
    if (n != 2) {
        return -1;
    }

    n = (int) fwrite(buffer, 1, length, ctx->file);
    if (n != length) {
        return -1;
    }

    ctx->header.be_file_ms += ctx->header.be_frame_ms;
    return 0;
}

int media_file_tell(void* any)
{
    struct media_file_context* ctx = (struct media_file_context *) any;
    if (ctx->mode == 1) {
        errno = EINVAL;
        return -1;
    }
    return ctx->rd.pos;
}

static int read_meta(struct media_file_context* ctx, struct list_head* headp)
{
    struct my_buffer* mbuf = mbuf_alloc_2(sizeof(media_buffer));
    if (mbuf == NULL) {
        errno = ENOMEM;
        return -1;
    }

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    mbuf->ptr[1] = mbuf->ptr[0] + sizeof(media_buffer);
    mbuf->length = 0;

    *media = ctx->rd.media;
    ctx->rd.media.seq++;
    ctx->rd.media.pts += 0;
    list_add_tail(&mbuf->head, headp);
    return 0;
}

int memdia_file_read(void* any, struct list_head* headp, const fraction* f)
{
    struct media_file_context* ctx = (struct media_file_context *) any;
    if (ctx->mode == 1) {
        errno = EINVAL;
        return -1;
    }

    if (f == NULL) {
        return 0;
    }

    if (f->num == 0) {
        return read_meta(ctx, headp);
    }

    errno = 0;
    uint32_t frames = f->num / (ctx->header.be_frame_ms * f->den);
    if (f->num > frames * ctx->header.be_frame_ms * f->den) {
        frames += 1;
    }

    int n, nr = 0;
    while (frames > 0) {
        if (ctx->rd.trunk == 0) {
            if (ctx->end - ctx->rd.pos < 2) {
                ctx->rd.pos = ctx->end;
                break;
            }

            short bytes;
            n = (int) fread(&bytes, 1, 2, ctx->file);
            if (n != 2) {
                ctx->rd.pos = -1;
                break;
            }
            bytes = ntohs(bytes);

            ctx->rd.pos += 2;
            if (ctx->end - ctx->rd.pos < bytes) {
                ctx->rd.pos = ctx->end;
                break;
            }

            ctx->rd.trunk = bytes;
            // the fucking existing android code force me to do this
            if (ctx->rd.trunk == 0) {
                continue;
            }
        }

        struct my_buffer* mbuf = mbuf_alloc_2(sizeof(media_buffer) + ctx->rd.trunk);
        if (mbuf == NULL) {
            errno = ENOMEM;
            break;
        }

        media_buffer* media = (media_buffer *) mbuf->ptr[0];
        mbuf->ptr[1] = mbuf->ptr[0] + sizeof(media_buffer);
        mbuf->length -= (uint32_t) sizeof(media_buffer);

        my_assert(feof(ctx->file) == 0);
        n = (int) fread(mbuf->ptr[1], 1, mbuf->length, ctx->file);
        if (n != mbuf->length) {            
            mbuf->mop->free(mbuf);
            ctx->rd.pos = -1;
            break;
        }

        ctx->rd.trunk = 0;
        ctx->rd.pos += mbuf->length;
        nr += mbuf->length;
        frames--;

        *media = ctx->rd.media;
        ctx->rd.media.seq++;
        ctx->rd.media.pts += ctx->header.be_frame_ms;
        list_add_tail(&mbuf->head, headp);
    }

    if (nr > 0) {
        return nr;
    }

    if (ctx->rd.pos == -1) {
        errno = EIO;
        return -1;
    }
    return 0;
}

void media_file_close(void* any)
{
    struct media_file_context* ctx = (struct media_file_context *) any;
    if (ctx->mode == 1) {
        ctx->header.be_file_ms = htonl(ctx->header.be_file_ms);
        ctx->header.be_frame_ms = htonl(ctx->header.be_frame_ms);
        ctx->header.be_bytes = htonl(ctx->header.be_bytes + sizeof(struct file_header));
        fwrite(&ctx->header, 1, sizeof(struct file_header), ctx->file);
    }
    fclose(ctx->file);
    my_free(ctx);
}
