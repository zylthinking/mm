
#ifndef thread_clean_h
#define thread_clean_h

#include "mydef.h"

#define process_key_nr 2
capi void* thread_clean_push(intptr_t idx, free_t fptr, size_t bytes);

#endif
