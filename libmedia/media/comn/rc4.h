
#ifndef rc4_h
#define rc4_h
#include <stdint.h>
#if 1

#define klen 256
typedef struct rc4_key_st
{
    uint8_t m,n;
    uint8_t key[256];
} rc4_stat;

static inline void rc4_init(rc4_stat* stat, const unsigned char* k, int bytes)
{
    int i;
    uint8_t key[klen];
    stat->m = stat->n = 0;

    for (i = 0; i < klen; i++) {
        stat->key[i] = i;
        key[i] = (uint8_t) k[i % bytes];
    }

    uint8_t j = 0;
    for (i = 0; i < klen; i++) {
        j += stat->key[i] + key[i];

        uint8_t c = stat->key[i];
        stat->key[i] = stat->key[j];
        stat->key[j] = c;
    }
}

static inline void rc4(rc4_stat* stat, unsigned char* buffer, int bytes)
{
    uint8_t m = stat->m, n = stat->n;
    uint8_t* pch = &(stat->key[0]);

    for (int i = 0; i < bytes; i++) {
        uint8_t c = pch[++m];
        n = n + c;
        pch[m] = pch[n];
        pch[n] = c;
        uint8_t q = pch[m] + pch[n];
        buffer[i] ^= pch[q];
    }

    stat->m = m;
    stat->n = n;
}

#else

typedef struct
{
    uint32_t x,y;
    uint32_t data[256];
} rc4_stat;

static inline void rc4_init(rc4_stat* key, const unsigned char* data, int len)
{
    int id1, id2;

    uint32_t* d = &(key->data[0]);
    key->x = 0;
    key->y = 0;
    id1 = id2 = 0;

#define SK_LOOP(d,n) \
do { \
    uint32_t tmp = d[(n)]; \
    id2 = (data[id1] + tmp + id2) & 0xff; \
    if (++id1 == len) id1=0; \
    d[(n)] = d[id2]; \
    d[id2] = tmp; \
} while (0)

    for (int i = 0; i < 256; i++) {
        d[i] = i;
    }

    for (int i = 0; i < 256; i += 4) {
        SK_LOOP(d, i + 0);
        SK_LOOP(d, i + 1);
        SK_LOOP(d, i + 2);
        SK_LOOP(d, i + 3);
    }
}

static inline void rc4(rc4_stat* key, unsigned char* buffer, int len)
{
    const unsigned char* input = buffer;
    unsigned char* output = buffer;

    uint32_t tx, ty;

    uint32_t x = key->x;
    uint32_t y = key->y;
    uint32_t* d = key->data;

#define LOOP(in, out) \
do { \
    x = ((x + 1) & 0xff); \
    tx = d[x]; \
    y = (tx + y) & 0xff; \
    d[x] = ty = d[y]; \
    d[y] = tx; \
    (out) = d[( tx + ty) & 0xff] ^ (in); \
} while (0)

    for (int i = len >> 3; i > 0; --i) {
        LOOP(input[0], output[0]);
        LOOP(input[1], output[1]);
        LOOP(input[2], output[2]);
        LOOP(input[3], output[3]);
        LOOP(input[4], output[4]);
        LOOP(input[5], output[5]);
        LOOP(input[6], output[6]);
        LOOP(input[7], output[7]);
        input += 8;
        output += 8;
    }

    int i = 0;
    switch (len & 0x07) {
        case 7: LOOP(input[i], output[i]); ++i;
        case 6: LOOP(input[i], output[i]); ++i;
        case 5: LOOP(input[i], output[i]); ++i;
        case 4: LOOP(input[i], output[i]); ++i;
        case 3: LOOP(input[i], output[i]); ++i;
        case 2: LOOP(input[i], output[i]); ++i;
        case 1: LOOP(input[i], output[i]);
    }
    key->x = x;
    key->y = y;
}

#endif

#define rc4_init(x, y, z) rc4_init((rc4_stat *) x, (const unsigned char *) y, (int) z)
#define rc4(x, y, z) rc4((rc4_stat *) x, (unsigned char*) y, (int) z)
#endif
