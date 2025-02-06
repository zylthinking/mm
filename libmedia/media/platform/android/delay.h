
#ifndef delay_h
#define delay_h

#include "mydef.h"
#include "fmt.h"
#include "now.h"
#include <string.h>

#define stable_ms 0
#define static_delay 64
#define delay_round 10

typedef struct {
    uint32_t pts;
    uint32_t stable;
    uint32_t mean_nr;
    uint32_t delay[32];
    uint32_t ms_bytes;
    uint32_t bytes_pulled;
} delay_estimator;

static uint32_t mean_delay(delay_estimator* est, uint32_t delay)
{
    uint32_t nr = elements(est->delay) - 2;
    uint32_t indx = est->delay[nr] % nr;
    uint32_t old_delay = est->delay[indx];
    est->delay[nr + 1] = (est->delay[nr + 1] + delay) - old_delay;
    est->delay[indx] = delay;
    est->delay[nr] += 1;

    if (est->delay[nr] < nr) {
        indx = est->delay[nr];
    } else {
        indx = nr;
    }

    delay = (est->delay[nr + 1] / indx) / delay_round;
    delay *= delay_round;
    return delay;
}

static void delay_estimator_init(delay_estimator* est)
{
    memset(est, 0, sizeof(delay_estimator));
    est->stable = !(stable_ms);
}

static uint32_t calc_delay(delay_estimator* est, uint32_t bytes, audio_format* fmt)
{
#define ms_reap (1000 * 120)
    uint32_t current = now();
    if (est->pts == 0) {
        est->pts = current;
    }

    if (est->ms_bytes == 0) {
        fraction f = pcm_bytes_from_ms(fmt->pcm, 1);
        est->ms_bytes = f.num / f.den;
    }

    est->bytes_pulled += bytes;
    if (est->bytes_pulled > ms_reap * est->ms_bytes) {
        est->pts += ms_reap;
        est->bytes_pulled -= ms_reap * est->ms_bytes;
    }

    uint32_t ms = 0;
    if (est->stable == 0) {
        if (current < est->pts + stable_ms) {
            return 0;
        }
        est->stable = 1;
    }

    if (!static_delay || est->stable == 1) {
        ms = est->bytes_pulled / est->ms_bytes;
        uint32_t tms = current - est->pts;
        if (ms > tms) {
            ms -= tms;
        } else {
            logmsg("pulled %d, elapsed: %d, reset the delay estimator.\n", ms, tms);
            est->pts = current;
            est->bytes_pulled = bytes;
            ms = bytes / est->ms_bytes;
        }

        est->mean_nr += 1;
        ms = mean_delay(est, ms);
        if (est->mean_nr == static_delay) {
            est->stable = ms;
        }
    } else {
        ms = est->stable;
    }

    return ms;
#undef ms_reap
}

#endif
