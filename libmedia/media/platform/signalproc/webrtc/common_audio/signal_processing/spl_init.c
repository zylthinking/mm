/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/* The global function contained in this file initializes SPL function
 * pointers, currently only for ARM platforms.
 *
 * Some code came from common/rtcd.c in the WebM project.
 */

#include "webrtc/common_audio/signal_processing/include/real_fft.h"
#include "webrtc/common_audio/signal_processing/include/signal_processing_library.h"
#include "webrtc/system_wrappers/interface/cpu_features_wrapper.h"

/* Declare function pointers. */
MaxAbsValueW16 WebRtcSpl_MaxAbsValueW16;
MaxAbsValueW32 WebRtcSpl_MaxAbsValueW32;
MaxValueW16 WebRtcSpl_MaxValueW16;
MaxValueW32 WebRtcSpl_MaxValueW32;
MinValueW16 WebRtcSpl_MinValueW16;
MinValueW32 WebRtcSpl_MinValueW32;
CrossCorrelation WebRtcSpl_CrossCorrelation;
DownsampleFast WebRtcSpl_DownsampleFast;
ScaleAndAddVectorsWithRound WebRtcSpl_ScaleAndAddVectorsWithRound;
CreateRealFFT WebRtcSpl_CreateRealFFT;
FreeRealFFT WebRtcSpl_FreeRealFFT;
RealForwardFFT WebRtcSpl_RealForwardFFT;
RealInverseFFT WebRtcSpl_RealInverseFFT;

#if (defined(WEBRTC_DETECT_ARM_NEON) || (0 == WEBRTC_ARCH_ARM_NEON))
/* Initialize function pointers to the generic C version. */
static void InitPointersToC()
{
    WebRtcSpl_MaxAbsValueW16 = WebRtcSpl_MaxAbsValueW16C;
    WebRtcSpl_MaxAbsValueW32 = WebRtcSpl_MaxAbsValueW32C;
    WebRtcSpl_MaxValueW16 = WebRtcSpl_MaxValueW16C;
    WebRtcSpl_MaxValueW32 = WebRtcSpl_MaxValueW32C;
    WebRtcSpl_MinValueW16 = WebRtcSpl_MinValueW16C;
    WebRtcSpl_MinValueW32 = WebRtcSpl_MinValueW32C;
    WebRtcSpl_CrossCorrelation = WebRtcSpl_CrossCorrelationC;
    WebRtcSpl_DownsampleFast = WebRtcSpl_DownsampleFastC;
    WebRtcSpl_ScaleAndAddVectorsWithRound =
        WebRtcSpl_ScaleAndAddVectorsWithRoundC;
    WebRtcSpl_CreateRealFFT = WebRtcSpl_CreateRealFFTC;
    WebRtcSpl_FreeRealFFT = WebRtcSpl_FreeRealFFTC;
    WebRtcSpl_RealForwardFFT = WebRtcSpl_RealForwardFFTC;
    WebRtcSpl_RealInverseFFT = WebRtcSpl_RealInverseFFTC;
}
#endif

#if defined(WEBRTC_DETECT_ARM_NEON) || (1 == WEBRTC_ARCH_ARM_NEON)
/* Initialize function pointers to the Neon version. */
static void InitPointersToNeon()
{
    WebRtcSpl_MaxAbsValueW16 = WebRtcSpl_MaxAbsValueW16Neon;
    WebRtcSpl_MaxAbsValueW32 = WebRtcSpl_MaxAbsValueW32Neon;
    WebRtcSpl_MaxValueW16 = WebRtcSpl_MaxValueW16Neon;
    WebRtcSpl_MaxValueW32 = WebRtcSpl_MaxValueW32Neon;
    WebRtcSpl_MinValueW16 = WebRtcSpl_MinValueW16Neon;
    WebRtcSpl_MinValueW32 = WebRtcSpl_MinValueW32Neon;
    WebRtcSpl_CrossCorrelation = WebRtcSpl_CrossCorrelationNeon;
    WebRtcSpl_DownsampleFast = WebRtcSpl_DownsampleFastNeon;
    WebRtcSpl_ScaleAndAddVectorsWithRound =
        WebRtcSpl_ScaleAndAddVectorsWithRoundNeon;
    WebRtcSpl_CreateRealFFT = WebRtcSpl_CreateRealFFTNeon;
    WebRtcSpl_FreeRealFFT = WebRtcSpl_FreeRealFFTNeon;
    WebRtcSpl_RealForwardFFT = WebRtcSpl_RealForwardFFTNeon;
    WebRtcSpl_RealInverseFFT = WebRtcSpl_RealInverseFFTNeon;
}
#endif

static void InitFunctionPointers(void)
{
#if defined(WEBRTC_DETECT_ARM_NEON)
    if ((WebRtc_GetCPUFeaturesARM() & kCPUFeatureNEON) != 0) {
        InitPointersToNeon();
    } else {
        InitPointersToC();
    }
#elif (1 == WEBRTC_ARCH_ARM_NEON)
    InitPointersToNeon();
#else
    InitPointersToC();
#endif  /* WEBRTC_DETECT_ARM_NEON */
}

#if defined(WEBRTC_POSIX)
#include <pthread.h>

static void once(void (*func)(void))
{
    static pthread_once_t lock = PTHREAD_ONCE_INIT;
    pthread_once(&lock, func);
}

#elif defined(_WIN32)
#include <windows.h>

static void once(void (*func)(void))
{
    /* Didn't use InitializeCriticalSection() since there's no race-free context
     * in which to execute it.
     *
     * TODO(kma): Change to different implementation (e.g.
     * InterlockedCompareExchangePointer) to avoid issues similar to
     * http://code.google.com/p/webm/issues/detail?id=467.
     */
    static CRITICAL_SECTION lock = {(void *)((size_t)-1), -1, 0, 0, 0, 0};
    static int done = 0;

    EnterCriticalSection(&lock);
    if (!done) {
        func();
        done = 1;
    }
    LeaveCriticalSection(&lock);
}

/* There's no fallback version as an #else block here to ensure thread safety.
 * In case of neither pthread for WEBRTC_POSIX nor _WIN32 is present, build
 * system should pick it up.
 */
#endif  /* WEBRTC_POSIX */

void WebRtcSpl_Init()
{
    once(InitFunctionPointers);
}
