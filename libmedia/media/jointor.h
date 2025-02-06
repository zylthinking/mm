
#ifndef jointor_h
#define jointor_h

#include <stdint.h>
#include "mydef.h"
#include "fraction.h"
#include "list_head.h"
#include "media_buffer.h"

#define link_from_up 0
// must not unlink_from_down before link_from_down returns
#define link_from_down 1
#define unlink_from_down 2

struct jointor_ops;

#define stand_join 0
#define stand_mixer 1
#define stand_file 2
#define flag_realtime 1

typedef struct {
    intptr_t stand;
    intptr_t identy;
    intptr_t extra;
} jointor_context;

typedef struct {
    struct list_head entry[2];
    void* any;
    struct jointor_ops* ops;
    jointor_context ctx;
} jointor;

struct jointor_ops {
    int (*jointor_get) (jointor*);
    int (*jointor_put) (jointor*);

    // only when return -1 list_head is assure to be untouched.
    int (*jointor_pull) (void*, struct list_head*, const fraction*);
    int (*jointor_push) (void*, int, void*);
};

#endif
