
#ifndef shader_h
#define shader_h

#include "mydef.h"
#include "gl.h"

#define shader_rgb 0
#define shader_i420 1
#define shader_nv12 2
#define shader_gbrp 3
#define shader_nv21 4

capi void* shader_create_objects();
capi void shader_delete_objects(void* ptr);
capi void shader_update_vertex(void* ptr, int z, int first, int angle);
capi intptr_t shader_change_mask(void* ptr, intptr_t idx, float fx, float fy, float fw, float fh, float fz);
capi intptr_t shader_clear_mask(void* ptr);
capi void shader_texture_ratio(void* ptr, float width, float height);
capi void shader_ortho(void* ptr, GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat nearval, GLfloat farval);
capi void shader_update_texture(void* ptr);
capi void shader_set_type(void* ptr, int type);

#endif
