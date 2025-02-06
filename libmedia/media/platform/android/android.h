
#ifndef android_h
#define android_h

#include <stdint.h>
#include <jni.h>
#include "my_buffer.h"

#define omx_decode 0
#define omx_encode 1
#define bug_mask 2
#define front_angle 3
#define camera_csp 4
#define back_angle 5
#define omx_usurp 6

#define bug_egl_release     (1 << 0)
#define bug_egl_dettach     (1 << 1)
#define bug_dlclose         (1 << 2)
#define bug_nv12_nv21       (1 << 3)
#define bug_gl_flush        (1 << 4)
#define bug_gl_no_resize    (1 << 5)
#define bug_gl_clear        (1 << 6)
#define bug_gl_clear2       (1 << 7)

static __attribute__((unused)) int64_t* android_feature_address()
{
    static int64_t* feature = NULL;
    if (feature == NULL) {
        char addr[64];
        const char* p = getenv("android_feature_address");
        if (p != NULL) {
            feature = (int64_t *) (intptr_t) strtoll(p, NULL, 10);
        }
    }
    return feature;
}

typedef struct {
    jbyteArray pixel;
    jbyte* mem;

    struct mbuf_operations* mop;
    intptr_t refs;
} java_buffer_t;

#endif
