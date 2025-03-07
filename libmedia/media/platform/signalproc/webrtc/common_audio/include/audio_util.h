/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_COMMON_AUDIO_INCLUDE_AUDIO_UTIL_H_
#define WEBRTC_COMMON_AUDIO_INCLUDE_AUDIO_UTIL_H_

#include "webrtc/typedefs.h"

namespace webrtc
{

// Clamp the floating |value| to the range representable by an int16_t.
    static inline float ClampInt16(float value)
    {
        const float kMaxInt16 = 32767.f;
        const float kMinInt16 = -32768.f;
        return value < kMinInt16 ? kMinInt16 :
               (value > kMaxInt16 ? kMaxInt16 : value);
    }

// Round |value| to the closest int16.
    static inline int16_t RoundToInt16(float value)
    {
        return static_cast<int16_t>(
                   value > 0 ? (value >= 32766.5 ? 32767 : value + 0.5f)
                       : (value <= -32767.5 ? -32768 : value - 0.5f));
    }

// Round |size| elements of |src| to the closest int16 and writes to |dest|.
    static inline void RoundToInt16(const float* src, int size, int16_t* dest)
    {
        for (int i = 0; i < size; ++i)
            dest[i] = RoundToInt16(src[i]);
    }

// Deinterleave audio from |interleaved| to the channel buffers pointed to
// by |deinterleaved|. There must be sufficient space allocated in the
// |deinterleaved| buffers (|num_channel| buffers with |samples_per_channel|
// per buffer).
    void Deinterleave(const int16_t* interleaved, int samples_per_channel,
                      int num_channels, int16_t** deinterleaved);

// Interleave audio from the channel buffers pointed to by |deinterleaved| to
// |interleaved|. There must be sufficient space allocated in |interleaved|
// (|samples_per_channel| * |num_channels|).
    void Interleave(const int16_t* const* deinterleaved, int samples_per_channel,
                    int num_channels, int16_t* interleaved);

}  // namespace webrtc

#endif  // WEBRTC_COMMON_AUDIO_INCLUDE_AUDIO_UTIL_H_
