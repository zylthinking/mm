
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../../../libmedia/media

#LOCAL_ARM_MODE := arm
LOCAL_MODULE := libmm
LOCAL_SRC_FILES := \
./mmapi.c \
../../nav.c \
../../mmv.c \
../../mediafile/mediafile.c \
../../netjointor/media_addr.c \
../../netjointor/net/proto/packet.c \
../../netjointor/net/proto/proto.c \
../../netjointor/net/resendbuf.c \
../../netjointor/net/tcp.c \
../../netjointor/net/udp.c \
../../netjointor/net/address.c \
../../netjointor/netjointor.c \
../../netjointor/preset.c \
../../hook/soundtouch/sound_proc.cpp \
../../hook/soundtouch/src/AAFilter.cpp \
../../hook/soundtouch/src/BPMDetect.cpp \
../../hook/soundtouch/src/cpu_detect_x86.cpp \
../../hook/soundtouch/src/FIFOSampleBuffer.cpp \
../../hook/soundtouch/src/FIRFilter.cpp \
../../hook/soundtouch/src/InterpolateCubic.cpp \
../../hook/soundtouch/src/InterpolateLinear.cpp \
../../hook/soundtouch/src/InterpolateShannon.cpp \
../../hook/soundtouch/src/mmx_optimized.cpp \
../../hook/soundtouch/src/PeakFinder.cpp \
../../hook/soundtouch/src/RateTransposer.cpp \
../../hook/soundtouch/src/SoundTouch.cpp \
../../hook/soundtouch/src/sse_optimized.cpp \
../../hook/soundtouch/src/TDStretch.cpp \
$(MEDIA_DIR)/comn/fdset.c \
$(MEDIA_DIR)/comn/utils.c \
$(MEDIA_DIR)/comn/myjni.c

LOCAL_C_INCLUDES := ../..
LOCAL_C_INCLUDES += ../../netjointor
LOCAL_C_INCLUDES += ../../netjointor/net/proto
LOCAL_C_INCLUDES += ../../mediafile
LOCAL_C_INCLUDES += ../../hook
LOCAL_C_INCLUDES += ../../hook/soundtouch
LOCAL_C_INCLUDES += ../../hook/soundtouch/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)
LOCAL_C_INCLUDES += $(MEDIA_DIR)/comn
LOCAL_C_INCLUDES += $(MEDIA_DIR)/mmapi
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform/android
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec
LOCAL_C_INCLUDES += $(MEDIA_DIR)/frontend
LOCAL_C_INCLUDES += $(MEDIA_DIR)/frontend/ramfile
LOCAL_C_INCLUDES += $(MEDIA_DIR)/frontend/capture

LOCAL_CONLYFLAGS += -std=c99
LOCAL_CFLAGS += -marm -rdynamic -fPIC -flax-vector-conversions -Dasm=__asm__
LOCAL_CFLAGS += -mfpu=neon -march=armv7-a -mfloat-abi=softfp

ifeq ($(APP_OPTIM),release)
    LOCAL_CFLAGS += -DNDEBUG -O3
else
    LOCAL_CFLAGS += -DDEBUG -O0 -ggdb
endif

LOCAL_LDFLAGS := -L$(MEDIA_DIR)/../android/libs/armeabi-v7a/ -lmedia2
include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../../../libmedia/media
LOCAL_MODULE := libavcodec_armv7
LOCAL_SRC_FILES := $(MEDIA_DIR)/ffmpeg/android/libavcodec.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../../../libmedia/media
LOCAL_MODULE := libavformat_armv7
LOCAL_SRC_FILES := $(MEDIA_DIR)/ffmpeg/android/libavformat.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../../../libmedia/media
LOCAL_MODULE := libavutil_armv7
LOCAL_SRC_FILES := $(MEDIA_DIR)/ffmpeg/android/libavutil.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../../../libmedia/media
LOCAL_MODULE := mp4
LOCAL_SRC_FILES :=  \
./mp4.c \
../../mediafile/mfio_mp4.c \
$(MEDIA_DIR)/platform/android/log.c \
$(MEDIA_DIR)/comn/backtrace.c \
$(MEDIA_DIR)/comn/now.c \
$(MEDIA_DIR)/comn/myjni.c

LOCAL_C_INCLUDES := ../../mediafile
LOCAL_C_INCLUDES += $(MEDIA_DIR)
LOCAL_C_INCLUDES += $(MEDIA_DIR)/comn
LOCAL_C_INCLUDES += $(MEDIA_DIR)/mmapi
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec
LOCAL_C_INCLUDES += $(MEDIA_DIR)/ffmpeg/include

LOCAL_CONLYFLAGS += -std=gnu99
LOCAL_CFLAGS := -fPIC
LOCAL_LDLIBS := -llog
LOCAL_STATIC_LIBRARIES := libavformat_armv7 libavutil_armv7 libavcodec_armv7
LOCAL_LDFLAGS := -L$(MEDIA_DIR)/../android/libs/armeabi-v7a/ -lmedia2
include $(BUILD_SHARED_LIBRARY)
