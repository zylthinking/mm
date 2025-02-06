LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libavcodec_armv7
LOCAL_SRC_FILES := ../../media/ffmpeg/android/libavcodec.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libavutil_armv7
LOCAL_SRC_FILES := ../../media/ffmpeg/android/libavutil.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libopus_armv7
LOCAL_SRC_FILES := ../../media/codec/opus/android/libopus.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libx264_armv7
LOCAL_SRC_FILES := ../../media/codec/h264/x264/android_libs/libx264.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libx265_armv7
LOCAL_SRC_FILES := ../../media/codec/h265/android.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
#LOCAL_ARM_MODE := arm
LOCAL_MODULE := libmedia2
LOCAL_CPP_EXTENSION := .cc .cpp
LOCAL_SRC_FILES :=  \
../../media/media_buffer.c \
../../media/sortcache.c \
../../media/fmt.c \
../../media/fmt_in.c \
../../media/comn/myjni.c \
../../media/comn/thread_clean.c \
../../media/comn/mbuf.c \
../../media/comn/mem.c \
../../media/comn/now.c \
../../media/comn/backtrace.c \
../../media/backend/drawer.c \
../../media/backend/render_jointor.c \
../../media/codec/codec.c \
../../media/codec/mpu.c \
../../media/codec/omx.c \
../../media/codec/opus/dec.c \
../../media/codec/silk/silk.c \
../../media/codec/silk/silkdec.c \
../../media/codec/silk/silkenc.c \
../../media/codec/silk/src/SKP_Silk_A2NLSF.c \
../../media/codec/silk/src/SKP_Silk_ana_filt_bank_1.c \
../../media/codec/silk/src/SKP_Silk_apply_sine_window_new.c \
../../media/codec/silk/src/SKP_Silk_array_maxabs.c \
../../media/codec/silk/src/SKP_Silk_autocorr.c \
../../media/codec/silk/src/SKP_Silk_biquad.c \
../../media/codec/silk/src/SKP_Silk_biquad_alt.c \
../../media/codec/silk/src/SKP_Silk_burg_modified.c \
../../media/codec/silk/src/SKP_Silk_bwexpander.c \
../../media/codec/silk/src/SKP_Silk_bwexpander_32.c \
../../media/codec/silk/src/SKP_Silk_CNG.c \
../../media/codec/silk/src/SKP_Silk_code_signs.c \
../../media/codec/silk/src/SKP_Silk_control_audio_bandwidth.c \
../../media/codec/silk/src/SKP_Silk_control_codec_FIX.c \
../../media/codec/silk/src/SKP_Silk_corrMatrix_FIX.c \
../../media/codec/silk/src/SKP_Silk_create_init_destroy.c \
../../media/codec/silk/src/SKP_Silk_dec_API.c \
../../media/codec/silk/src/SKP_Silk_decode_core.c \
../../media/codec/silk/src/SKP_Silk_decode_frame.c \
../../media/codec/silk/src/SKP_Silk_decode_parameters.c \
../../media/codec/silk/src/SKP_Silk_decode_pitch.c \
../../media/codec/silk/src/SKP_Silk_decode_pulses.c \
../../media/codec/silk/src/SKP_Silk_decoder_set_fs.c \
../../media/codec/silk/src/SKP_Silk_detect_SWB_input.c \
../../media/codec/silk/src/SKP_Silk_enc_API.c \
../../media/codec/silk/src/SKP_Silk_encode_frame_FIX.c \
../../media/codec/silk/src/SKP_Silk_encode_parameters.c \
../../media/codec/silk/src/SKP_Silk_encode_pulses.c \
../../media/codec/silk/src/SKP_Silk_find_LPC_FIX.c \
../../media/codec/silk/src/SKP_Silk_find_LTP_FIX.c \
../../media/codec/silk/src/SKP_Silk_find_pitch_lags_FIX.c \
../../media/codec/silk/src/SKP_Silk_find_pred_coefs_FIX.c \
../../media/codec/silk/src/SKP_Silk_gain_quant.c \
../../media/codec/silk/src/SKP_Silk_HP_variable_cutoff_FIX.c \
../../media/codec/silk/src/SKP_Silk_init_encoder_FIX.c \
../../media/codec/silk/src/SKP_Silk_inner_prod_aligned.c \
../../media/codec/silk/src/SKP_Silk_interpolate.c \
../../media/codec/silk/src/SKP_Silk_k2a.c \
../../media/codec/silk/src/SKP_Silk_k2a_Q16.c \
../../media/codec/silk/src/SKP_Silk_LBRR_reset.c \
../../media/codec/silk/src/SKP_Silk_lin2log.c \
../../media/codec/silk/src/SKP_Silk_log2lin.c \
../../media/codec/silk/src/SKP_Silk_LP_variable_cutoff.c \
../../media/codec/silk/src/SKP_Silk_LPC_inv_pred_gain.c \
../../media/codec/silk/src/SKP_Silk_LPC_synthesis_filter.c \
../../media/codec/silk/src/SKP_Silk_LPC_synthesis_order16.c \
../../media/codec/silk/src/SKP_Silk_LSF_cos_table.c \
../../media/codec/silk/src/SKP_Silk_LTP_analysis_filter_FIX.c \
../../media/codec/silk/src/SKP_Silk_LTP_scale_ctrl_FIX.c \
../../media/codec/silk/src/SKP_Silk_MA.c \
../../media/codec/silk/src/SKP_Silk_NLSF2A.c \
../../media/codec/silk/src/SKP_Silk_NLSF2A_stable.c \
../../media/codec/silk/src/SKP_Silk_NLSF_MSVQ_decode.c \
../../media/codec/silk/src/SKP_Silk_NLSF_MSVQ_encode_FIX.c \
../../media/codec/silk/src/SKP_Silk_NLSF_stabilize.c \
../../media/codec/silk/src/SKP_Silk_NLSF_VQ_rate_distortion_FIX.c \
../../media/codec/silk/src/SKP_Silk_NLSF_VQ_sum_error_FIX.c \
../../media/codec/silk/src/SKP_Silk_NLSF_VQ_weights_laroia.c \
../../media/codec/silk/src/SKP_Silk_noise_shape_analysis_FIX.c \
../../media/codec/silk/src/SKP_Silk_NSQ.c \
../../media/codec/silk/src/SKP_Silk_NSQ_del_dec.c \
../../media/codec/silk/src/SKP_Silk_pitch_analysis_core.c \
../../media/codec/silk/src/SKP_Silk_pitch_est_tables.c \
../../media/codec/silk/src/SKP_Silk_PLC.c \
../../media/codec/silk/src/SKP_Silk_prefilter_FIX.c \
../../media/codec/silk/src/SKP_Silk_process_gains_FIX.c \
../../media/codec/silk/src/SKP_Silk_process_NLSFs_FIX.c \
../../media/codec/silk/src/SKP_Silk_quant_LTP_gains_FIX.c \
../../media/codec/silk/src/SKP_Silk_range_coder.c \
../../media/codec/silk/src/SKP_Silk_regularize_correlations_FIX.c \
../../media/codec/silk/src/SKP_Silk_resampler.c \
../../media/codec/silk/src/SKP_Silk_resampler_down2.c \
../../media/codec/silk/src/SKP_Silk_resampler_down2_3.c \
../../media/codec/silk/src/SKP_Silk_resampler_down3.c \
../../media/codec/silk/src/SKP_Silk_resampler_private_AR2.c \
../../media/codec/silk/src/SKP_Silk_resampler_private_ARMA4.c \
../../media/codec/silk/src/SKP_Silk_resampler_private_copy.c \
../../media/codec/silk/src/SKP_Silk_resampler_private_down4.c \
../../media/codec/silk/src/SKP_Silk_resampler_private_down_FIR.c \
../../media/codec/silk/src/SKP_Silk_resampler_private_IIR_FIR.c \
../../media/codec/silk/src/SKP_Silk_resampler_private_up2_HQ.c \
../../media/codec/silk/src/SKP_Silk_resampler_private_up4.c \
../../media/codec/silk/src/SKP_Silk_resampler_rom.c \
../../media/codec/silk/src/SKP_Silk_resampler_up2.c \
../../media/codec/silk/src/SKP_Silk_residual_energy16_FIX.c \
../../media/codec/silk/src/SKP_Silk_residual_energy_FIX.c \
../../media/codec/silk/src/SKP_Silk_scale_copy_vector16.c \
../../media/codec/silk/src/SKP_Silk_scale_vector.c \
../../media/codec/silk/src/SKP_Silk_schur.c \
../../media/codec/silk/src/SKP_Silk_schur64.c \
../../media/codec/silk/src/SKP_Silk_shell_coder.c \
../../media/codec/silk/src/SKP_Silk_sigm_Q15.c \
../../media/codec/silk/src/SKP_Silk_solve_LS_FIX.c \
../../media/codec/silk/src/SKP_Silk_sort.c \
../../media/codec/silk/src/SKP_Silk_sum_sqr_shift.c \
../../media/codec/silk/src/SKP_Silk_tables_gain.c \
../../media/codec/silk/src/SKP_Silk_tables_LTP.c \
../../media/codec/silk/src/SKP_Silk_tables_NLSF_CB0_10.c \
../../media/codec/silk/src/SKP_Silk_tables_NLSF_CB0_16.c \
../../media/codec/silk/src/SKP_Silk_tables_NLSF_CB1_10.c \
../../media/codec/silk/src/SKP_Silk_tables_NLSF_CB1_16.c \
../../media/codec/silk/src/SKP_Silk_tables_other.c \
../../media/codec/silk/src/SKP_Silk_tables_pitch_lag.c \
../../media/codec/silk/src/SKP_Silk_tables_pulses_per_block.c \
../../media/codec/silk/src/SKP_Silk_tables_sign.c \
../../media/codec/silk/src/SKP_Silk_tables_type_offset.c \
../../media/codec/silk/src/SKP_Silk_VAD.c \
../../media/codec/silk/src/SKP_Silk_VQ_nearest_neighbor_FIX.c \
../../media/codec/silk/src/SKP_Silk_warped_autocorrelation_FIX.c \
../../media/codec/silk/src/SKP_Silk_A2NLSF_arm.S \
../../media/codec/silk/src/SKP_Silk_allpass_int_arm.S \
../../media/codec/silk/src/SKP_Silk_ana_filt_bank_1_arm.S \
../../media/codec/silk/src/SKP_Silk_array_maxabs_arm.S \
../../media/codec/silk/src/SKP_Silk_clz_arm.S \
../../media/codec/silk/src/SKP_Silk_decode_core_arm.S \
../../media/codec/silk/src/SKP_Silk_inner_prod_aligned_arm.S \
../../media/codec/silk/src/SKP_Silk_lin2log_arm.S \
../../media/codec/silk/src/SKP_Silk_MA_arm.S \
../../media/codec/silk/src/SKP_Silk_NLSF_VQ_sum_error_FIX_arm.S \
../../media/codec/silk/src/SKP_Silk_prefilter_FIX_arm.S \
../../media/codec/silk/src/SKP_Silk_resampler_down2_arm.S \
../../media/codec/silk/src/SKP_Silk_resampler_private_AR2_arm.S \
../../media/codec/silk/src/SKP_Silk_resampler_private_ARMA4_arm.S \
../../media/codec/silk/src/SKP_Silk_resampler_private_down_FIR_arm.S \
../../media/codec/silk/src/SKP_Silk_resampler_private_IIR_FIR_arm.S \
../../media/codec/silk/src/SKP_Silk_resampler_private_up2_HQ_arm.S \
../../media/codec/silk/src/SKP_Silk_resampler_rom_arm.S \
../../media/codec/silk/src/SKP_Silk_resampler_up2_arm.S \
../../media/codec/silk/src/SKP_Silk_schur64_arm.S \
../../media/codec/silk/src/SKP_Silk_sigm_Q15_arm.S \
../../media/codec/silk/src/SKP_Silk_sum_sqr_shift_arm.S \
../../media/codec/silk/src/SKP_Silk_warped_autocorrelation_FIX_arm.S \
../../media/codec/speex/libspeex/bits.c \
../../media/codec/speex/libspeex/cb_search.c \
../../media/codec/speex/libspeex/exc_10_16_table.c \
../../media/codec/speex/libspeex/exc_10_32_table.c \
../../media/codec/speex/libspeex/exc_20_32_table.c \
../../media/codec/speex/libspeex/exc_5_256_table.c \
../../media/codec/speex/libspeex/exc_5_64_table.c \
../../media/codec/speex/libspeex/exc_8_128_table.c \
../../media/codec/speex/libspeex/fftwrap.c \
../../media/codec/speex/libspeex/filterbank.c \
../../media/codec/speex/libspeex/filters.c \
../../media/codec/speex/libspeex/gain_table.c \
../../media/codec/speex/libspeex/gain_table_lbr.c \
../../media/codec/speex/libspeex/hexc_10_32_table.c \
../../media/codec/speex/libspeex/hexc_table.c \
../../media/codec/speex/libspeex/high_lsp_tables.c \
../../media/codec/speex/libspeex/jitter.c \
../../media/codec/speex/libspeex/kiss_fft.c \
../../media/codec/speex/libspeex/kiss_fftr.c \
../../media/codec/speex/libspeex/lpc.c \
../../media/codec/speex/libspeex/lsp.c \
../../media/codec/speex/libspeex/lsp_tables_nb.c \
../../media/codec/speex/libspeex/ltp.c \
../../media/codec/speex/libspeex/mdf.c \
../../media/codec/speex/libspeex/modes.c \
../../media/codec/speex/libspeex/modes_wb.c \
../../media/codec/speex/libspeex/nb_celp.c \
../../media/codec/speex/libspeex/preprocess.c \
../../media/codec/speex/libspeex/quant_lsp.c \
../../media/codec/speex/libspeex/resample.c \
../../media/codec/speex/libspeex/sb_celp.c \
../../media/codec/speex/libspeex/smallft.c \
../../media/codec/speex/libspeex/speex.c \
../../media/codec/speex/libspeex/speex_callbacks.c \
../../media/codec/speex/libspeex/speex_gverb.c \
../../media/codec/speex/libspeex/speex_header.c \
../../media/codec/speex/libspeex/stereo.c \
../../media/codec/speex/libspeex/vbr.c \
../../media/codec/speex/libspeex/vq.c \
../../media/codec/speex/libspeex/window.c \
../../media/codec/speex/speexdec.c \
../../media/codec/speex/resampler.c \
../../media/codec/aac/dec.c \
../../media/codec/aac/enc.c \
../../media/codec/h264/dec.c \
../../media/codec/h264/enc.c \
../../media/codec/h264/java.c \
../../media/codec/h264/nalu.c \
../../media/codec/h265/enc.c \
../../media/codec/h265/dec.c \
../../media/frontend/capture/audcap.c \
../../media/frontend/capture/vidcap.c \
../../media/frontend/file_jointor.c \
../../media/muxer/muxer.c \
../../media/platform/pts.c \
../../media/platform/shader.c \
../../media/platform/stage.c \
../../media/platform/win.c \
../../media/platform/android/log.c \
../../media/platform/android/mmapi.c \
../../media/platform/android/track.c \
../../media/platform/android/video.c \
../../media/platform/android/myegl.c \
../../media/platform/android/surfacepool.c \
../../media/platform/android/cpu-features.c \
../../media/platform/signalproc/filt.cpp \
../../media/platform/signalproc/sigproc.cpp \
../../media/platform/signalproc/webrtc/common_audio/audio_util.cc \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/auto_correlation.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/complex_fft.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/copy_set_operations.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/division_operations.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/dot_product_with_scale.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/energy.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/filter_ar.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/filter_ma_fast_q12.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/get_hanning_window.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/get_scaling_square.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/ilbc_specific_functions.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/levinson_durbin.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/lpc_to_refl_coef.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/randomization_functions.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/real_fft.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/refl_coef_to_lpc.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/resample.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/resample_48khz.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/resample_by_2.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/resample_by_2_internal.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/resample_fractional.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/spl_init.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/spl_sqrt.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/spl_version.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/splitting_filter.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/sqrt_of_one_minus_x_squared.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/min_max_operations.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/downsample_fast.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/cross_correlation.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/vector_scaling_operations.c \
../../media/platform/signalproc/webrtc/common_audio/vad/vad_core.c \
../../media/platform/signalproc/webrtc/common_audio/vad/vad_filterbank.c \
../../media/platform/signalproc/webrtc/common_audio/vad/vad_gmm.c \
../../media/platform/signalproc/webrtc/common_audio/vad/vad_sp.c \
../../media/platform/signalproc/webrtc/common_audio/vad/webrtc_vad.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/aec/aec_core.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/aec/aec_rdft.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/aec/aec_resampler.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/aec/echo_cancellation.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/aecm/aecm_core.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/aecm/aecm_core_c.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/aecm/echo_control_mobile.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/agc/analog_agc.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/agc/digital_agc.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/audio_buffer.cc \
../../media/platform/signalproc/webrtc/modules/audio_processing/audio_processing_impl.cc \
../../media/platform/signalproc/webrtc/modules/audio_processing/echo_cancellation_impl.cc \
../../media/platform/signalproc/webrtc/modules/audio_processing/echo_control_mobile_impl.cc \
../../media/platform/signalproc/webrtc/modules/audio_processing/gain_control_impl.cc \
../../media/platform/signalproc/webrtc/modules/audio_processing/high_pass_filter_impl.cc \
../../media/platform/signalproc/webrtc/modules/audio_processing/level_estimator_impl.cc \
../../media/platform/signalproc/webrtc/modules/audio_processing/noise_suppression_impl.cc \
../../media/platform/signalproc/webrtc/modules/audio_processing/ns/noise_suppression.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/ns/noise_suppression_x.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/ns/ns_core.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/ns/nsx_core.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/ns/nsx_core_c.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/processing_component.cc \
../../media/platform/signalproc/webrtc/modules/audio_processing/utility/delay_estimator.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/utility/delay_estimator_wrapper.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/utility/fft4g.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/utility/ring_buffer.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/voice_detection_impl.cc \
../../media/platform/signalproc/webrtc/system_wrappers/source/aligned_malloc.cc \
../../media/platform/signalproc/webrtc/system_wrappers/source/critical_section.cc \
../../media/platform/signalproc/webrtc/system_wrappers/source/critical_section_posix.cc \
../../media/platform/signalproc/webrtc/system_wrappers/source/cpu_features_android.c \
../../media/mmapi/playback.c \
../../media/mmapi/captofile.c \
../../media/mmapi/mfio.c \
../../media/mmapi/writer.c \
../../media/frontend/ramfile/ramfile.c

CODE_C := \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/complex_bit_reverse.c \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/spl_sqrt_floor.c

CODE_ARM := \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/complex_bit_reverse_arm.S \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/spl_sqrt_floor_arm.S \

CODE_NEON := \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/complex_bit_reverse_arm.S \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/cross_correlation_neon.S \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/downsample_fast_neon.S \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/min_max_operations_neon.S \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/spl_sqrt_floor_arm.S \
../../media/platform/signalproc/webrtc/common_audio/signal_processing/vector_scaling_operations_neon.S \
../../media/platform/signalproc/webrtc/modules/audio_processing/aecm/aecm_core_neon_offsets.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/aecm/aecm_core_neon.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/ns/nsx_core_neon_offsets.c \
../../media/platform/signalproc/webrtc/modules/audio_processing/ns/nsx_core_neon.c

MEDIA_DIR := $(LOCAL_PATH)/../../media
LOCAL_C_INCLUDES := $(MEDIA_DIR)
LOCAL_C_INCLUDES += $(MEDIA_DIR)/mmapi
LOCAL_C_INCLUDES += $(MEDIA_DIR)/comn
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/ffmpeg
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/silk
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/speex
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/speex/libspeex
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/speex/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/h264/x264
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/h265
LOCAL_C_INCLUDES += $(MEDIA_DIR)/ffmpeg/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform/android
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform/signalproc
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform/signalproc/webrtc/modules/audio_processing/aecm
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform/signalproc/webrtc/modules/audio_processing/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform/signalproc/webrtc/modules/interface
LOCAL_C_INCLUDES += $(MEDIA_DIR)/frontend
LOCAL_C_INCLUDES += $(MEDIA_DIR)/frontend/capture
LOCAL_C_INCLUDES += $(MEDIA_DIR)/frontend/ramfile
LOCAL_C_INCLUDES += $(MEDIA_DIR)/backend
LOCAL_C_INCLUDES += $(MEDIA_DIR)/muxer

LOCAL_CONLYFLAGS += -std=gnu99
LOCAL_CFLAGS += -Wno-multichar -marm -rdynamic -fPIC -flax-vector-conversions -DHAVE_CONFIG_H \
                -DWEBRTC_NS_FIXED -DWEBRTC_LINUX -DWEBRTC_ANDROID -Dasm=__asm__ -D__ANDROID__

ifeq ($(APP_ABI), armeabi)
    LOCAL_SRC_FILES += $(CODE_ARM)
    LOCAL_CFLAGS += -march=armv6
else
    LOCAL_SRC_FILES += $(CODE_NEON)
    LOCAL_STATIC_LIBRARIES := libavcodec_armv7 libavutil_armv7 libopus_armv7 libx264_armv7 libx265_armv7
    LOCAL_CFLAGS += -mfpu=neon -march=armv7-a -mfloat-abi=softfp -D__ARM_ARCH_7A__ -D__ARM_NEON__
endif

ifeq ($(APP_OPTIM),release)
    LOCAL_CFLAGS += -DNDEBUG -O3
else
    cmd-strip :=
    LOCAL_CFLAGS += -DDEBUG -O0 -ggdb
endif

LOCAL_LDLIBS += -landroid -llog -lGLESv2 -lEGL -lm  -lc
include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../media

LOCAL_MODULE := omx40
LOCAL_SRC_FILES :=  \
$(MEDIA_DIR)/codec/omx/dec.cpp \
$(MEDIA_DIR)/codec/omx/enc.cpp \
$(MEDIA_DIR)/codec/omx/mtk.c \
$(MEDIA_DIR)/codec/omx/qcom.c \
$(MEDIA_DIR)/codec/omx/ti.c \
$(MEDIA_DIR)/codec/omx/sec.cpp \
$(MEDIA_DIR)/codec/omx/media_source.cpp \
$(MEDIA_DIR)/codec/omx/omxil.cpp

VERSION := 4.0

LOCAL_C_INCLUDES := $(MEDIA_DIR)
LOCAL_C_INCLUDES += $(MEDIA_DIR)/comn
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform/android
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/system/core/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/hardware/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/hardware/libhardware/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/frameworks/base/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/frameworks/base/include/media/stagefright/openmax
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/frameworks/base/native/include

LOCAL_CFLAGS := -Wno-multichar -fPIC
LOCAL_CONLYFLAGS += -std=gnu99
LOCAL_LDLIBS := -L../libs/armeabi-v7a -lmedia2 -L$(MEDIA_DIR)/codec/omx/aosp/lib/$(VERSION) -lstagefright -lutils -lbinder -llog
include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../media

LOCAL_MODULE := omx41
LOCAL_SRC_FILES :=  \
$(MEDIA_DIR)/codec/omx/dec.cpp \
$(MEDIA_DIR)/codec/omx/enc.cpp \
$(MEDIA_DIR)/codec/omx/mtk.c \
$(MEDIA_DIR)/codec/omx/qcom.c \
$(MEDIA_DIR)/codec/omx/ti.c \
$(MEDIA_DIR)/codec/omx/sec.cpp \
$(MEDIA_DIR)/codec/omx/media_source.cpp \
$(MEDIA_DIR)/codec/omx/omxil.cpp

VERSION := 4.1

LOCAL_C_INCLUDES := $(MEDIA_DIR)
LOCAL_C_INCLUDES += $(MEDIA_DIR)/comn
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform/android
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/system/core/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/hardware/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/hardware/libhardware/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/frameworks/native/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/frameworks/native/include/media/openmax
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/frameworks/av/include

LOCAL_CFLAGS := -Wno-multichar -fPIC
LOCAL_CONLYFLAGS += -std=gnu99
LOCAL_LDLIBS := -L../libs/armeabi-v7a -lmedia2 -L$(MEDIA_DIR)/codec/omx/aosp/lib/$(VERSION) -lstagefright -lutils -lbinder -llog
include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../media

LOCAL_MODULE := omx50
LOCAL_SRC_FILES :=  \
$(MEDIA_DIR)/codec/omx/dec.cpp \
$(MEDIA_DIR)/codec/omx/enc.cpp \
$(MEDIA_DIR)/codec/omx/mtk.c \
$(MEDIA_DIR)/codec/omx/qcom.c \
$(MEDIA_DIR)/codec/omx/ti.c \
$(MEDIA_DIR)/codec/omx/sec.cpp \
$(MEDIA_DIR)/codec/omx/media_source.cpp \
$(MEDIA_DIR)/codec/omx/omxil.cpp

VERSION := 5.0

LOCAL_C_INCLUDES := $(MEDIA_DIR)
LOCAL_C_INCLUDES += $(MEDIA_DIR)/comn
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform/android
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/system/core/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/hardware/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/hardware/libhardware/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/frameworks/native/include
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/frameworks/native/include/media/openmax
LOCAL_C_INCLUDES += $(MEDIA_DIR)/codec/omx/aosp/header/$(VERSION)/frameworks/av/include

LOCAL_CFLAGS := -Wno-multichar -fPIC
LOCAL_CONLYFLAGS += -std=gnu99
LOCAL_LDLIBS := -L../libs/armeabi-v7a -lmedia2 -L$(MEDIA_DIR)/codec/omx/aosp/lib/$(VERSION) -lstagefright -lutils -lbinder -llog
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
MEDIA_DIR := $(LOCAL_PATH)/../../media
LOCAL_MODULE := runtime
LOCAL_SRC_FILES :=  \
../../media/platform/android/runtime.c \
../../media/platform/android/log.c \
../../media/comn/backtrace.c \
../../media/comn/now.c \
$(MEDIA_DIR)/comn/myjni.c

LOCAL_C_INCLUDES := $(MEDIA_DIR)
LOCAL_C_INCLUDES += $(MEDIA_DIR)/comn
LOCAL_C_INCLUDES += $(MEDIA_DIR)/platform/android
LOCAL_CONLYFLAGS += -std=gnu99
LOCAL_CFLAGS := -fPIC
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)

