
#if defined(__ANDROID__)
#include "myjni.h"
#include "fmt.h"
#include "android.h"
#include <stdlib.h>

static int64_t android_feature[] = {
    [omx_decode] = 1,
    [omx_encode] = 1,
    [bug_mask] = 0,
    [front_angle] = 3,
    [back_angle] = 1,
    [camera_csp] = csp_nv21ref,
    [omx_usurp] = 0,
};

static void jni_libpath_set(JNIEnv* env, jclass clazz, jbyteArray path)
{
    char addr[512];
    jint length = (*env)->GetArrayLength(env, path);
    jbyte* buf = (*env)->GetByteArrayElements(env, path, NULL);

    strncpy(addr, buf, length);
    addr[length] = 0;
    (*env)->ReleaseByteArrayElements(env, path, buf, 0);

    setenv("andaroid_so_path", addr, 1);
    sprintf(addr, "%lld", (long long) (intptr_t) android_feature);
    setenv("android_feature_address", addr, 1);
}

static void jni_feature_mask(JNIEnv* env, jclass clazz, jlong feature, jlong value)
{
    android_feature[feature] = value;
}

static JNINativeMethod runtime_methods[] = {
    {
        .name = "libpath_set",
        .signature = "([B)V",
        .fnPtr = jni_libpath_set
    },

    {
        .name = "feature_mask",
        .signature = "(JJ)V",
        .fnPtr = jni_feature_mask
    }
};

__attribute__((constructor)) static void api_register()
{
    static struct java_mehods api_entry = methods_entry(libmedia, runtime, NULL, NULL, NULL);
    java_method_register(&api_entry);
}

#endif
