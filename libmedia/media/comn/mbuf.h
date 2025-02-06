
#ifndef mbuf_h
#define mbuf_h

#include "mydef.h"
#include "my_buffer.h"

capi intptr_t mbuf_hget(uintptr_t bytes, uintptr_t capacity, intptr_t clone);
capi struct my_buffer* do_mbuf_alloc_1(intptr_t handle, const char* func, int line);
capi void mbuf_reap(intptr_t handle);

capi struct my_buffer* do_mbuf_alloc_2(uintptr_t bytes, const char* func, int line);
capi struct my_buffer* do_mbuf_alloc_3(uintptr_t bytes, const char* func, int line);
capi struct my_buffer* do_mbuf_alloc_4(void* addr, uintptr_t bytes, const char* func, int line);
capi struct my_buffer* do_mbuf_alloc_5(void* addr, uintptr_t bytes, const char* func, int line);
capi struct my_buffer* mbuf_alloc_6(void* addr, uintptr_t bytes);

capi void mbuf_tell(struct my_buffer* mbuf);

#define mbuf_alloc_1(handle) do_mbuf_alloc_1(handle, __FUNCTION__, __LINE__)
#define mbuf_alloc_2(bytes) do_mbuf_alloc_2(bytes, __FUNCTION__, __LINE__)
#define mbuf_alloc_3(bytes) do_mbuf_alloc_3(bytes, __FUNCTION__, __LINE__)
#define mbuf_alloc_4(addr, bytes) do_mbuf_alloc_4(addr, bytes, __FUNCTION__, __LINE__)
#define mbuf_alloc_5(addr, bytes) do_mbuf_alloc_5(addr, bytes, __FUNCTION__, __LINE__)

#endif
