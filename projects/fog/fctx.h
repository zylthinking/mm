

#ifndef ffit_h
#define ffit_h

#define context_update 1

typedef struct {
    uintptr_t ftype;
    uint64_t have, more;
    uint64_t cursor, duration;
} fcontext_t;

typedef struct {
    uint64_t seekto;
    uintptr_t sync;
    uintptr_t latch[2];
} file_mode_t;

#endif
