/* Copyright (C) 2002 Jean-Marc Valin */
/**
   @file quant_lsp.h
   @brief LSP vector quantization
*/
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   
   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   
   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
   
   - Neither the name of the Xiph.org Foundation nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.
   
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef QUANT_LSP_H
#define QUANT_LSP_H

#include "../include/speex/speex_bits.h"
#include "arch.h"

#define MAX_LSP_SIZE 20

#define NB_CDBK_SIZE 64
#define NB_CDBK_SIZE_LOW1 64
#define NB_CDBK_SIZE_LOW2 64
#define NB_CDBK_SIZE_HIGH1 64
#define NB_CDBK_SIZE_HIGH2 64

/*Narrowband codebooks*/

#ifdef __cplusplus
extern "C" const signed char cdbk_nb[];
extern "C" const signed char cdbk_nb_low1[];
extern "C" const signed char cdbk_nb_low2[];
extern "C" const signed char cdbk_nb_high1[];
extern "C" const signed char cdbk_nb_high2[];
#else
extern const signed char cdbk_nb[];
extern const signed char cdbk_nb_low1[];
extern const signed char cdbk_nb_low2[];
extern const signed char cdbk_nb_high1[];
extern const signed char cdbk_nb_high2[];
#endif

/* Quantizes narrowband LSPs with 30 bits */
void lsp_quant_nb(spx_lsp_t *lsp, spx_lsp_t *qlsp, int order, SpeexBits *bits);

/* Decodes quantized narrowband LSPs */
void lsp_unquant_nb(spx_lsp_t *lsp, int order, SpeexBits *bits);

/* Quantizes low bit-rate narrowband LSPs with 18 bits */
void lsp_quant_lbr(spx_lsp_t *lsp, spx_lsp_t *qlsp, int order, SpeexBits *bits);

/* Decodes quantized low bit-rate narrowband LSPs */
void lsp_unquant_lbr(spx_lsp_t *lsp, int order, SpeexBits *bits);

/* Quantizes high-band LSPs with 12 bits */
void lsp_quant_high(spx_lsp_t *lsp, spx_lsp_t *qlsp, int order, SpeexBits *bits);

/* Decodes high-band LSPs */
void lsp_unquant_high(spx_lsp_t *lsp, int order, SpeexBits *bits);

#endif
