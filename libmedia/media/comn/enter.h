

#ifndef entrypoint_h
#define entrypoint_h

#include "mydef.h"
#include "lock.h"

typedef struct {
    lock_t lck;
    intptr_t nr;
    intptr_t forbide;
} entry_point;

#define enter_init(block) \
{ \
    .lck = lock_initial, \
    .nr = 0, \
    .forbide = block \
}

static __attribute__((unused)) void init_enter(entry_point* point, intptr_t block)
{
    point->nr = 0;
    point->lck = lock_val;
    point->forbide = block;
}

static __attribute__((unused)) void enter_block(entry_point* point, intptr_t block)
{
    point->forbide = block;
    if (point->forbide == 1) {
        lock(&point->lck);
        while (point->nr > 0) {
            unlock(&point->lck);
            sched_yield();
            lock(&point->lck);
        }
        unlock(&point->lck);
    }
}

static __attribute__((unused)) void enter_out(entry_point* point)
{
    lock(&point->lck);
    --point->nr;
    unlock(&point->lck);
}

static __attribute__((unused)) intptr_t enter_in(entry_point* point)
{
    lock(&point->lck);
    if (point->forbide == 1) {
        unlock(&point->lck);
        return 1;
    }
    ++point->nr;
    unlock(&point->lck);
    return 0;
}

#endif
