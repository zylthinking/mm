/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_SYSTEM_WRAPPERS_INTERFACE_ASM_DEFINES_H_
#define WEBRTC_SYSTEM_WRAPPERS_INTERFACE_ASM_DEFINES_H_

#if defined(__arm64__)
    #define WEBRTC_ARCH_ARM_V7 0
#elif defined (__ARM_ARCH_7A__)
    #define WEBRTC_ARCH_ARM_V7 1
    #if defined (__ARM_NEON__)
        #define WEBRTC_ARCH_ARM_NEON 1
    #else
        #define WEBRTC_ARCH_ARM_NEON 0
    #endif
#else
    #define WEBRTC_ARCH_ARM_V7 0
    #define WEBRTC_ARCH_ARM_NEON 0
#endif

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif

// With Apple's clang compiler, for instructions ldrb, strh, etc.,
// the condition code is after the width specifier. Here we define
// only the ones that are actually used in the assembly files.
#if (defined __clang__) && (defined __APPLE__)
.macro streqh reg1, reg2, num
strheq \reg1, \reg2, \num
.endm
#endif

.text

#endif  // WEBRTC_SYSTEM_WRAPPERS_INTERFACE_ASM_DEFINES_H_
