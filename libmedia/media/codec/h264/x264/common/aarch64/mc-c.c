/*****************************************************************************
 * mc-c.c: aarch64 motion compensation
 *****************************************************************************
 * Copyright (C) 2009-2015 x264 project
 *
 * Authors: David Conrad <lessen42@gmail.com>
 *          Janne Grunau <janne-x264@jannau.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at licensing@x264.com.
 *****************************************************************************/

#include "common/common_x264.h"
#if ARCH_AARCH64
#include "mc.h"

void x264_prefetch_ref_aarch64( uint8_t *, intptr_t, int );
void x264_prefetch_fenc_420_aarch64( uint8_t *, intptr_t, uint8_t *, intptr_t, int );
void x264_prefetch_fenc_422_aarch64( uint8_t *, intptr_t, uint8_t *, intptr_t, int );

void *x264_memcpy_aligned_neon( void *dst, const void *src, size_t n );
void x264_memzero_aligned_neon( void *dst, size_t n );

void x264_pixel_avg_16x16_neon( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, intptr_t, int );
void x264_pixel_avg_16x8_neon ( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, intptr_t, int );
void x264_pixel_avg_8x16_neon ( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, intptr_t, int );
void x264_pixel_avg_8x8_neon  ( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, intptr_t, int );
void x264_pixel_avg_8x4_neon  ( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, intptr_t, int );
void x264_pixel_avg_4x16_neon ( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, intptr_t, int );
void x264_pixel_avg_4x8_neon  ( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, intptr_t, int );
void x264_pixel_avg_4x4_neon  ( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, intptr_t, int );
void x264_pixel_avg_4x2_neon  ( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, intptr_t, int );

void x264_pixel_avg2_w4_neon ( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, int );
void x264_pixel_avg2_w8_neon ( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, int );
void x264_pixel_avg2_w16_neon( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, int );
void x264_pixel_avg2_w20_neon( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, int );

void x264_plane_copy_neon( pixel *dst, intptr_t i_dst,
                           pixel *src, intptr_t i_src, int w, int h );
void x264_plane_copy_deinterleave_neon(  pixel *dstu, intptr_t i_dstu,
                                         pixel *dstv, intptr_t i_dstv,
                                         pixel *src,  intptr_t i_src, int w, int h );
void x264_plane_copy_deinterleave_rgb_neon( pixel *dsta, intptr_t i_dsta,
                                            pixel *dstb, intptr_t i_dstb,
                                            pixel *dstc, intptr_t i_dstc,
                                            pixel *src,  intptr_t i_src, int pw, int w, int h );
void x264_plane_copy_interleave_neon( pixel *dst,  intptr_t i_dst,
                                      pixel *srcu, intptr_t i_srcu,
                                      pixel *srcv, intptr_t i_srcv, int w, int h );

void x264_store_interleave_chroma_neon( pixel *dst, intptr_t i_dst, pixel *srcu, pixel *srcv, int height );
void x264_load_deinterleave_chroma_fdec_neon( pixel *dst, pixel *src, intptr_t i_src, int height );
void x264_load_deinterleave_chroma_fenc_neon( pixel *dst, pixel *src, intptr_t i_src, int height );

#define MC_WEIGHT(func)\
void x264_mc_weight_w20##func##_neon( uint8_t *, intptr_t, uint8_t *, intptr_t, const x264_weight_t *, int );\
void x264_mc_weight_w16##func##_neon( uint8_t *, intptr_t, uint8_t *, intptr_t, const x264_weight_t *, int );\
void x264_mc_weight_w8##func##_neon ( uint8_t *, intptr_t, uint8_t *, intptr_t, const x264_weight_t *, int );\
void x264_mc_weight_w4##func##_neon ( uint8_t *, intptr_t, uint8_t *, intptr_t, const x264_weight_t *, int );\
\
static void (* x264_mc##func##_wtab_neon[6])( uint8_t *, intptr_t, uint8_t *, intptr_t, const x264_weight_t *, int ) =\
{\
    x264_mc_weight_w4##func##_neon,\
    x264_mc_weight_w4##func##_neon,\
    x264_mc_weight_w8##func##_neon,\
    x264_mc_weight_w16##func##_neon,\
    x264_mc_weight_w16##func##_neon,\
    x264_mc_weight_w20##func##_neon,\
};

MC_WEIGHT()
MC_WEIGHT(_nodenom)
MC_WEIGHT(_offsetadd)
MC_WEIGHT(_offsetsub)

void x264_mc_copy_w4_neon ( uint8_t *, intptr_t, uint8_t *, intptr_t, int );
void x264_mc_copy_w8_neon ( uint8_t *, intptr_t, uint8_t *, intptr_t, int );
void x264_mc_copy_w16_neon( uint8_t *, intptr_t, uint8_t *, intptr_t, int );

void x264_mc_chroma_neon( uint8_t *, uint8_t *, intptr_t, uint8_t *, intptr_t, int, int, int, int );
void integral_init4h_neon( uint16_t *, uint8_t *, intptr_t );
void integral_init4v_neon( uint16_t *, uint16_t *, intptr_t );
void integral_init8h_neon( uint16_t *, uint8_t *, intptr_t );
void integral_init8v_neon( uint16_t *, intptr_t );
void x264_frame_init_lowres_core_neon( uint8_t *, uint8_t *, uint8_t *, uint8_t *, uint8_t *, intptr_t, intptr_t, int, int );

void x264_mbtree_propagate_cost_neon( int16_t *, uint16_t *, uint16_t *, uint16_t *, uint16_t *, float *, int );

#if !HIGH_BIT_DEPTH
static void x264_weight_cache_neon( x264_t *h, x264_weight_t *w )
{
    if( w->i_scale == 1<<w->i_denom )
    {
        if( w->i_offset < 0 )
        {
            w->weightfn = x264_mc_offsetsub_wtab_neon;
            w->cachea[0] = -w->i_offset;
        }
        else
        {
            w->weightfn = x264_mc_offsetadd_wtab_neon;
            w->cachea[0] = w->i_offset;
        }
    }
    else if( !w->i_denom )
        w->weightfn = x264_mc_nodenom_wtab_neon;
    else
        w->weightfn = x264_mc_wtab_neon;
}

static void (* const x264_pixel_avg_wtab_neon[6])( uint8_t *, intptr_t, uint8_t *, intptr_t, uint8_t *, int ) =
{
    NULL,
    x264_pixel_avg2_w4_neon,
    x264_pixel_avg2_w8_neon,
    x264_pixel_avg2_w16_neon,   // no slower than w12, so no point in a separate function
    x264_pixel_avg2_w16_neon,
    x264_pixel_avg2_w20_neon,
};

static void (* const x264_mc_copy_wtab_neon[5])( uint8_t *, intptr_t, uint8_t *, intptr_t, int ) =
{
    NULL,
    x264_mc_copy_w4_neon,
    x264_mc_copy_w8_neon,
    NULL,
    x264_mc_copy_w16_neon,
};

static void mc_luma_neon( uint8_t *dst,    intptr_t i_dst_stride,
                          uint8_t *src[4], intptr_t i_src_stride,
                          int mvx, int mvy,
                          int i_width, int i_height, const x264_weight_t *weight )
{
    int qpel_idx = ((mvy&3)<<2) + (mvx&3);
    intptr_t offset = (mvy>>2)*i_src_stride + (mvx>>2);
    uint8_t *src1 = src[x264_hpel_ref0[qpel_idx]] + offset;
    if ( (mvy&3) == 3 )             // explict if() to force conditional add
        src1 += i_src_stride;

    if( qpel_idx & 5 ) /* qpel interpolation needed */
    {
        uint8_t *src2 = src[x264_hpel_ref1[qpel_idx]] + offset + ((mvx&3) == 3);
        x264_pixel_avg_wtab_neon[i_width>>2](
                dst, i_dst_stride, src1, i_src_stride,
                src2, i_height );
        if( weight->weightfn )
            weight->weightfn[i_width>>2]( dst, i_dst_stride, dst, i_dst_stride, weight, i_height );
    }
    else if( weight->weightfn )
        weight->weightfn[i_width>>2]( dst, i_dst_stride, src1, i_src_stride, weight, i_height );
    else
        x264_mc_copy_wtab_neon[i_width>>2]( dst, i_dst_stride, src1, i_src_stride, i_height );
}

static uint8_t *get_ref_neon( uint8_t *dst,   intptr_t *i_dst_stride,
                              uint8_t *src[4], intptr_t i_src_stride,
                              int mvx, int mvy,
                              int i_width, int i_height, const x264_weight_t *weight )
{
    int qpel_idx = ((mvy&3)<<2) + (mvx&3);
    intptr_t offset = (mvy>>2)*i_src_stride + (mvx>>2);
    uint8_t *src1 = src[x264_hpel_ref0[qpel_idx]] + offset;
    if ( (mvy&3) == 3 )             // explict if() to force conditional add
        src1 += i_src_stride;

    if( qpel_idx & 5 ) /* qpel interpolation needed */
    {
        uint8_t *src2 = src[x264_hpel_ref1[qpel_idx]] + offset + ((mvx&3) == 3);
        x264_pixel_avg_wtab_neon[i_width>>2](
                dst, *i_dst_stride, src1, i_src_stride,
                src2, i_height );
        if( weight->weightfn )
            weight->weightfn[i_width>>2]( dst, *i_dst_stride, dst, *i_dst_stride, weight, i_height );
        return dst;
    }
    else if( weight->weightfn )
    {
        weight->weightfn[i_width>>2]( dst, *i_dst_stride, src1, i_src_stride, weight, i_height );
        return dst;
    }
    else
    {
        *i_dst_stride = i_src_stride;
        return src1;
    }
}

void x264_hpel_filter_neon( uint8_t *dsth, uint8_t *dstv, uint8_t *dstc,
                            uint8_t *src, intptr_t stride, int width,
                            int height, int16_t *buf );
#endif // !HIGH_BIT_DEPTH

#define CLIP_ADD(s,x) (s) = X264_MIN((s)+(x),(1<<15)-1)
#define CLIP_ADD2(s,x)\
do\
{\
    CLIP_ADD((s)[0], (x)[0]);\
    CLIP_ADD((s)[1], (x)[1]);\
} while(0)

void x264_mbtree_propagate_list_internal_neon( int16_t (*mvs)[2],
                                               int16_t *propagate_amount,
                                               uint16_t *lowres_costs,
                                               int16_t *output,
                                               int bipred_weight, int mb_y,
                                               int len );

static void x264_mbtree_propagate_list_neon( x264_t *h, uint16_t *ref_costs,
                                             int16_t (*mvs)[2],
                                             int16_t *propagate_amount,
                                             uint16_t *lowres_costs,
                                             int bipred_weight, int mb_y,
                                             int len, int list )
{
    int16_t *current = h->scratch_buffer2;

    x264_mbtree_propagate_list_internal_neon( mvs, propagate_amount,
                                              lowres_costs, current,
                                              bipred_weight, mb_y, len );

    unsigned stride = h->mb.i_mb_stride;
    unsigned width = h->mb.i_mb_width;
    unsigned height = h->mb.i_mb_height;

    for( unsigned i = 0; i < len; current += 32 )
    {
        int end = X264_MIN( i+8, len );
        for( ; i < end; i++, current += 2 )
        {
            if( !(lowres_costs[i] & (1 << (list+LOWRES_COST_SHIFT))) )
                continue;

            unsigned mbx = current[0];
            unsigned mby = current[1];
            unsigned idx0 = mbx + mby * stride;
            unsigned idx2 = idx0 + stride;

            /* Shortcut for the simple/common case of zero MV */
            if( !M32( mvs[i] ) )
            {
                CLIP_ADD( ref_costs[idx0], current[16] );
                continue;
            }

            if( mbx < width-1 && mby < height-1 )
            {
                CLIP_ADD2( ref_costs+idx0, current+16 );
                CLIP_ADD2( ref_costs+idx2, current+32 );
            }
            else
            {
                /* Note: this takes advantage of unsigned representation to
                 * catch negative mbx/mby. */
                if( mby < height )
                {
                    if( mbx < width )
                        CLIP_ADD( ref_costs[idx0+0], current[16] );
                    if( mbx+1 < width )
                        CLIP_ADD( ref_costs[idx0+1], current[17] );
                }
                if( mby+1 < height )
                {
                    if( mbx < width )
                        CLIP_ADD( ref_costs[idx2+0], current[32] );
                    if( mbx+1 < width )
                        CLIP_ADD( ref_costs[idx2+1], current[33] );
                }
            }
        }
    }
}

#undef CLIP_ADD
#undef CLIP_ADD2

void x264_mc_init_aarch64( int cpu, x264_mc_functions_t *pf )
{
#if !HIGH_BIT_DEPTH
    if( cpu&X264_CPU_ARMV8 )
    {
        pf->prefetch_fenc_420 = x264_prefetch_fenc_420_aarch64;
        pf->prefetch_fenc_422 = x264_prefetch_fenc_422_aarch64;
        pf->prefetch_ref      = x264_prefetch_ref_aarch64;
    }

    if( !(cpu&X264_CPU_NEON) )
        return;

    pf->copy_16x16_unaligned = x264_mc_copy_w16_neon;
    pf->copy[PIXEL_16x16]    = x264_mc_copy_w16_neon;
    pf->copy[PIXEL_8x8]      = x264_mc_copy_w8_neon;
    pf->copy[PIXEL_4x4]      = x264_mc_copy_w4_neon;

    pf->plane_copy                  = x264_plane_copy_neon;
    pf->plane_copy_deinterleave     = x264_plane_copy_deinterleave_neon;
    pf->plane_copy_deinterleave_rgb = x264_plane_copy_deinterleave_rgb_neon;
    pf->plane_copy_interleave       = x264_plane_copy_interleave_neon;

    pf->load_deinterleave_chroma_fdec = x264_load_deinterleave_chroma_fdec_neon;
    pf->load_deinterleave_chroma_fenc = x264_load_deinterleave_chroma_fenc_neon;
    pf->store_interleave_chroma       = x264_store_interleave_chroma_neon;

    pf->avg[PIXEL_16x16] = x264_pixel_avg_16x16_neon;
    pf->avg[PIXEL_16x8]  = x264_pixel_avg_16x8_neon;
    pf->avg[PIXEL_8x16]  = x264_pixel_avg_8x16_neon;
    pf->avg[PIXEL_8x8]   = x264_pixel_avg_8x8_neon;
    pf->avg[PIXEL_8x4]   = x264_pixel_avg_8x4_neon;
    pf->avg[PIXEL_4x16]  = x264_pixel_avg_4x16_neon;
    pf->avg[PIXEL_4x8]   = x264_pixel_avg_4x8_neon;
    pf->avg[PIXEL_4x4]   = x264_pixel_avg_4x4_neon;
    pf->avg[PIXEL_4x2]   = x264_pixel_avg_4x2_neon;

    pf->weight       = x264_mc_wtab_neon;
    pf->offsetadd    = x264_mc_offsetadd_wtab_neon;
    pf->offsetsub    = x264_mc_offsetsub_wtab_neon;
    pf->weight_cache = x264_weight_cache_neon;

    pf->mc_chroma = x264_mc_chroma_neon;
    pf->mc_luma = mc_luma_neon;
    pf->get_ref = get_ref_neon;
    pf->hpel_filter = x264_hpel_filter_neon;
    pf->frame_init_lowres_core = x264_frame_init_lowres_core_neon;

    pf->integral_init4h = integral_init4h_neon;
    pf->integral_init8h = integral_init8h_neon;
    pf->integral_init4v = integral_init4v_neon;
    pf->integral_init8v = integral_init8v_neon;

    pf->mbtree_propagate_cost = x264_mbtree_propagate_cost_neon;
    pf->mbtree_propagate_list = x264_mbtree_propagate_list_neon;

    pf->memcpy_aligned  = x264_memcpy_aligned_neon;
    pf->memzero_aligned = x264_memzero_aligned_neon;
#endif // !HIGH_BIT_DEPTH
}

#endif
