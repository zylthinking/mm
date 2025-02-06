

#ifndef fdset_h
#define fdset_h

#include "mydef.h"

capi void* fdset_new(uint32_t ms_cycle);
capi int fdset_join1(void* set, volatile int32_t* flag);
// keep should be an uint32_t[3], and should be zero
// before passed to fdset_join2, and should be avaliable
// until the last fdset_join2 returns
capi int fdset_join2(void* set, volatile int32_t* flag, uint32_t* keep);
capi void fdset_delete(void* set);

#endif
