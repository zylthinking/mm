/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Performs delay estimation on binary converted spectra.
// The return value is  0 - OK and -1 - Error, unless otherwise stated.

#ifndef WEBRTC_MODULES_AUDIO_PROCESSING_UTILITY_DELAY_ESTIMATOR_H_
#define WEBRTC_MODULES_AUDIO_PROCESSING_UTILITY_DELAY_ESTIMATOR_H_

#include "webrtc/typedefs.h"

typedef struct {
    // Pointer to bit counts.
    int* far_bit_counts;
    // Binary history variables.
    uint32_t* binary_far_history;
    int history_size;
} BinaryDelayEstimatorFarend;

typedef struct {
    // Pointer to bit counts.
    int32_t* mean_bit_counts;
    // Array only used locally in ProcessBinarySpectrum() but whose size is
    // determined at run-time.
    int32_t* bit_counts;

    // Binary history variables.
    uint32_t* binary_near_history;
    int near_history_size;

    // Delay estimation variables.
    int32_t minimum_probability;
    int last_delay_probability;

    // Delay memory.
    int last_delay;

    // Robust validation
    int robust_validation_enabled;
    int allowed_offset;
    int last_candidate_delay;
    int compare_delay;
    int candidate_hits;
    float* histogram;
    float last_delay_histogram;

    // Far-end binary spectrum history buffer etc.
    BinaryDelayEstimatorFarend* farend;
} BinaryDelayEstimator;

// Releases the memory allocated by
// WebRtc_CreateBinaryDelayEstimatorFarend(...).
// Input:
//    - self              : Pointer to the binary delay estimation far-end
//                          instance which is the return value of
//                          WebRtc_CreateBinaryDelayEstimatorFarend().
//
void WebRtc_FreeBinaryDelayEstimatorFarend(BinaryDelayEstimatorFarend* self);

// Allocates the memory needed by the far-end part of the binary delay
// estimation. The memory needs to be initialized separately through
// WebRtc_InitBinaryDelayEstimatorFarend(...).
//
// Inputs:
//      - history_size    : Size of the far-end binary spectrum history.
//
// Return value:
//      - BinaryDelayEstimatorFarend*
//                        : Created |handle|. If the memory can't be allocated
//                          or if any of the input parameters are invalid NULL
//                          is returned.
//
BinaryDelayEstimatorFarend* WebRtc_CreateBinaryDelayEstimatorFarend(
    int history_size);

// Initializes the delay estimation far-end instance created with
// WebRtc_CreateBinaryDelayEstimatorFarend(...).
//
// Input:
//    - self              : Pointer to the delay estimation far-end instance.
//
// Output:
//    - self              : Initialized far-end instance.
//
void WebRtc_InitBinaryDelayEstimatorFarend(BinaryDelayEstimatorFarend* self);

// Adds the binary far-end spectrum to the internal far-end history buffer. This
// spectrum is used as reference when calculating the delay using
// WebRtc_ProcessBinarySpectrum().
//
// Inputs:
//    - self                  : Pointer to the delay estimation far-end
//                              instance.
//    - binary_far_spectrum   : Far-end binary spectrum.
//
// Output:
//    - self                  : Updated far-end instance.
//
void WebRtc_AddBinaryFarSpectrum(BinaryDelayEstimatorFarend* self,
                                 uint32_t binary_far_spectrum);

// Releases the memory allocated by WebRtc_CreateBinaryDelayEstimator(...).
//
// Note that BinaryDelayEstimator utilizes BinaryDelayEstimatorFarend, but does
// not take ownership of it, hence the BinaryDelayEstimator has to be torn down
// before the far-end.
//
// Input:
//    - self              : Pointer to the binary delay estimation instance
//                          which is the return value of
//                          WebRtc_CreateBinaryDelayEstimator().
//
void WebRtc_FreeBinaryDelayEstimator(BinaryDelayEstimator* self);

// Allocates the memory needed by the binary delay estimation. The memory needs
// to be initialized separately through WebRtc_InitBinaryDelayEstimator(...).
//
// Inputs:
//      - farend        : Pointer to the far-end part of the Binary Delay
//                        Estimator. This memory has to be created separately
//                        prior to this call using
//                        WebRtc_CreateBinaryDelayEstimatorFarend().
//
//                        Note that BinaryDelayEstimator does not take
//                        ownership of |farend|.
//
//      - lookahead     : Amount of non-causal lookahead to use. This can
//                        detect cases in which a near-end signal occurs before
//                        the corresponding far-end signal. It will delay the
//                        estimate for the current block by an equal amount,
//                        and the returned values will be offset by it.
//
//                        A value of zero is the typical no-lookahead case.
//                        This also represents the minimum delay which can be
//                        estimated.
//
//                        Note that the effective range of delay estimates is
//                        [-|lookahead|,... ,|history_size|-|lookahead|)
//                        where |history_size| was set upon creating the far-end
//                        history buffer size.
//
// Return value:
//      - BinaryDelayEstimator*
//                        : Created |handle|. If the memory can't be allocated
//                          or if any of the input parameters are invalid NULL
//                          is returned.
//
BinaryDelayEstimator* WebRtc_CreateBinaryDelayEstimator(
    BinaryDelayEstimatorFarend* farend, int lookahead);

// Initializes the delay estimation instance created with
// WebRtc_CreateBinaryDelayEstimator(...).
//
// Input:
//    - self              : Pointer to the delay estimation instance.
//
// Output:
//    - self              : Initialized instance.
//
void WebRtc_InitBinaryDelayEstimator(BinaryDelayEstimator* self);

// Estimates and returns the delay between the binary far-end and binary near-
// end spectra. It is assumed the binary far-end spectrum has been added using
// WebRtc_AddBinaryFarSpectrum() prior to this call. The value will be offset by
// the lookahead (i.e. the lookahead should be subtracted from the returned
// value).
//
// Inputs:
//    - self                  : Pointer to the delay estimation instance.
//    - binary_near_spectrum  : Near-end binary spectrum of the current block.
//
// Output:
//    - self                  : Updated instance.
//
// Return value:
//    - delay                 :  >= 0 - Calculated delay value.
//                              -2    - Insufficient data for estimation.
//
int WebRtc_ProcessBinarySpectrum(BinaryDelayEstimator* self,
                                 uint32_t binary_near_spectrum);

// Returns the last calculated delay updated by the function
// WebRtc_ProcessBinarySpectrum(...).
//
// Input:
//    - self                  : Pointer to the delay estimation instance.
//
// Return value:
//    - delay                 :  >= 0 - Last calculated delay value
//                              -2    - Insufficient data for estimation.
//
int WebRtc_binary_last_delay(BinaryDelayEstimator* self);

// Returns the estimation quality of the last calculated delay updated by the
// function WebRtc_ProcessBinarySpectrum(...). The estimation quality is a value
// in the interval [0, 1] in Q14. The higher the value, the better quality.
//
// Input:
//    - self                  : Pointer to the delay estimation instance.
//
// Return value:
//    - delay_quality         :  >= 0 - Estimation quality (in Q14) of last
//                                      calculated delay value.
//                              -2    - Insufficient data for estimation.
//
int WebRtc_binary_last_delay_quality(BinaryDelayEstimator* self);

// Updates the |mean_value| recursively with a step size of 2^-|factor|. This
// function is used internally in the Binary Delay Estimator as well as the
// Fixed point wrapper.
//
// Inputs:
//    - new_value             : The new value the mean should be updated with.
//    - factor                : The step size, in number of right shifts.
//
// Input/Output:
//    - mean_value            : Pointer to the mean value.
//
void WebRtc_MeanEstimatorFix(int32_t new_value,
                             int factor,
                             int32_t* mean_value);


#endif  // WEBRTC_MODULES_AUDIO_PROCESSING_UTILITY_DELAY_ESTIMATOR_H_
