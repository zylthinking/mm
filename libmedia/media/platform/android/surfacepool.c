
#include "myjni.h"
#include "mydef.h"
#include "my_handle.h"
#include "win.h"
#include "area.h"
#include "android.h"
#include <android/native_window_jni.h>
#include <android/native_window.h>

static java_code code[3] = {
    {{"libmedia/surfacepool", "area_open",  "(JJJJ)[J"}, NULL, NULL, 0},
    {{"libmedia/surfacepool", "area_close", "(JJJ)I"}, NULL, NULL, 0},
    {{"libmedia/surfacepool", "area_reopen", "(JJJJJ)[J"}, NULL, NULL, 0},
};

static jclass surfacepool_clszz = NULL;
static int32_t lib_open(JavaVM* vm, JNIEnv* env)
{
    surfacepool_clszz = (*env)->FindClass(env, "libmedia/surfacepool");
    if (surfacepool_clszz == NULL) {
        return -1;
    }
    surfacepool_clszz = (*env)->NewGlobalRef(env, surfacepool_clszz);
    return 0;
}

intptr_t open_area(area_t* area)
{
    JNIEnv* env = jni_get(&code[0], surfacepool_clszz);
    if (env == NULL) {
        return -1;
    }
    intptr_t n = -1;

    jlongArray longs = (jlongArray) (*env)->CallStaticObjectMethod(env, code[0].clazz, code[0].id,
                                                (jlong) area->identy.cid, (jlong) area->identy.uid, (jlong) area->w, (jlong) area->h);
    if (longs != NULL) {
        jlong xywhz[6];
        (*env)->GetLongArrayRegion(env, longs, 0, 6, xywhz);
        area->x = (uintptr_t) xywhz[0];
        area->y = (uintptr_t) xywhz[1];
        area->w = (uintptr_t) xywhz[2];
        area->h = (uintptr_t) xywhz[3];
        area->z = (uintptr_t) xywhz[4];
        area->egl = (void *) (uintptr_t) xywhz[5];
        (*env)->DeleteLocalRef(env, longs);
        n = 0;
    }

    jni_put(&code[0]);
    return n;
}

void reopen_area(area_t* area, uintptr_t width, uintptr_t height)
{
    JNIEnv* env = jni_get(&code[2], surfacepool_clszz);
    if (env == NULL) {
        my_assert(0);
        return;
    }

    jlongArray longs = (jlongArray) (*env)->CallStaticObjectMethod(env, code[2].clazz, code[2].id,
                                                (jlong) area->z, (jlong) area->w, (jlong) area->h, (jlong) width, (jlong) height);
    my_assert(longs != NULL);

    jlong xywhz[6];
    (*env)->GetLongArrayRegion(env, longs, 0, 5, xywhz);
    area->x = (uintptr_t) xywhz[0];
    area->y = (uintptr_t) xywhz[1];
    area->w = (uintptr_t) xywhz[2];
    area->h = (uintptr_t) xywhz[3];
    area->z = (uintptr_t) xywhz[4];
    (*env)->DeleteLocalRef(env, longs);

    jni_put(&code[2]);
}

intptr_t close_area(area_t* area)
{
    JNIEnv* env = jni_get(&code[1], surfacepool_clszz);
    if (env == NULL) {
        return -1;
    }

    intptr_t n = (*env)->CallStaticIntMethod(env, code[1].clazz, code[1].id,
                                (jlong) area->identy.cid, (jlong) area->identy.uid, (jlong) (intptr_t) area->egl);
    jni_put(&code[1]);
    return n;
}

static jlong jni_handle_reference(JNIEnv* env, jclass clazz, jlong handle)
{
    handle_clone((my_handle *) (intptr_t) handle);
    return handle;
}

static void jni_handle_dereference(JNIEnv* env, jclass clazz, jlong handle)
{
    handle_release((my_handle *) (intptr_t) handle);
}

static jlong jni_surface_register(JNIEnv* env, jclass clazz, jint r, jint g, jint b, jint w, jint h, jobject jsurface)
{
    ANativeWindow* android_window = ANativeWindow_fromSurface(env, jsurface);
    if (android_window == NULL) {
        return 0;
    }

    float red = ((float) r) / 255;
    float green = ((float) g) / 255;
    float blue = ((float) b) / 255;

    extern eglop_t eglop;
    my_handle* handle = window_create(red, green, blue, 1.0, w, h, (void *) android_window, &eglop);
    if (handle == NULL) {
        ANativeWindow_release(android_window);
    }
    return (jlong) (intptr_t) handle;
}

static void jni_surface_unregister(JNIEnv* env, jclass clazz, jlong handle)
{
    window_t* win = (window_t *) handle_get((my_handle *) (intptr_t) handle);
    my_assert(win != NULL);
    win->killed = 1;
    handle_put((my_handle *) (intptr_t) handle);
    handle_release((my_handle *) (intptr_t) handle);
}

static void jni_surface_resize(JNIEnv* env, jclass clazz, jlong handle, jlong width, jlong height)
{
    window_t* win = (window_t *) handle_get((my_handle *) (intptr_t) handle);
    my_assert(win != NULL);

    int64_t* feature = android_feature_address();
    if (feature[bug_mask] & bug_gl_no_resize) {
        width = win->egl.w;
        height = win->egl.h;
    }
    window_resize(win, width, height);
    handle_put((my_handle *) (intptr_t) handle);
}

static JNINativeMethod surfacepool_methods[] = {
    {
        .name = "handle_reference",
        .signature = "(J)J",
        .fnPtr = jni_handle_reference
    },

    {
        .name = "handle_dereference",
        .signature = "(J)V",
        .fnPtr = jni_handle_dereference
    },

    {
        .name = "surface_register",
        .signature = "(IIIIILjava/lang/Object;)J",
        .fnPtr = jni_surface_register
    },

    {
        .name = "surface_unregister",
        .signature = "(J)V",
        .fnPtr = jni_surface_unregister
    },

    {
        .name = "surface_resize",
        .signature = "(JJJ)V",
        .fnPtr = jni_surface_resize
    },
};

__attribute__((constructor)) static void surfacepool_register()
{
    static struct java_mehods media_entry = methods_entry(libmedia, surfacepool, lib_open, NULL, NULL);
    java_method_register(&media_entry);
}

