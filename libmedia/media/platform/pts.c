
#include "pts.h"
#include "media_buffer.h"
#include "now.h"
#include "lock.h"

static struct pts_generator {
    fraction frac;
    uint64_t pts;
    uint32_t tms;
    lock_t lck;
    uint64_t oldpts;
} gen = {{0, 1}, 0, 0, {0}, 0};

// preconditions:
// 1. audio calling interval <= 300ms
// 2. video calling interval > audio calling interval
// the worst case: video has 300ms delay. it is acceptable.
void fill_pts(struct my_buffer* audio, struct my_buffer* video)
{
    uint32_t current = now();
    lock(&gen.lck);
    if (audio != NULL) {
        my_assert(video == NULL);

        media_buffer* media = (media_buffer *) audio->ptr[0];
        media->pts = gen.pts;

        audio_format* fmt = to_audio_format(media->pptr_cc);
        fraction frac = fmt->ops->ms_from(fmt, (uint32_t) audio->length, 0);
        fraction_add(&gen.frac, &frac);

        uint32_t n = gen.frac.num / gen.frac.den;
        gen.pts += n;

        frac.num = n;
        frac.den = 1;
        fraction_sub(&gen.frac, &frac);

        gen.tms = current + 300;
    } else {
        my_assert(video != NULL);

        if (current > gen.tms) {
            gen.pts += current - gen.tms;
            gen.tms = current;
            fraction_zero(&gen.frac);
        }

        media_buffer* media = (media_buffer *) video->ptr[0];
        if (gen.oldpts >= gen.pts) {
            media->pts = gen.oldpts + 1;
        } else {
            media->pts = gen.pts;
        }
        gen.oldpts = media->pts;
    }
    unlock(&gen.lck);
}
