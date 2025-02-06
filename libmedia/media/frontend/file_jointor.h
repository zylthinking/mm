
#ifndef file_jointor_h
#define file_jointor_h

#include "mydef.h"
#include "jointor.h"

#define as_stream ((void *) NULL)
#define no_stream ((void *) 1)

typedef struct {
    void* (*open) (void*);
    int (*read) (void*, struct list_head*, const fraction*);
    void (*close) (void*);
} media_operation_t;

typedef struct {
    media_operation_t* ops;
    void* any;
} media_struct_t;

capi jointor* file_jointor_open(media_struct_t* media);
capi int file_jointor_notify_stream(jointor* jointer, void* id, free_t free_fptr);
capi void file_jointor_close(jointor* join);

#endif
