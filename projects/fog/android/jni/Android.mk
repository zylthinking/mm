
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../../../libmedia/media
LOCAL_MODULE := libavformat_armv7
LOCAL_SRC_FILES := $(MEDIA_DIR)/ffmpeg/android/libavformat.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../../../libmedia/media
LOCAL_MODULE := libavcodec_armv7
LOCAL_SRC_FILES := $(MEDIA_DIR)/ffmpeg/android/libavcodec.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../../../libmedia/media
LOCAL_MODULE := libavutil_armv7
LOCAL_SRC_FILES := $(MEDIA_DIR)/ffmpeg/android/libavutil.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../../../libmedia/media

#LOCAL_ARM_MODE := arm
LOCAL_MODULE := libfog
LOCAL_SRC_FILES := \
./mmapi.c \
../../ff_buffer.c \
../../filefmt.c \
../../fileit.c \
../../ffmpegfile.c \
../../../../libmedia/media/comn/myjni.c

LOCAL_C_INCLUDES := ../..
LOCAL_C_INCLUDES += $(MEDIA_DIR)
LOCAL_C_INCLUDES += $(MEDIA_DIR)/comn
LOCAL_C_INCLUDES += $(MEDIA_DIR)/ffmpeg/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/frontend
LOCAL_C_INCLUDES += $(MEDIA_DIR)/mmapi
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform/android

LOCAL_CONLYFLAGS += -std=c99
LOCAL_CFLAGS += -Wno-multichar -marm -rdynamic -fPIC -flax-vector-conversions -Dasm=__asm__
LOCAL_CFLAGS += -mfpu=neon -march=armv7-a -mfloat-abi=softfp

ifeq ($(APP_OPTIM),release)
    LOCAL_CFLAGS += -DNDEBUG -O3
else
    cmd-strip :=
    LOCAL_CFLAGS += -DDEBUG -O0 -ggdb
endif

LOCAL_STATIC_LIBRARIES := libavformat_armv7 libavcodec_armv7 libavutil_armv7
LOCAL_LDFLAGS := -L$(MEDIA_DIR)/../android/libs/armeabi-v7a/ -lmedia2 -llog
include $(BUILD_SHARED_LIBRARY)
