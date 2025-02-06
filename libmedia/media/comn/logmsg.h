
#ifndef logmsg_h
#define logmsg_h

#include "mydef.h"
#include <string.h>

static __attribute__((unused)) const char* my_basename(const char* path)
{
    char* pch = strrchr(path, '/');
    if (pch == NULL) {
        return path;
    }
    return pch + 1;
}

#ifndef _MSC_VER
    capi void logbuffer(const char * __restrict, ...);
    #define logmsg(fmt, args...) \
    do { \
        logbuffer(fmt, ##args); \
    } while (0)
#else
    #define logmsg printf
#endif

#if 0
    #define mark(...) (void) 0
#else
    #define mark(fmt, args...) logmsg("%d@%s tid %d " fmt "\n", __LINE__, __FUNCTION__, systid(), ##args)
#endif

#if defined(NDEBUG)
    #define ctrace(x) do {x;} while (0)
    #define logmsg_d(...) (void) 0
#else
    #if 0
        #define ctrace(x) do {mark(#x " begin"); x; mark(#x " end");} while (0)
    #else
        #define ctrace(x) do {x;} while (0)
    #endif
    #define logmsg_d(...) logmsg(__VA_ARGS__)
#endif

#if 0 && defined(NDEBUG)
#define my_assert(...) ((void) 0)
#define my_assert2(...) ((void) 0)
#else
#include "backtrace.h"
#define my_assert(x) \
do { \
    if (!(x)) { \
        mark("of %s, assert failed", my_basename(__FILE__)); \
        backtrace_print(16); \
        my_abort(); \
    } \
} while (0)

#define my_assert2(x, fmt, args...) \
do { \
    if (!(x)) { \
        mark("of %s, assert failed: " fmt, my_basename(__FILE__), ##args); \
        backtrace_print(16); \
        my_abort(); \
    } \
} while (0)

#endif

#define trace_interval(fmt, args...) \
do { \
    static uint32_t x = 0; \
    int cur = now(); \
    if (x == 0) { \
        x = cur; \
    } \
    logmsg("%d@%s: %d " fmt "\n", __LINE__, __FUNCTION__, cur - x, ##args); \
    x = cur; \
} while (0)

#define trace_change(x, msg) \
do { \
    static int64_t y = 0; \
    const char* message = msg; \
    if (message == NULL) { \
        message = #x; \
    } \
    if (y != x) { \
        logmsg("%d@%s tid %d %s %lld(%llx) -> %lld(%llx)\n", \
               __LINE__, __FUNCTION__, systid(), message, y, y, (int64_t) x, (int64_t) x); \
        y = (int64_t) x; \
    } \
} while (0)

#define trace_change2(x, fmt, args...) \
do { \
    static int64_t y = 0; \
    if (y != x) { \
        logmsg("%d@%s tid %d %lld(%llx) -> %lld(%llx) " fmt "\n", \
                __LINE__, __FUNCTION__, systid(), y, y, (int64_t) x, (int64_t) x, ##args); \
        y = (int64_t) x; \
    } \
} while (0)

#define logpcm_enable 0
#define logpcm(x, buf, len) \
do { \
    extern const char* pcm_path;\
    static FILE* file = NULL; \
    if (pcm_path != NULL) { \
        if (file == NULL) { \
            char path[1204] = {0}; \
            sprintf(path, "%s/%s", pcm_path, x); \
            file = fopen(path, "wb"); \
            if (file == NULL) { \
                logmsg("failed to open %s, errno: %d\n", path, errno); \
            } else { \
                logmsg("opened %s %p\n", path, file); \
            } \
        } \
        \
        if (file != NULL) { \
            fwrite(buf, 1, len, file); \
            fflush(file); \
        } \
    } else if (file != NULL) { \
        logmsg("close %p\n", file); \
        fclose(file); \
        file = NULL; \
    } \
} while (0)

#endif
