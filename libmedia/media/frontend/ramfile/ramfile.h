
#ifndef ramfile_h
#define ramfile_h

#include "mydef.h"
#include "jointor.h"
#include "my_buffer.h"

capi void* ramfile_open();
capi jointor* ramfile_jointor_get(void* ptr);
capi int32_t ramfile_write(void* ptr, struct my_buffer* mbuf);
capi void ramfile_close(void* ptr);

#endif