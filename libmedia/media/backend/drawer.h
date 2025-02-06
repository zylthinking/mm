

#ifndef drawer_h
#define drawer_h

#include "mydef.h"
#include "list_head.h"

capi void* drawer_get();
capi void drawer_write(void* rptr, struct list_head* headp);
capi void drawer_put(void* rptr);

#endif
