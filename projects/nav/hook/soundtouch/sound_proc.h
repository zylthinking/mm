

#ifndef sound_proc_h
#define sound_proc_h

#include "mydef.h"
#include "fmt.h"
#include "my_buffer.h"
#include "efftype.h"

capi void effect_settype(int type);
capi intptr_t soundtouch_proc(void* uptr, intptr_t identy, void* any);

#endif
