

#ifndef smooth_h
#define smooth_h

#include "mydef.h"
#include <string.h>

#define deine_fill_pcm_xbit(x)  \
static void fill_pcm_##x##bit(int##x##_t* buffer0, intptr_t samples0, int##x##_t* buffer1, intptr_t samples1, int##x##_t val1) \
{ \
    intptr_t n = (samples1 & 1); \
    samples1 >>= 1; \
    int##x##_t* intp1 = &buffer1[0]; \
    \
    intptr_t nr = (samples1 + (samples0 - 1)) / samples0; \
    nr *= 2; \
    int##x##_t val2 = buffer0[samples0 - 1]; \
    float k = ((float) (val1 - val2)) / nr; \
    \
    while (samples1 > 0) { \
        intptr_t ns = my_min(samples0, samples1); \
        int##x##_t* intp0 = &buffer0[samples0 - 1]; \
        for (intptr_t i = 0; i < ns; i++) { \
            int##x##_t c = *intp0; \
            if (val1 == 0) { \
                *(intp0--) = c / 4; \
            } else { \
                *(intp0--) += k; \
            } \
            *(intp1++) = c; \
        } \
        \
        if (__builtin_expect(n == 1, 0)) { \
            int##x##_t c = *intp1; \
            *(intp1++) = c; \
            n = 0; \
        } \
        \
        for (intptr_t i = 0; i < ns; i++) { \
            int##x##_t c = *intp0; \
            if (val1 == 0) { \
                *(intp0++) = c / 4; \
            } else { \
                *(intp0++) += k; \
            } \
            *(intp1++) = c; \
        } \
        \
        samples1 -= ns; \
    } \
}

deine_fill_pcm_xbit(8)
deine_fill_pcm_xbit(16)

static void fill_pcm(intptr_t bits, int8_t* buffer1, intptr_t bytes, int8_t* buffer2, intptr_t need, char* new_ptr)
{
    if (bits == 8) {
        int8_t n = 0;
        if (new_ptr != NULL) {
            n = *(int8_t *) new_ptr;
        }
        fill_pcm_8bit(buffer1, bytes, buffer2, need, n);
    } else {
        int16_t n = 0;
        if (new_ptr != NULL) {
            n = *(int16_t *) new_ptr;
        }
        fill_pcm_16bit((int16_t *) buffer1, bytes / 2, (int16_t *) buffer2, need / 2, n);
    }
}

static void smooth_fill(pcm_format* pcm, char* buf, unsigned int bytes, unsigned int need)
{
    static intptr_t nb = 0;
    static int8_t buffer[4096];
    intptr_t bits = pcm->sambits;

    if (__builtin_expect(nb == 0, 0)) {
        fraction frac = pcm_bytes_from_ms(pcm, 20);
        nb = frac.num / frac.den;
        my_assert(nb <= sizeof(buffer));
    }

    if (__builtin_expect(bytes < need, 0)) {
        intptr_t absent = need - bytes;

        char* new_ptr = NULL;
        if (bytes > 0) {
            memmove(&buf[absent], &buf[0], bytes);
            new_ptr = &buf[absent];
        }
        fill_pcm(bits, &buffer[0], nb, (int8_t *) buf, absent, new_ptr);
    }

    if (bytes >= nb) {
        memcpy(&buffer[0], &buf[bytes - nb], nb);
    } else if (bytes > 0) {
        memmove(&buffer[0], &buf[bytes], nb - bytes);
        memcpy(&buffer[nb - bytes], &buf[0], bytes);
    }
}

#endif
