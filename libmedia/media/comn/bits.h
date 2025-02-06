
#ifndef bits_h
#define bits_h

#include <stdint.h>

inline static uint32_t get_bit_at(const uint8_t* const base, uint32_t offset)
{
    return ((*(base + (offset >> 0x3))) >> (0x7 - (offset & 0x7))) & 0x1;
}

inline static uint32_t be_bits_value(const uint8_t* const base, uint32_t* const offset, uint8_t bits)
{
    uint32_t value = 0;
    for (int i = 0; i < bits; i++) {
        value = (value << 1) | get_bit_at(base, (*offset)++);
    }
    return value;
}

#endif
