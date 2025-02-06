
#ifndef myjni_h
#define myjni_h

#include "mydef.h"
#include <jni.h>

#define clsname(x, y) #x"/"#y
#define methods_entry(p, c, f1, f2, f3) \
{ \
    .open = f1, \
    .close = f2, \
    .loaded = f3, \
    .cls = clsname(p, c), \
    .nr = elements(c##_methods), \
    .methods = c##_methods, \
    .next = NULL \
}

struct java_mehods {
    int32_t (*open) (JavaVM* vm, JNIEnv* env);
    void (*loaded) ();
    void (*close) (JavaVM* vm, JNIEnv* env);
    const char* cls;
    uint32_t nr;
    JNINativeMethod* methods;
    struct java_mehods* next;
};

typedef struct {
    const char* code[3];
    jclass clazz;
    jmethodID id;
    int32_t attached;
} java_code;

capi void java_method_register(struct java_mehods* methods);
capi JNIEnv* jni_get(java_code* jcode, jclass clazz);
capi void jni_put(java_code* jcode);

static __attribute__((unused)) void jcode_get_env(java_code* code)
{
    code->code[0] = code->code[1] = code->code[2] = NULL;
    code->clazz = NULL;
    code->id = NULL;
    code->attached = 0;
}

#endif