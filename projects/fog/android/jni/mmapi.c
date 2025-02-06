
#include "myjni.h"
#include "mydef.h"
#include "mem.h"
#include "my_handle.h"
#include "filefmt.h"

typedef struct {
    int nr[2];
    void* handle;
} supplier_t;

static intptr_t notify(void* uptr, intptr_t type, void* any)
{
    my_handle* handle = (my_handle *) uptr;
    supplier_t* supplier = (supplier_t *) handle_get(handle);
    if (supplier == NULL) {
        return -1;
    }

    supplier->nr[1] += 1;
    futex(&supplier->nr[1], FUTEX_WAKE, 1, NULL, NULL, 0);
    handle_put(handle);
    return 0;
}

static void supplier_free(void* addr)
{
    supplier_t* supplier = (supplier_t *) addr;
    if (supplier->handle != NULL) {
        filefmt_close(supplier->handle);
    }
    my_free(addr);
}

static int64_t jni_supplier_open(JNIEnv* env, jclass clazz, jbyteArray uri, jlong seekto, jlong sync, jlong latch0, jlong latch1)
{
    uint8_t buf[1024];
    jsize bytes = (*env)->GetArrayLength(env, uri);
    my_assert(bytes < sizeof(buf));
    (*env)->GetByteArrayRegion(env, uri, 0, bytes, (jbyte *) buf);
    buf[bytes] = 0;

    supplier_t* supplier = (supplier_t *) my_malloc(sizeof(supplier_t));
    if (supplier == NULL) {
        logmsg("failed to alloc supplier\n");
        return 0;
    }
    memset(supplier, 0, sizeof(supplier_t));

    my_handle* handle = handle_attach(supplier, supplier_free);
    if (handle == NULL) {
        my_free(supplier);
        return 0;
    }

    handle_clone(handle);
    callback_t callback;
    callback.uptr = handle;
    callback.uptr_put = (free_t) handle_release;
    callback.notify = notify;

    file_mode_t mode;
    mode.seekto = (uint64_t) seekto;
    mode.sync = (uintptr_t) sync;
    mode.latch[0] = (uintptr_t) latch0;
    mode.latch[1] = (uintptr_t) latch1;
    supplier->handle = filefmt_open(buf, ftype_auto, &callback, &mode);
    if (supplier->handle == NULL) {
        handle_release(handle);
        handle_dettach(handle);
        return 0;
    }
    return (int64_t) (intptr_t) handle;
}

static jlongArray jni_supplier_context(JNIEnv* env, jclass clazz, jlong something)
{
    uint64_t intp[4];
    my_handle* handle = (my_handle *) (intptr_t) something;
    supplier_t* supplier = (supplier_t *) handle_get(handle);
    if (supplier == NULL) {
        return NULL;
    }

    fcontext_t* fctx = fcontext_get(supplier->handle);
    if (fctx == NULL) {
        handle_put(handle);
        return NULL;
    }

    intp[0] = fctx->have;
    intp[1] = fctx->more;
    intp[2] = fctx->cursor;
    intp[3] = fctx->duration;
    handle_put(handle);

    jlongArray longs = (*env)->NewLongArray(env, 4);
    if (longs != NULL) {
        (*env)->SetLongArrayRegion(env, longs, 0, 4, (jlong *) intp);
    }
    return longs;
}

static jlong jni_supplier_seek(JNIEnv* env, jclass clazz, jlong something, jlong pos)
{
    uint64_t intp[4];
    my_handle* handle = (my_handle *) (intptr_t) something;
    supplier_t* supplier = (supplier_t *) handle_get(handle);
    if (supplier == NULL) {
        return -1;
    }

    jlong n = filefmt_seek(supplier->handle, (uintptr_t) pos);
    handle_put(handle);
    return n;
}

static jlong jni_supplier_wait_get(JNIEnv* env, jclass clazz, jlong something)
{
    my_handle* handle = (my_handle *) (intptr_t) something;
    handle_clone(handle);
    return something;
}

static jint jni_supplier_wait(JNIEnv* env, jclass clazz, jlong something)
{
    my_handle* handle = (my_handle *) (intptr_t) something;
    supplier_t* supplier = (supplier_t *) handle_get(handle);
    if (supplier == NULL) {
        return -1;
    }

    int nr1 = supplier->nr[1];
    int nr0 = __sync_lock_test_and_set(&supplier->nr[0], nr1);
    if (nr0 != nr1) {
        handle_put(handle);
        return 0;
    }

    do {
        nr0 = futex(&supplier->nr[1], FUTEX_WAIT, nr1, NULL, NULL, 0);
    } while (nr0 == -1);

    handle_put(handle);
    return 0;
}

static void jni_supplier_wait_put(JNIEnv* env, jclass clazz, jlong something)
{
    my_handle* handle = (my_handle *) (intptr_t) something;
    handle_release(handle);
}

static void jni_supplier_close(JNIEnv* env, jclass clazz, jlong something)
{
    my_handle* handle = (my_handle *) (intptr_t) something;
    supplier_t* supplier = (supplier_t *) handle_get(handle);
    my_assert(supplier != NULL);
    handle_clone(handle);
    handle_dettach(handle);

    // if jni_supplier_wait has observed the fllowing line
    // then it will return before FUTEX_WAIT.
    // if it passed the test, then means nr1 will be different with
    // supplier->nr[1] after the assignment. And FUTEX_WAIT will
    // return immediately if it FUTEX_WAIT after the assignment.
    // If FUTEX_WAIT happens before the assignment, then FUTEX_WAKE
    // after the assignment will wake it up.
    // While, this only take effect when there is only one thread
    // in jni_supplier_wait, if more than one, the 2nd one will pass
    // the if test in jni_supplier_wait because the 1st one assign it.
    // then the nr1 maybe never changed again, and it maybe FUTEX_WAIT
    // after all FUTEX_WAKE, then the thread will be blocked forever.
    supplier->nr[1] += 1;
    futex(&supplier->nr[1], FUTEX_WAKE, 128, NULL, NULL, 0);
    handle_put(handle);
    handle_release(handle);
}

static JNINativeMethod fogapi_methods[] = {
    {
        .name = "supplier_open",
        .signature = "([BJJJJ)J",
        .fnPtr = jni_supplier_open
    },

    {
        .name = "supplier_context",
        .signature = "(J)[J",
        .fnPtr = jni_supplier_context
    },

    {
        .name = "supplier_seek",
        .signature = "(JJ)J",
        .fnPtr = jni_supplier_seek
    },

    {
        .name = "supplier_wait_get",
        .signature = "(J)J",
        .fnPtr = jni_supplier_wait_get
    },

    {
        .name = "supplier_wait_put",
        .signature = "(J)V",
        .fnPtr = jni_supplier_wait_put
    },

    {
        .name = "supplier_wait",
        .signature = "(J)I",
        .fnPtr = jni_supplier_wait
    },

    {
        .name = "supplier_close",
        .signature = "(J)V",
        .fnPtr = jni_supplier_close
    }
};

__attribute__((constructor)) static void fogapi_register()
{
    static struct java_mehods fogapi_entry = methods_entry(libmm, fogapi, NULL, NULL, NULL);
    java_method_register(&fogapi_entry);
}
