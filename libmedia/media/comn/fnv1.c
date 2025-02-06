
#include "mydef.h"

uint32_t fnv1_hash32(const char* key, uint32_t bytes)
{
    uint32_t hash = 2166136261;
    for(; bytes > 0; --bytes) {
        hash ^= (uint32_t) (*key++);
        hash *= 16777619;
    }

    hash += hash << 13;
    hash ^= hash >> 7;
    hash += hash << 3;
    hash ^= hash >> 17;
    hash += hash << 5;
    return hash;
}
