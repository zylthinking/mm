
#ifndef fileit_h
#define fileit_h

#include "mydef.h"
#include "jointor.h"
#include "fctx.h"

typedef struct {
    void* (*open) (const char* uri, callback_t* cb, file_mode_t* mode);
    fcontext_t* (*fcontext_get) (void* fctx);
    void (*fcontext_put) (void* rptr);
    intptr_t (*seek) (void* rptr, uint64_t seekto);
    jointor* (*jointor_get) (void* rptr);
    void (*close) (void* rptr);
} fileops_t;

typedef struct {
    intptr_t magic;
    fileops_t* ops;
    void* next;
} filefmt_t;

capi filefmt_t* fiflefmt_get(intptr_t magic);

#endif
