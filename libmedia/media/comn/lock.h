
#ifndef lock_h
#define lock_h

#include <stdint.h>
#include <string.h>
#include "mydef.h"
#include "now.h"
#define debug_lock 1

typedef struct {
    intptr_t lck;
    uintptr_t tid;
    uintptr_t nr;
#if debug_lock
    const char* file;
    uintptr_t line;
#endif
} lock_t;

#define lock_initial {0}
#define lock_initial_locked {1, 0, 1}
static __attribute__((unused)) lock_t lock_val = lock_initial;

#if (debug_lock == 2)
#define locktrace_begin() uintptr_t tms = now();
#define lock_backtrace(lkp) \
do { \
    uintptr_t current = now(); \
    if (tms == 0) { \
        tms = current; \
    } \
    \
    if (current > tms + 3000) { \
        tms = current - 2000; \
        logmsg("locktrace: %d %s:%d\n", (int) lkp->lck, lkp->file, (int) lkp->line); \
    } \
} while (0)
#else
#define locktrace_begin() (void) 0
#define lock_backtrace(x) (void) 0
#endif

#if debug_lock
    #define log_lock(ptr, l, f) do {ptr->line = l; ptr->file = f;} while (0)
    #define log_unlock(ptr) do {ptr->line = -1; ptr->file = "";} while (0)
#else
    #define log_lock(ptr, l, f) (void) (0)
    #define log_unlock(ptr) (void) (0)
#endif

#ifdef __linux__
    #ifndef _GNU_SOURCE
    #define _GNU_SOURCE
    #endif

    #define my_lock(lkp, re) \
    do {  \
        lock_t* ptr = lkp; \
        if (!__sync_bool_compare_and_swap(&ptr->lck, 0, 1)) { \
            if (ptr->lck == 2) { \
                syscall(__NR_futex, &ptr->lck, FUTEX_WAIT, 2, NULL, NULL, 0); \
            } \
            \
            locktrace_begin(); \
            while (0 != __sync_lock_test_and_set(&ptr->lck, 2)) { \
                syscall(__NR_futex, &ptr->lck, FUTEX_WAIT, 2, NULL, NULL, 0); \
                lock_backtrace(ptr); \
            } \
        } \
        my_assert2(ptr->lck != 0, "lck = %d", ptr->lck); \
        my_assert2(ptr->nr == 0, "lck = %d, nr = %d, %d@%s", ptr->lck, ptr->nr, ptr->line, ptr->file); \
        log_lock(ptr, __LINE__, __FILE__); \
        \
        my_assert(ptr->tid == 0); \
        if (re) { \
            ptr->tid = systid(); \
        } \
        ++ptr->nr; \
    } while (0)

    #define unlock(lkp) \
    do { \
        lock_t* ptr = lkp; \
        my_assert2(ptr->lck != 0, "lck = %d, nr = %d", ptr->lck, ptr->nr); \
        --ptr->nr; \
        wmb(); \
        if (ptr->nr > 0) { \
            my_assert2(ptr->tid != 0, "tid != 0, ptr->nr = %d, lck = %d, %d@%s", ptr->nr, ptr->lck, ptr->line, ptr->file); \
        } else { \
            ptr->tid = 0; \
            /* wmb(); */ \
            log_unlock(ptr); \
            if (2 == __sync_lock_test_and_set(&ptr->lck, 0)) { \
                while (-1 == syscall(__NR_futex, &ptr->lck, FUTEX_WAKE, 1, NULL, NULL, 0)); \
            } \
        } \
    } while (0)

#else
    #define my_lock(lkp, re) \
    do {  \
        lock_t* ptr = lkp; \
        locktrace_begin(); \
        while (!__sync_bool_compare_and_swap((void **) &ptr->lck, (void *) 0, (void *) 1)) { \
            sched_yield();  \
            lock_backtrace(ptr); \
        } \
        log_lock(ptr, __LINE__, __FILE__); \
        \
        my_assert(ptr->tid == 0); \
        if (re) { \
            ptr->tid = systid(); \
        } \
        ++ptr->nr; \
    } while (0)

    #define unlock(lkp) \
    do { \
        lock_t* ptr = lkp; \
        my_assert(ptr->lck != 0); \
        --ptr->nr; \
        wmb(); \
        if (ptr->nr > 0) { \
            my_assert(ptr->tid != 0); \
        } else { \
            ptr->tid = 0; \
            /* wmb(); */ \
            log_unlock(ptr); \
            ptr->lck = 0; \
        } \
    } while (0)
#endif

#define lock(lkp) my_lock(lkp, 0)

#define relock(lkp) \
do {  \
    lock_t* ptr = lkp; \
    /* this rmb() is here to assure to see ptr->tid = 0 in unlock */ \
    /* if thread exit after unlock(), then another thread is spwaned with same tid */ \
    /* on another cpu core and then call lock_recursive. */ \
    /* all the above happens so quickly that the other cpu core does not see ptr->tid = 0 */ \
    /* it is so impossible to happen that I comment the "correct" implemention. */ \
    /* rmb(); */ \
    \
    if (ptr->tid == systid()) { \
        /* if true, it's same thread, event in another cpu core, no mb() is needed. */ \
        ++ptr->nr; \
    } else { \
        my_lock(lkp, 1); \
    } \
} while (0)

static __attribute__((unused)) inline intptr_t my_try_lock(lock_t* lkp, uintptr_t re, uintptr_t line, const char* file)
{
    if (!__sync_bool_compare_and_swap((void **) &((lkp)->lck), (void *) 0, (void *) 1)) {
        return -1;
    }
    log_lock(lkp, line, file);

    my_assert(lkp->tid == 0);
    if (re) {
        lkp->tid = systid();
    }
    ++lkp->nr;
    return 0;
}

#define try_lock(lkp) my_try_lock(lkp, 0, __LINE__, __FILE__)
#define retry_lock(lkp) my_try_lock(lkp, 1, __LINE__, __FILE__)

typedef struct {
    intptr_t nr;
} rwlock_t;

#define read_write_max 8000
#define rw_lock_initial {read_write_max}
static __attribute__((unused)) rwlock_t rw_lock_val = rw_lock_initial;

#define read_write_lock(lckp, val) \
do { \
    rwlock_t* lck = lckp; \
    do { \
        intptr_t n = __sync_sub_and_fetch(&lck->nr, val); \
        if (n >= 0) { \
            break; \
        } \
        __sync_add_and_fetch(&lck->nr, val); \
        sched_yield(); \
    } while (1); \
} while (0)

#define read_write_unlock(lckp, val) \
do { \
    rwlock_t* lck = lckp; \
    __sync_add_and_fetch(&lck->nr, val); \
} while (0)

#define read_lock(lckp) read_write_lock(lckp, 1)
#define write_lock(lckp) read_write_lock(lckp, read_write_max)
#define read_unlock(lckp) read_write_unlock(lckp, 1)
#define write_unlock(lckp) read_write_unlock(lckp, read_write_max)

#endif
