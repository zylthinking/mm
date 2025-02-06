
#ifndef videoctl_h
#define videoctl_h

#include "mydef.h"

enum camera_position_t {back, front, unkown};
capi int use_camera(enum camera_position_t campos);

#endif
