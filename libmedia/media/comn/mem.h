
#ifndef mem_h
#define mem_h

#include <stdlib.h>
#include "mydef.h"

#if 0 && defined(NDEBUG)
#define debug_mem 0
#else
#define debug_mem 1
#endif

#if debug_mem
    capiw void* debug_malloc(size_t n, const char* func, int line);
    capiw void debug_free(void* ptr);
    capiw size_t debug_mem_bytes();
    capiw void debug_mem_stats(printf_t func_ptr);
    capiw void my_tell(void* ptr);
    capiw void check_memory(void* ptr);

    #define my_malloc(n) debug_malloc(n, __FUNCTION__, __LINE__)
    #define my_free debug_free
    #define mem_bytes debug_mem_bytes
    #define mem_stats debug_mem_stats
#else
    #define debug_malloc(x, y, z) malloc(x)
    #define my_malloc malloc
    #define my_free free
    #define mem_bytes() 0
    #define mem_stats(x) ((void) (0))
    #define my_tell(x) ((void) (0))
    #define check_memory(x) ((void) (0))
#endif

#define heap_alloc(x) (typeof(*x)*) my_malloc(sizeof(*x))
#define heap_alloc2(x, nb) (typeof(*x)*) my_malloc(sizeof(*x) + nb)
#define heap_free my_free

#endif
