
#include "mpu.h"
#include "mbuf.h"
#include "media_buffer.h"
#include "my_errno.h"
#include "mem.h"
#include "fmt.h"
#include "x265.h"
#include <string.h>

#if defined(X265_CSP_NV21)
#pragma error "X265_CSP_NV21 have been defined"
#else
#define X265_CSP_NV21 X265_CSP_COUNT
#endif

struct x265_wrapper_t;
typedef struct x265_wrapper_t x265_wrapper_t;
struct x265_wrapper_t {
    x265_encoder* x265;
    x265_picture pic;
    struct my_buffer* sps_pps;
    uint32_t seq;
    media_iden_t id;
    fourcc** cc_ptr;
    uintptr_t reflost;

    uint8_t* uv;
    intptr_t nb;
    intptr_t (*csp_convert) (x265_wrapper_t* x265, media_buffer* media);
};

static intptr_t packed_uv_to_i420(x265_wrapper_t* x265, media_buffer* media)
{
    video_format* fmt = to_video_format(media->pptr_cc);
    int bytes = media->vp[1].stride * media->vp[1].height;

    if (bytes > x265->nb) {
        uint8_t* uv = my_malloc(bytes);
        if (uv == NULL) {
            return -1;
        }

        if (x265->uv != NULL) {
            my_free(x265->uv);
        }
        x265->uv = uv;
        x265->nb = bytes;
    }

    bytes /= 2;
    uint8_t *u, *v;
    x265->pic.planes[1] = u = x265->uv;
    x265->pic.planes[2] = v = x265->uv + bytes;

    if ((fmt->pixel->csp == csp_nv12 || fmt->pixel->csp == csp_nv12ref)) {
        uint8_t* pixel = (uint8_t *) media->vp[1].ptr;
        for (int j = 0; j < media->vp[1].height; j++) {
            uint8_t* vu = pixel;

            for (int i = 0; i < media->vp[1].stride; i += 2) {
                *(u++) = *(vu++);
                *(v++) = *(vu++);
            }

            pixel += media->vp[1].stride;
        }

    } else {
        uint8_t* pixel = (uint8_t *) media->vp[1].ptr;
        for (int j = 0; j < media->vp[1].height; j++) {
            uint8_t* vu = pixel;

            for (int i = 0; i < media->vp[1].stride; i += 2) {
                *(v++) = *(vu++);
                *(u++) = *(vu++);
            }
            pixel += media->vp[1].stride;
        }
    }

    x265->pic.stride[1] = x265->pic.stride[2] = media->vp[1].stride / 2;
    return 0;
}

static int x265_csp_from_fmt(x265_wrapper_t* x265, video_format* fmt)
{
    uint32_t csp = fmt->pixel->csp;
    switch (csp) {
        case csp_rgb:
            my_assert2(0, "not support rgb");
            return -1;
        case csp_bgra:
            my_assert2(0, "not support rgba");
            return -1;

        case csp_nv12:
        case csp_nv12ref:
            x265->csp_convert = packed_uv_to_i420;
            return X265_CSP_I420;

        case csp_nv21:
        case csp_nv21ref:
            x265->csp_convert = packed_uv_to_i420;
            return X265_CSP_I420;

        case csp_i420:
        case csp_i420ref: return X265_CSP_I420;
        default: my_assert(0);
    }
    return -1;
}

static int frame_type_from_x265(int type)
{
    switch (type) {
        case X265_TYPE_IDR: return video_type_idr;
        case X265_TYPE_I: return video_type_i;
        case X265_TYPE_P: return video_type_p;
        case X265_TYPE_B: return video_type_b;
        case X265_TYPE_BREF: return video_type_bref;
        default: my_assert(0);
    }
    return -1;
}

static struct my_buffer* video_stream_init(x265_encoder* x265)
{
    x265_nal* sps_pps;
    uint32_t nr_nal;
    int n = x265_encoder_headers(x265, &sps_pps, &nr_nal);
    if (n == -1) {
        return NULL;
    }

    struct my_buffer* mbuf = mbuf_alloc_2((uint32_t) (sizeof(media_buffer) + n));
    if (mbuf == NULL) {
        return NULL;
    }
    mbuf->ptr[1] = mbuf->ptr[0] + sizeof(media_buffer);
    mbuf->length -= sizeof(media_buffer);

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    memset(media->vp, 0, sizeof(media->vp));
    media->angle = 0;
    media->frametype = video_type_sps;
    media->fragment[0] = 0;
    media->fragment[1] = 1;
    media->vp[0].stride = (uint32_t) mbuf->length;
    media->vp[0].height = 1;
    media->vp[0].ptr = mbuf->ptr[1];

    char* pch = media->vp[0].ptr;
    uintptr_t bytes = media->vp[0].stride;
    for (int i = 0; i < nr_nal; ++i) {
        my_assert(bytes >= sps_pps[i].sizeBytes);
        bytes -= sps_pps[i].sizeBytes;

        if (1) {
            memcpy(pch, sps_pps[i].payload, sps_pps[i].sizeBytes);
            pch += sps_pps[i].sizeBytes;
        } else {
            mbuf->length -= sps_pps[i].sizeBytes;
        }
    }
    my_assert(bytes == 0);
    media->vp[0].stride = (uint32_t) mbuf->length;

    return mbuf;
}

static void* x265_open(fourcc** in, fourcc** out)
{
    static FILE* file = NULL;
    if (NULL == file) {
        file = freopen("/sdcard/stderr", "w", stderr);
        if (file == NULL) {
            mark("failed");
            return NULL;
        }
        setlinebuf(stderr);
    }

    x265_param param;
    intptr_t n = x265_param_default_preset(&param, "ultrafast", "fastdecode");
    if (n == -1) {
        return NULL;
    }
    video_format* fmt_in = to_video_format(in);

    param.numaPools = strdup("*");
    param.frameNumThreads = 0;
    param.bEnableWavefront = 1;
    param.bDistributeModeAnalysis = 0;
    param.bDistributeMotionEstimation = 0;

    param.logLevel = X265_LOG_FULL;
    param.bLogCuStats = 0;
    param.bEnablePsnr = 0;
    param.bEnableSsim = 0;

    param.internalBitDepth = X265_DEPTH;
    param.internalCsp = X265_CSP_I420;
    param.fpsNum = (int) fmt_in->caparam->fps;
    param.fpsDenom = 1;
    param.sourceWidth = fmt_in->pixel->size->width;
    param.sourceHeight = fmt_in->pixel->size->height;
    param.interlaceMode = 0;
    param.totalFrames = 0;

    param.levelIdc = 0;
    param.uhdBluray = 0;
    param.maxNumReferences = 1;
    param.bAllowNonConformance = 0;

    param.bRepeatHeaders = 0;
    param.bAnnexB = 1;
    param.bEnableAccessUnitDelimiters = 0;
    param.bEmitHRDSEI = 0;
    param.bEmitInfoSEI = 0;
    param.decodedPictureHashSEI = 0;
    param.bEnableTemporalSubLayers = 0;

    param.bOpenGOP = 0;
    param.keyframeMin = (int) fmt_in->caparam->gop;
    param.keyframeMax = (int) fmt_in->caparam->gop;

    n = fmt_in->caparam->fps * 2 / 10 - 1;
    if (n == -1) {
        n = 0;
    }

    param.bframes = (int) n;
    param.bFrameAdaptive = 0;
    param.bBPyramid = 0;
    param.bFrameBias = 0;
    param.lookaheadDepth = (int) (n + 1);
    param.lookaheadSlices = 0;
    param.scenecutThreshold = 0;
    param.bIntraRefresh = 0;
    param.maxCUSize = 32;
    param.minCUSize = 16;
    param.bEnableRectInter = 0;
    param.bEnableAMP = 0;
    param.maxTUSize = 4;
    param.tuQTMaxInterDepth = 1;
    param.tuQTMaxIntraDepth = 1;

    param.bEnableSignHiding = 0;
    param.bEnableTransformSkip = 0;
    param.noiseReductionIntra = 0;
    param.noiseReductionInter = 0;
    param.scalingLists = NULL;
    param.bEnableConstrainedIntra = 0;
    param.bEnableStrongIntraSmoothing = 0;
    param.maxNumMergeCand = 1;
    param.limitReferences = 0;
    param.limitModes = 0;
    param.searchMethod = X265_DIA_SEARCH;
    param.subpelRefine = 0;
    param.searchRange = 57;
    param.bEnableTemporalMvp = 0;
    param.bEnableWeightedPred = 0;
    param.bEnableWeightedBiPred = 0;
    param.bEnableLoopFilter = 0;
    param.deblockingFilterTCOffset = 0;
    param.deblockingFilterBetaOffset = 0;
    param.bEnableSAO = 0;
    param.bSaoNonDeblocked = 0;

    param.bEnableEarlySkip = 1;
    param.bEnableRecursionSkip = 1;
    param.bEnableFastIntra = 1;
    param.bEnableTSkipFast = 1;
    param.bCULossless = 0;
    param.bIntraInBFrames = 0;

    param.rdPenalty = 2;
    param.rdoqLevel = 0;
    param.rdLevel = 1;
    param.psyRd = 1.0;
    param.psyRdoq = 0.0;
    param.bEnableRdRefine = 0;
    param.analysisMode = X265_ANALYSIS_OFF;
    param.analysisFileName = NULL;

    param.bLossless = 0;
    param.cbQpOffset = 0;
    param.crQpOffset = 0;

    param.rc.rateControlMode = X265_RC_ABR;
    param.rc.bitrate = (int) fmt_in->caparam->kbps;
    param.rc.cuTree = 0;

    x265_encoder* p = x265_encoder_open(&param);
    if (p == NULL) {
        return NULL;
    }

    x265_wrapper_t* codec = (x265_wrapper_t *) my_malloc(sizeof(x265_wrapper_t));
    if (codec == NULL) {
        x265_encoder_close(p);
        return NULL;
    }
    codec->seq = 0;
    codec->x265 = p;
    codec->cc_ptr = out;
    codec->reflost = 0;
    codec->uv = NULL;
    codec->nb = 0;
    codec->csp_convert = NULL;

    codec->sps_pps = video_stream_init(p);
    if (codec->sps_pps == NULL) {
        my_free(codec);
        x265_encoder_close(p);
        return NULL;
    }

    media_buffer* media = (media_buffer *) codec->sps_pps->ptr[0];
    media->pptr_cc = out;
    media->seq = codec->seq;
    media->pts = 0;
    media->iden = media_id_unkown;

    x265_encoder_parameters(p, &param);
    x265_picture_init(&param, &codec->pic);
    return codec;
}

static int32_t add_to_list(x265_wrapper_t* codec, x265_picture* pic_out, x265_nal* nal, uintptr_t nr, struct list_head* headp)
{
    int32_t n = 0;
    for (uintptr_t i = 0; i < nr; ++i) {
        uint32_t type = pic_out->sliceType;
        my_assert(X265_TYPE_AUTO != type);

        codec->seq++;
        type = frame_type_from_x265(type);
        if (type != video_type_idr && type != video_type_i && codec->reflost == 1) {
            continue;
        }

        uintptr_t bytes = nal[i].sizeBytes;
        struct my_buffer* mbuf = (struct my_buffer *) mbuf_alloc_2((uint32_t) (bytes + sizeof(media_buffer)));
        if (mbuf == NULL) {
            if (reference_able(type)) {
                codec->reflost = 1;
            }
            continue;
        }

        mbuf->ptr[1] = mbuf->ptr[0] + sizeof(media_buffer);
        mbuf->length = bytes;
        memcpy(mbuf->ptr[1], nal[i].payload, bytes);

        media_buffer* stream = (media_buffer *) mbuf->ptr[0];
        memset(stream->vp, 0, sizeof(stream->vp));
        stream->frametype = type;
        stream->angle = (uint32_t) (intptr_t) pic_out->userData;
        stream->fragment[0] = 0;
        stream->fragment[1] = 1;
        stream->iden = codec->id;
        stream->pptr_cc = codec->cc_ptr;
        stream->pts = pic_out->pts;
        stream->seq = codec->seq;
        stream->vp[0].stride = (uint32_t) mbuf->length;
        stream->vp[0].ptr = mbuf->ptr[1];

        if (reference_able(stream->frametype)) {
            if (stream->frametype == video_type_idr) {
                codec->reflost = 0;
            }
        }
        list_add_tail(&mbuf->head, headp);
        ++n;
    }
    return n;
}

static int32_t flush_delayed_frame(x265_wrapper_t* codec, struct list_head* head)
{
    x265_picture pic_out;
    x265_nal* nal;
    uint32_t nr_nal, n = 0;

    do {
        int bytes = x265_encoder_encode(codec->x265, &nal, &nr_nal, NULL, &pic_out);
        if (bytes < 0) {
            return n;
        }

        n += add_to_list(codec, &pic_out, nal, nr_nal, head);
     } while (1);
    return n;
}

static int32_t x265_write(void* handle, struct my_buffer* mbuf, struct list_head* head, int32_t* delay)
{
    (void) delay;
    x265_wrapper_t* codec = (x265_wrapper_t *) handle;
    if (__builtin_expect(mbuf == NULL, 0)) {
        if (0 && codec->sps_pps == NULL) {
            return flush_delayed_frame(codec, head);
        }
        return 0;
    }

    media_buffer* media = (media_buffer *) mbuf->ptr[0];
    video_format* fmt = to_video_format(media->pptr_cc);
    for (uint32_t i = 0; i < fmt->pixel->panels; ++i) {
        codec->pic.planes[i] = (uint8_t *) media->vp[i].ptr;
        codec->pic.stride[i] = (int) media->vp[i].stride;
    }

    codec->pic.pts = media->pts;
    codec->pic.userData = (void *) (intptr_t) media->angle;
    codec->pic.colorSpace = x265_csp_from_fmt(codec, fmt);
    if (codec->csp_convert && -1 == codec->csp_convert(codec, media)) {
        return -1;
    }

    x265_picture pic_out;
    x265_nal* nal;
    uint32_t nr_nal;

    int bytes = x265_encoder_encode(codec->x265, &nal, &nr_nal, &codec->pic, &pic_out);
    if(bytes < 0){
        return -1;
    }

    codec->id = media->iden;
    intptr_t n = add_to_list(codec, &pic_out, nal, nr_nal, head);
    if (codec->sps_pps) {
        media_buffer* media = (media_buffer *) codec->sps_pps->ptr[0];
        media->pts = codec->pic.pts;
        media->iden = codec->id;
        media->angle = (uint32_t) (intptr_t) codec->pic.userData;
        list_add(&codec->sps_pps->head, head);
        stream_begin(codec->sps_pps);
        codec->sps_pps = NULL;
        n += 1;
    }
    mbuf->mop->free(mbuf);
    return (int32_t) n;
}

static void x265_close(void* handle)
{
    x265_wrapper_t* codec = (x265_wrapper_t *) handle;
    stream_end(codec->id, codec->cc_ptr);
    x265_encoder_close(codec->x265);
    if (codec->sps_pps != NULL) {
        codec->sps_pps->mop->free(codec->sps_pps);
    }
    my_free(codec);
}

static mpu_operation x265_ops = {
    x265_open,
    x265_write,
    x265_close
};

media_process_unit x265_h265_enc = {
    &raw_0x0.cc,
    &h265_0x0.cc,
    &x265_ops,
    10,
    mpu_encoder,
    "x265_h265_enc"
};
