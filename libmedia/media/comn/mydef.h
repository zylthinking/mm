
#ifndef mydef_h
#define mydef_h

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "my_errno.h"

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <winsock2.h>
#include <mswsock.h>
#include <mmsystem.h>

#pragma warning(disable: 4217)
#pragma warning(disable: 4996)
#pragma warning(disable: 4273)
#pragma warning(disable: 4312)
#pragma comment(lib, "mswsock.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "pthreadVSE2.lib")
#pragma comment(lib, "winmm.lib")

#ifdef dll_import
#define visible __declspec(dllimport)
#else
#define visible __declspec(dllexport)
#endif

#define inline __inline
#define __inline__ __inline
#define __attribute__(x)
#define __builtin_expect(x, y) (x)

static inline int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    *memptr = _aligned_malloc(size, alignment);
    if (*memptr == NULL) {
        return -1;
    }
    return 0;
}
#define posix_free _aligned_free

#define usleep(x) Sleep((x) / 1000)

static int __sync_bool_compare_and_swap(void** lkp, void* cmp_value, void* new_value)
{
    void* n = InterlockedCompareExchangePointer((volatile PVOID*) lkp, new_value, cmp_value);
    return (n == cmp_value);
}

#define __sync_val_compare_and_swap(lkp, cmp_value, new_value)  \
    InterlockedCompareExchange ((LONG *) (lkp), (LONG) (new_value), (LONG) (cmp_value))

#define __sync_add_and_fetch(lkp, val) \
    (val + ((int) InterlockedExchangeAdd((LONG *) (lkp), (LONG) val)))

#define __sync_sub_and_fetch(lkp, val) __sync_add_and_fetch((lkp), -(val))
#define __sync_lock_test_and_set(lkp, val) (void *) InterlockedExchangePointer((void **) (lkp), (void *) (val))

#else
#define visible
#include "unistd.h"
#include <sys/syscall.h>
#include <sched.h>

#if defined(__linux__)
#include <linux/futex.h>
#endif

#ifdef __ANDROID__
#include <malloc.h>

#define futex(p1, p2, p3, p4, p5, p6) syscall(__NR_futex, p1, p2, p3, p4, p5, p6)

__attribute__((weak)) int posix_memalign(void** pp, size_t align, size_t size)
{
    *pp = memalign(align, size);
    if (*pp == NULL) {
        return -1;
    }
    return 0;
}

#endif
#define posix_free free
#endif

#ifdef __cplusplus
#define capi extern "C" visible
#define capiw extern "C" visible __attribute__((weak))
#else
#define capi extern visible
#define capiw extern visible __attribute__((weak))
#endif

#define my_min(x, y) ((x) < (y) ? (x) : (y))
#define my_max(x, y) ((x) > (y) ? (x) : (y))
#define event_number(x) (0 == (x & (x - 1)))

#define elements(x) (sizeof(x) / sizeof(x[0]))
#ifndef container_of
#define container_of(ptr, type, member) ((type *) ((char *) ptr - offsetof(type, member)))
#endif

typedef intptr_t (*notify_t) (void* uptr, intptr_t identy, void* any);
typedef void (*free_t) (void*);
typedef int (*printf_t) (const char* fmt, ...);
#define roundup(bytes, align) (((bytes) + (align) - 1) & (~((align) - 1)))
static __attribute__((unused)) void no_free(void* addr){}

typedef struct {
    void* uptr;
    notify_t notify;
    free_t uptr_put;
} callback_t;

#if defined(__arm__)
static __attribute__((unused)) inline const int16_t clip_int16(int a)
{
    int x;
    __asm__ ("ssat %0, #16, %1" : "=r"(x) : "r"(a));
    return x;
}
#else
static __attribute__((unused)) inline const int16_t clip_int16(int a)
{
    if ((a + 0x8000U) & (~0xFFFF)) {
        a = ((a >> 31) ^ 0x7FFF);
    }
    return a;
}
#endif

#if defined(__arm__)
    #define my_abort() __asm__("bkpt 0")
#elif defined(__arm64__)
    #define my_abort() __asm__("brk 0")
#elif defined(_MSC_VER)
    #define my_abort() asm{"int 3"}
#else
    #define my_abort() __asm__("int $3")
#endif

#if defined(_MSC_VER)
    #define barrier() __asm {nop}
    #define rmb() __asm {lfence}
    #define wmb() __asm {sfence}
    #define systid() ((uintptr_t) GetCurrentThreadId())
#else
    #if defined(__linux__)
        #define systid() ((uintptr_t) syscall(__NR_gettid))
    #elif defined(__APPLE__)
        #define systid() ((uintptr_t) syscall(SYS_thread_selfid))
    #endif

    #define barrier() __asm__ __volatile__("":::"memory")
    #if (defined(__arm__)) || (defined(__arm64__))
        #if (defined(__ARM_ARCH_7A__) || defined(__arm64__))
            #define rmb() __asm__ __volatile__ ("dsb sy":::"memory")
            #define wmb() __asm__ __volatile__ ("dsb sy":::"memory")
        #else
            #define rmb()
            #define wmb()
        #endif
    #else
        #define rmb() __asm__ __volatile__("lfence":::"memory")
        #define wmb() __asm__ __volatile__("sfence":::"memory")
    #endif
#endif

#include "logmsg.h"
#endif
