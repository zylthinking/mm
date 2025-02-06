
#ifndef fraction_h
#define fraction_h

#include <stdint.h>
#include <stdlib.h>

typedef struct tag_fraction {
    uint32_t num;
    uint32_t den;
} fraction;

static uint32_t bgcd (uint32_t x, uint32_t y)
{
    if (x == 0) return y;
    if (y == 0) return x;
    uint32_t bits = __builtin_ctz(x | y);

    x = (x >> bits);
    y = (y >> bits);
    x >>= __builtin_ctz(x);
    do {
        y >>= __builtin_ctz(y);

        uint32_t n = y;
        y = abs((int) x - (int) y);
        x = n;
    } while (y);

    return (x << bits);
}

static inline void fraction_zero(fraction* f)
{
    f->num = 0;
    f->den = 1;
}

static inline void fraction_add(fraction* one, const fraction* two)
{
    if (one->den == two->den) {
        one->num += two->num;
        return;
    }

    one->num = one->num * two->den + two->num * one->den;
    one->den = one->den * two->den;
    if (one->num <= 0xffff && one->den <= 0xffff) {
        return;
    }
    uint32_t n = bgcd(one->num, one->den);
    one->num /= n;
    one->den /= n;
}

static inline int fraction_sub(fraction* one, const fraction* two)
{
    if (one->den == two->den) {
        one->num -= two->num;
        return (int) one->num;
    }

    one->num = one->num * two->den - two->num * one->den;
    one->den = one->den * two->den;
    if (one->num == 0) {
        one->den = 1;
        return 0;
    }

    if (one->num > 0xffff || one->den > 0xffff) {
        uint32_t n = bgcd(one->num, one->den);
        one->num /= n;
        one->den /= n;
    }
    return one->num;
}

#endif
