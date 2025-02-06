
#include "myjni.h"
#include "my_handle.h"

jclass media_clszz = NULL;
extern jint jni_pcm_pull(JNIEnv* env, jobject obj, jlong something, jbyteArray cpmbuf, jint padding, uint32_t delay);
extern jint jni_pcm_push(JNIEnv* env, jobject obj, jlong something, jbyteArray pcmbuf, uint32_t delay);
extern jint jni_pixel_push(JNIEnv* env, jobject obj, jlong something, jbyteArray pixel);

static int32_t lib_open(JavaVM* vm, JNIEnv* env)
{
    media_clszz = (*env)->FindClass(env, "libmedia/media");
    media_clszz = (*env)->NewGlobalRef(env, media_clszz);
    return 0;
}

static void jni_handle_dereference(JNIEnv* env, jclass clazz, jlong handle)
{
    handle_release((my_handle *) (intptr_t) handle);
}

static JNINativeMethod media_methods[] = {
    {
        .name = "handle_dereference",
        .signature = "(J)V",
        .fnPtr = jni_handle_dereference
    },

    {
        .name = "pcm_pull",
        .signature = "(J[BII)I",
        .fnPtr = jni_pcm_pull
    },

    {
        .name = "pcm_push",
        .signature = "(J[BI)I",
        .fnPtr = jni_pcm_push
    },

    {
        .name = "pixel_push",
        .signature = "(J[B)I",
        .fnPtr = jni_pixel_push
    }
};

__attribute__((constructor)) static void media_register()
{
    static struct java_mehods media_entry = methods_entry(libmedia, media, lib_open, NULL, NULL);
    java_method_register(&media_entry);
}
