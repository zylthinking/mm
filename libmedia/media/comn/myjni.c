
#include "myjni.h"

static JavaVM* jvm = NULL;
static struct java_mehods* methods = NULL;
void java_method_register(struct java_mehods* method)
{
    method->next = methods;
    methods = method;
}

static int32_t register_ndk_method(JNIEnv* env, struct java_mehods* method)
{
    if (method->nr == 0) {
        return 0;
    }

    jclass clazz = (*env)->FindClass(env, method->cls);
    if (clazz == NULL) {
        return -1;
    }

    int n = (*env)->RegisterNatives(env, clazz, method->methods, method->nr);
    if (n < 0) {
        return -1;
    }
    return 0;
}

static int32_t unregister_ndk_method(JNIEnv* env, struct java_mehods* method)
{
    if (method->nr == 0) {
        return 0;
    }

    jclass clazz = (*env)->FindClass(env, method->cls);
    if (clazz == NULL) {
        return -1;
    }

    int n = (*env)->UnregisterNatives(env, clazz);
    if (n < 0) {
        return -1;
    }
    return 0;
}

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_4) != JNI_OK) {
        return JNI_ERR;
    }
    my_assert(env != NULL);
    jvm = vm;

    struct java_mehods *cur1 = NULL, *cur2 = NULL;
    for (cur1 = methods; cur1 != NULL; cur1 = cur1->next) {
        if (cur1->open != NULL && -1 == cur1->open(vm, env)) {
            break;
        }

        if (-1 == register_ndk_method(env, cur1)) {
            if (cur1->close != NULL) {
                cur1->close(vm, env);
            }
            break;
        }

        if (cur1->loaded != NULL) {
            cur1->loaded();
        }
    }

    if (cur1 == NULL) {
        return JNI_VERSION_1_4;
    }

    for (cur2 = methods; cur2 != cur1; cur2 = cur2->next) {
        unregister_ndk_method(env, cur2);
        if (cur2->close != NULL) {
            cur2->close(vm, env);
        }
    }
    return JNI_ERR;
}

void jni_put(java_code* jcode)
{
    if (jcode->attached == 1) {
        (*jvm)->DetachCurrentThread(jvm);
        jcode->attached = 0;
    }
}

JNIEnv* jni_get(java_code* jcode, jclass clazz)
{
    JNIEnv* env = NULL;
    jint n = (*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_4);
    if (n == JNI_EDETACHED) {
        n = (*jvm)->AttachCurrentThread(jvm, (JNIEnv**) &env, NULL);
        if (n != JNI_OK) {
            return NULL;
        }
        jcode->attached = 1;
    }

    if (n != JNI_OK) {
        return NULL;
    }

    if (jcode->code[0] != NULL && jcode->code[1] != NULL && jcode->code[2] != NULL) {
        if (jcode->clazz == NULL) {
            if (clazz == NULL) {
                clazz = (*env)->FindClass(env, jcode->code[0]);
                if (clazz == NULL) {
                    jni_put(jcode);
                    return NULL;
                }
                clazz = (*env)->NewGlobalRef(env, clazz);
            }
            jcode->clazz = clazz;
        }

        if (jcode->id == NULL) {
            jmethodID mid = (*env)->GetStaticMethodID(env, jcode->clazz, jcode->code[1], jcode->code[2]);
            if (mid == NULL) {
                jni_put(jcode);
                return NULL;
            }
            jcode->id = mid;
        }
    }
    return env;
}
