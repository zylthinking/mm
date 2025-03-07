/***********************************************************************
Copyright (c) 2006-2011, Skype Limited. All rights reserved. 
Redistribution and use in source and binary forms, with or without 
modification, (subject to the limitations in the disclaimer below) 
are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright 
notice, this list of conditions and the following disclaimer in the 
documentation and/or other materials provided with the distribution.
- Neither the name of Skype Limited, nor the names of specific 
contributors, may be used to endorse or promote products derived from 
this software without specific prior written permission.
NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED 
BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND 
CONTRIBUTORS ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND 
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF 
USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***********************************************************************/

#if defined(__arm__)

#include "SKP_Silk_AsmPreproc.h"
#if EMBEDDED_ARM>=5

val_lz				.req 	r1
val_shift			.req	r2
val_frac			.req	r3

val_ret1			.req	r1
val_128minfrac		.req	r2
val_mul_ret			.req	r0
val_add_ret			.req	r3
const1				.req	r1

.globl	SYM(SKP_Silk_lin2log)
.align  4
SYM(SKP_Silk_lin2log):
	stmdb	sp!,  {r4-r5, fp, ip, lr}
	add		fp, sp, #16
	
	clz		val_lz, r0											/*lz*/
	cmp		val_lz, #24
	sub		val_shift, val_lz, #24
	rsblt	val_shift, val_lz, #24
	movlt	val_frac, r0, asr val_shift
	movge	val_frac, r0, lsl val_shift
	and		val_frac, val_frac, #0x7F							/*frac_Q7*/
	
	rsb		val_ret1, val_lz, #31								/*31-lz*/
	rsb		val_128minfrac, val_frac, #128						/*128-frac_Q7*/
	mov		val_ret1, val_ret1, lsl #7
	mul		val_mul_ret, val_frac, val_128minfrac				/*SKP_MUL(frac_Q7, 128 - frac_Q7)*/
	add		val_add_ret, val_ret1, val_frac
	mov		const1, #179
	smlawb	r0, val_mul_ret, const1, val_add_ret
	
	ldmia	sp!,  {r4-r5, fp, ip, pc}
	
#elif EMBEDDED_ARM>=4
val_lz				.req 	r0
val_in				.req	r4
val_shift			.req	r1
val_frac			.req	r2
val_ret1			.req	r1
val_128minfrac		.req	r3
val_mul_ret			.req	r3
tmp0				.req	r4
const1				.req	r2
val_ret				.req	r0

.globl	SYM(SKP_Silk_lin2log)
.align  4
SYM(SKP_Silk_lin2log):
	stmdb	sp!,  {r4-r5, fp, ip, lr}
	add		fp, sp, #16

	mov		val_in, r0
	bl		SYM(SKP_Silk_CLZ32)
	cmp		val_lz, #24
	sub		val_shift, val_lz, #24
	rsblt	val_shift, val_lz, #24
	movlt	val_frac, val_in, asr val_shift
	movge	val_frac, val_in, lsl val_shift
	and		val_frac, val_frac, #0x7F							/*frac_Q7*/
	
	rsb		val_ret1, val_lz, #31								/*31-lz*/
	rsb		val_128minfrac, val_frac, #128							/*128-frac_Q7*/
	mov		val_ret1, val_ret1, lsl #7
	mul		val_mul_ret, val_frac, val_128minfrac								/*SKP_MUL(frac_Q7, 128 - frac_Q7)*/
	add		val_ret, val_ret1, val_frac
	mov		tmp0, #0
	mov		const1, #0xB30000
	smlal	tmp0, val_ret, val_mul_ret, const1
	
	ldmia	sp!,  {r4-r5, fp, ip, pc}

#endif
#endif
