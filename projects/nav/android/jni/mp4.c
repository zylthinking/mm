
#include "myjni.h"
#include "mydef.h"
#include "writer.h"
#include "mfio_mp4.h"
#include "fmt.h"
#include "media_buffer.h"

static void* mp4_begin(const char* file, media_iden_t iden, media_buffer* video)
{
    static void* ptrs[3] = {NULL, NULL, NULL};

    audio_format* afmt = &aac_8k16b1;
    media_buffer audio;
    audio.pptr_cc = &afmt->cc;

    ptrs[0] = writer_open(&mp4_mfio_writer, file, &audio, video);
    if (ptrs[0] == NULL) {
        return NULL;
    }

    ptrs[1] = add_stream(&mp4_mfio_writer, ptrs[0], iden, audio_type | video_type, 0);
    if (ptrs[1] == NULL) {
        writer_close(&mp4_mfio_writer, ptrs[0]);
        ptrs[0] = NULL;
        return NULL;
    }

    ptrs[2] = add_stream(&mp4_mfio_writer, ptrs[0], media_id_self, audio_type, 0);
    if (ptrs[2] == NULL) {
        delete_stream(ptrs[1]);
        writer_close(&mp4_mfio_writer, ptrs[0]);
        ptrs[0] = NULL;
        return NULL;
    }
    return ptrs;
}

static jlong jni_mp4_begin(JNIEnv* env, jclass clazz, jbyteArray file, jlong uid, jlong cid, jlong video)
{
    media_iden_t iden = make_media_id((uint32_t) cid, (uint64_t) uid);
    media_buffer media;
    media.pptr_cc = (fourcc **) (intptr_t) video;

    uint8_t buf[1024];
    jsize bytes = (*env)->GetArrayLength(env, file);
    assert(bytes < sizeof(buf));
    (*env)->GetByteArrayRegion(env, file, 0, bytes, (jbyte *) buf);
    buf[bytes] = 0;

    return (jlong) (intptr_t) mp4_begin(buf, iden, &media);
}

static void jni_mp4_end(JNIEnv* env, jclass clazz, jlong mp4)
{
    void** ptrs = (void **) (intptr_t) mp4;
    delete_stream(ptrs[2]);
    delete_stream(ptrs[1]);
    writer_close(&mp4_mfio_writer, ptrs[0]);
}

static JNINativeMethod mp4_methods[] = {
    {
        .name = "mp4_begin",
        .signature = "([BJJJ)J",
        .fnPtr = jni_mp4_begin
    },

    {
        .name = "mp4_end",
        .signature = "(J)V",
        .fnPtr = jni_mp4_end
    },
};

__attribute__((constructor)) static void mp4_register()
{
    static struct java_mehods mp4_entry = methods_entry(libmm, mp4, NULL, NULL, NULL);
    java_method_register(&mp4_entry);
}
